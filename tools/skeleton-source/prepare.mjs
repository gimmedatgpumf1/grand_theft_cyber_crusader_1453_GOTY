// Flattens the open-anatomy-3d skeleton of one or both bodies into a small OBJ.
//
// The upstream models are ~1M triangles each with one glTF node per bone, and
// their node transforms are the only thing that puts the pieces in the right
// place. This script bakes those transforms, welds seams, decimates each bone
// to a fixed triangle budget, compacts the result and writes one OBJ per body
// with the upstream part names kept as `o` groups.
//
// Usage:
//   npm i @gltf-transform/core @gltf-transform/extensions meshoptimizer
//   OPEN_ANATOMY=/path/to/open-anatomy-3d node tools/skeleton-source/prepare.mjs

import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { MeshoptDecoder, MeshoptSimplifier } from 'meshoptimizer';
import { writeFileSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const OPEN_ANATOMY = process.env.OPEN_ANATOMY || '';
const HERE = dirname(fileURLToPath(import.meta.url));
const CATALOG = join(OPEN_ANATOMY, 'packages/open-anatomy-3d/catalog.json');

// Per-bone triangle budgets. The skull keeps enough to read orbits, jaw line
// and sinus cavities; the long bones are allowed to fall back to a handful of
// quads, because at arm's length they are a straight shaft either way.
const BUDGET = {
  skull: 1400, mandible: 260, teeth: 700,
  spine_cervical: 700, spine_thoracic: 900, spine_lumbar: 700,
  ribs_l: 700, ribs_r: 700, sternum: 160,
  scapula_l: 380, scapula_r: 380, clavicle_l: 90, clavicle_r: 90,
  pelvis: 700,
  humerus_l: 150, humerus_r: 150, forearm_l: 150, forearm_r: 150,
  hand_l: 850, hand_r: 850,
  femur_l: 200, femur_r: 200, patella_l: 60, patella_r: 60,
  lower_leg_l: 200, lower_leg_r: 200, foot_l: 750, foot_r: 750,
};

const IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];

function multiply(a, b) {
  const out = new Array(16).fill(0); // column-major, like glTF
  for (let column = 0; column < 4; ++column) {
    for (let row = 0; row < 4; ++row) {
      let sum = 0;
      for (let k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[column * 4 + k];
      out[column * 4 + row] = sum;
    }
  }
  return out;
}

function localMatrix(node) {
  const [tx, ty, tz] = node.getTranslation();
  const [x, y, z, w] = node.getRotation();
  const [sx, sy, sz] = node.getScale();
  return [
    (1 - 2 * (y * y + z * z)) * sx, (2 * (x * y + z * w)) * sx, (2 * (x * z - y * w)) * sx, 0,
    (2 * (x * y - z * w)) * sy, (1 - 2 * (x * x + z * z)) * sy, (2 * (y * z + x * w)) * sy, 0,
    (2 * (x * z + y * w)) * sz, (2 * (y * z - x * w)) * sz, (1 - 2 * (x * x + y * y)) * sz, 0,
    tx, ty, tz, 1,
  ];
}

function worldMatrix(node) {
  const chain = [];
  for (let current = node; current; current = current.getParentNode()) chain.unshift(current);
  return chain.reduce((acc, current) => multiply(acc, localMatrix(current)), IDENTITY);
}

function apply(matrix, point) {
  return [
    matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
    matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
    matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14],
  ];
}

// Collapses vertices that sit at the same place. Without this the simplifier
// sees a crack at every seam and refuses to close a surface across it.
function weld(positions, indices) {
  const lookup = new Map();
  const remap = [];
  const points = [];
  for (let i = 0; i < positions.length / 3; ++i) {
    const key = `${positions[i * 3].toFixed(5)},${positions[i * 3 + 1].toFixed(5)},${positions[i * 3 + 2].toFixed(5)}`;
    let target = lookup.get(key);
    if (target === undefined) {
      target = points.length / 3;
      lookup.set(key, target);
      points.push(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
    }
    remap.push(target);
  }
  const source = indices.length ? indices : positions.map((_, i) => i / 3);
  return { points, indices: new Uint32Array(source.map(index => remap[index])) };
}

function compact(points, indices) {
  const used = new Map();
  const kept = [];
  const remapped = [];
  for (const index of indices) {
    let target = used.get(index);
    if (target === undefined) {
      target = kept.length / 3;
      used.set(index, target);
      kept.push(points[index * 3], points[index * 3 + 1], points[index * 3 + 2]);
    }
    remapped.push(target);
  }
  return { points: kept, indices: remapped };
}

// Splits an index buffer into connected shells. A hand is one mesh node made of
// forty-six separate bones; simplifying it as one blob gives every phalanx four
// triangles and the fingers turn into spikes, so each shell is handled on its
// own and given a floor it may not fall below.
function shells(indices) {
  const parent = new Map();
  const find = start => {
    let node = start;
    while (parent.get(node) !== node) node = parent.get(node);
    while (parent.get(start) !== node) { const next = parent.get(start); parent.set(start, node); start = next; }
    return node;
  };
  for (const index of indices) if (!parent.has(index)) parent.set(index, index);
  for (let i = 0; i < indices.length; i += 3) {
    const a = find(indices[i]);
    parent.set(find(indices[i + 1]), a);
    parent.set(find(indices[i + 2]), a);
  }
  const groups = new Map();
  for (let i = 0; i < indices.length; i += 3) {
    const root = find(indices[i]);
    if (!groups.has(root)) groups.set(root, []);
    groups.get(root).push(indices[i], indices[i + 1], indices[i + 2]);
  }
  return [...groups.values()];
}

// Simplifies one shell towards `target` triangles, never past `floor` and never
// into a sliver: a shell that collapses to nothing is worse than a small honest
// one, because a finger bone that vanishes also loses its joint.
function simplifyShell(points, indices, target, floor) {
  const triangles = indices.length / 3;
  const wanted = Math.max(floor, Math.min(triangles, target));
  if (triangles <= wanted) return indices;
  const [simplified] = MeshoptSimplifier.simplify(new Uint32Array(indices), new Float32Array(points), 3,
    Math.max(8, Math.round(wanted / 3) * 3), 1e-2, []);
  return simplified.length >= 12 ? Array.from(simplified) : indices;
}

// A part made of many separate shells -- a hand is forty-six bones, a skull is
// a dozen plates, a jaw holds thirty-two teeth -- cannot be simplified as one
// blob without turning every phalanx into a spike. Each shell is simplified on
// its own, then the whole part is trimmed in passes until it fits its budget,
// so a part with too many shells loses triangles from the big ones rather than
// deleting the small ones outright.
function decimateShells(points, indices, budget) {
  const groups = shells(indices);
  let shellsAt = groups.map(group => ({ group, indices: group }));
  const total = () => shellsAt.reduce((sum, shell) => sum + shell.indices.length / 3, 0);
  for (let pass = 0; pass < 4 && total() > budget; ++pass) {
    const factor = budget / total();
    shellsAt = shellsAt.map(shell => {
      // The target is taken from what this shell has now, not from what it
      // started as: the simplifier cannot always reach a target in one go, and
      // measuring against the original count made the second pass grow the
      // shell back instead of shrinking it.
      const current = shell.indices.length / 3;
      if (current <= 12) return shell;
      const target = Math.max(12, Math.floor(current * factor));
      return { group: shell.group, indices: simplifyShell(points, shell.group, target, 12) };
    });
  }
  return shellsAt.flatMap(shell => Array.from(shell.indices));
}

function decimate(name, points, indices) {
  const budget = BUDGET[name] || 200;
  if (indices.length / 3 <= budget) return { points, indices: Array.from(indices) };
  return compact(points, decimateShells(points, Array.from(indices), budget));
}

function bounds(parts) {
  const box = { minX: Infinity, minY: Infinity, minZ: Infinity, maxX: -Infinity, maxY: -Infinity, maxZ: -Infinity };
  for (const part of parts) {
    for (let i = 0; i < part.points.length; i += 3) {
      box.minX = Math.min(box.minX, part.points[i]); box.maxX = Math.max(box.maxX, part.points[i]);
      box.minY = Math.min(box.minY, part.points[i + 1]); box.maxY = Math.max(box.maxY, part.points[i + 1]);
      box.minZ = Math.min(box.minZ, part.points[i + 2]); box.maxZ = Math.max(box.maxZ, part.points[i + 2]);
    }
  }
  return box;
}

function writeObj(path, parts) {
  const lines = ['# Low-poly anatomical skeleton, derived from open-anatomy-3d (CC BY 4.0).',
                 '# See tools/skeleton-source/ATTRIBUTION.md.'];
  let running = 1;
  for (const part of parts) {
    lines.push(`o ${part.name}`);
    for (let i = 0; i < part.points.length; i += 3) {
      lines.push(`v ${part.points[i].toFixed(5)} ${part.points[i + 1].toFixed(5)} ${part.points[i + 2].toFixed(5)}`);
    }
    for (let i = 0; i < part.indices.length; i += 3) {
      lines.push(`f ${running + part.indices[i]} ${running + part.indices[i + 1]} ${running + part.indices[i + 2]}`);
    }
    running += part.points.length / 3;
  }
  writeFileSync(path, lines.join('\n') + '\n');
}

async function exportBody(io, catalog, sex) {
  const source = join(OPEN_ANATOMY, `packages/open-anatomy-3d/models/anatomy-${sex}.glb`);
  const wanted = new Map(catalog.filter(entry => entry.system === 'skeleton').map(entry => [entry.key, entry.label]));
  const doc = await io.read(source);
  const parts = [];
  for (const node of doc.getRoot().listNodes()) {
    const name = node.getName();
    const mesh = node.getMesh();
    if (!mesh || !wanted.has(name)) continue;
    const matrix = worldMatrix(node);
    const positions = [];
    const indices = [];
    for (const primitive of mesh.listPrimitives()) {
      const position = primitive.getAttribute('POSITION');
      if (!position) continue;
      const base = positions.length / 3;
      for (let i = 0; i < position.getCount(); ++i) positions.push(...apply(matrix, position.getElement(i, [])));
      const index = primitive.getIndices();
      if (index) for (let i = 0; i < index.getCount(); ++i) indices.push(base + index.getScalar(i));
      else for (let i = 0; i < position.getCount(); ++i) indices.push(base + i);
    }
    const merged = weld(positions, indices);
    const simplified = decimate(name, merged.points, merged.indices);
    parts.push({ name, label: wanted.get(name), points: simplified.points, indices: simplified.indices });
  }
  const box = bounds(parts);
  const triangles = parts.reduce((sum, part) => sum + part.indices.length / 3, 0);
  const vertices = parts.reduce((sum, part) => sum + part.points.length / 3, 0);
  writeObj(join(HERE, `skeleton-${sex}.obj`), parts);
  writeFileSync(join(HERE, `skeleton-${sex}.parts.json`), JSON.stringify({
    source: `open-anatomy-3d anatomy-${sex}.glb (CC BY 4.0, BodyParts3D)`,
    units: 'metres, feet on y = 0, facing +z',
    vertices,
    triangles,
    bbox: box,
    parts: parts.map(part => ({ name: part.name, label: part.label, tris: part.indices.length / 3 })),
  }, null, 1) + '\n');
  console.log(`${sex}: ${parts.length} parts, ${vertices} verts, ${triangles} tris, `
    + `height ${(box.maxY - box.minY).toFixed(3)} m, width ${(box.maxX - box.minX).toFixed(3)} m`);
}

await MeshoptSimplifier.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({ 'meshopt.decoder': MeshoptDecoder });
const catalog = JSON.parse(readFileSync(CATALOG, 'utf8'));
for (const sex of (process.env.SEXES || 'male,female').split(',')) await exportBody(io, catalog, sex.trim());
