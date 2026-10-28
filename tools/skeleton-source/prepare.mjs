import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { MeshoptDecoder } from 'meshoptimizer';
import { MeshoptSimplifier } from 'meshoptimizer';
import { writeFileSync, readFileSync } from 'node:fs';

import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

// Point OPEN_ANATOMY at a checkout of https://github.com/MervinR07/open-anatomy-3d
const OPEN_ANATOMY = process.env.OPEN_ANATOMY || '';
const SRC = join(OPEN_ANATOMY, 'packages/open-anatomy-3d/models/anatomy-male.glb');
const CATALOG = join(OPEN_ANATOMY, 'packages/open-anatomy-3d/catalog.json');
const OUT = process.env.OUT || join(dirname(fileURLToPath(import.meta.url)), 'skeleton');

await MeshoptSimplifier.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({ 'meshopt.decoder': MeshoptDecoder });
const doc = await io.read(SRC);
const catalog = JSON.parse(readFileSync(CATALOG, 'utf8'));
const wanted = new Map(catalog.filter(c => c.system === 'skeleton').map(c => [c.key, c.label]));

function mul(a, b) {  // column-major 4x4
  const out = new Array(16).fill(0);
  for (let c = 0; c < 4; ++c) for (let r = 0; r < 4; ++r) {
    let sum = 0; for (let k = 0; k < 4; ++k) sum += a[k * 4 + r] * b[c * 4 + k];
    out[c * 4 + r] = sum;
  }
  return out;
}
const ident = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];
function localMatrix(node) {
  const t = node.getTranslation(), r = node.getRotation(), s = node.getScale();
  const [x, y, z, w] = r;
  const m = [
    (1 - 2 * (y * y + z * z)) * s[0], (2 * (x * y + z * w)) * s[0], (2 * (x * z - y * w)) * s[0], 0,
    (2 * (x * y - z * w)) * s[1], (1 - 2 * (x * x + z * z)) * s[1], (2 * (y * z + x * w)) * s[1], 0,
    (2 * (x * z + y * w)) * s[2], (2 * (y * z - x * w)) * s[2], (1 - 2 * (x * x + y * y)) * s[2], 0,
    t[0], t[1], t[2], 1,
  ];
  return m;
}
function worldMatrix(node) {
  const chain = [];
  for (let n = node; n; n = n.getParentNode()) chain.unshift(n);
  return chain.reduce((acc, n) => mul(acc, localMatrix(n)), ident);
}
function apply(m, p) {
  return [m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
          m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
          m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]];
}

const parts = [];
for (const node of doc.getRoot().listNodes()) {
  const name = node.getName();
  const mesh = node.getMesh();
  if (!mesh || !wanted.has(name)) continue;
  const m = worldMatrix(node);
  const positions = [], indices = [];
  for (const prim of mesh.listPrimitives()) {
    const pos = prim.getAttribute('POSITION');
    if (!pos) continue;
    const base = positions.length / 3;
    for (let i = 0; i < pos.getCount(); ++i) positions.push(...apply(m, pos.getElement(i, [])));
    const idx = prim.getIndices();
    if (idx) for (let i = 0; i < idx.getCount(); ++i) indices.push(base + idx.getScalar(i));
    else for (let i = 0; i < pos.getCount(); ++i) indices.push(base + i);
  }
  // Weld duplicate positions so the simplifier can collapse the seams.
  const lookup = new Map(), remap = [], welded = [];
  const src = indices.length ? indices : positions.map((_, i) => i);
  for (let i = 0; i < positions.length / 3; ++i) {
    const key = `${positions[i * 3].toFixed(5)},${positions[i * 3 + 1].toFixed(5)},${positions[i * 3 + 2].toFixed(5)}`;
    let target = lookup.get(key);
    if (target === undefined) {
      target = welded.length / 3;
      lookup.set(key, target);
      welded.push(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
    }
    remap.push(target);
  }
  const weldedIndex = new Uint32Array(src.map(i => remap[i]));
  const before = weldedIndex.length / 3;
  const BUDGET = { skull: 900, mandible: 180, teeth: 320, spine_cervical: 320, spine_thoracic: 460,
    spine_lumbar: 400, ribs_l: 640, ribs_r: 640, sternum: 140, scapula_l: 320, scapula_r: 320,
    clavicle_l: 90, clavicle_r: 90, pelvis: 520, humerus_l: 140, humerus_r: 140,
    forearm_l: 130, forearm_r: 130, hand_l: 420, hand_r: 420, femur_l: 190, femur_r: 190,
    patella_l: 50, patella_r: 50, lower_leg_l: 190, lower_leg_r: 190, foot_l: 300, foot_r: 300 };
  const budget = BUDGET[name] || 200;
  const targetIndexCount = Math.max(24, Math.round(Math.min(budget, before) / 3) * 3);
  let outIndex = weldedIndex;
  if (before > 40) {
    const [simplified] = MeshoptSimplifier.simplify(weldedIndex, new Float32Array(welded), 3, targetIndexCount, 0.05, ['LockBorder']);
    if (simplified.length >= 12) outIndex = simplified;
  }
  // Keep only the vertices the simplified index buffer actually references.
  const used = new Map(), compact = [], compactIndex = [];
  for (let i = 0; i < outIndex.length; ++i) {
    const v = outIndex[i];
    let to = used.get(v);
    if (to === undefined) {
      to = compact.length / 3;
      used.set(v, to);
      compact.push(welded[v * 3], welded[v * 3 + 1], welded[v * 3 + 2]);
    }
    compactIndex.push(to);
  }
  parts.push({ name, label: wanted.get(name), positions: compact, indices: compactIndex, tris: compactIndex.length / 3 });
  console.log(name.padEnd(16), before, '->', outIndex.length / 3);
}

// Normalise: metres, feet on y = 0, facing +z (the source faces -z or +z; check bbox).
let minY = Infinity, maxY = -Infinity, minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
for (const part of parts) for (let i = 0; i < part.positions.length; i += 3) {
  minX = Math.min(minX, part.positions[i]); maxX = Math.max(maxX, part.positions[i]);
  minY = Math.min(minY, part.positions[i + 1]); maxY = Math.max(maxY, part.positions[i + 1]);
  minZ = Math.min(minZ, part.positions[i + 2]); maxZ = Math.max(maxZ, part.positions[i + 2]);
}
console.log('bbox', { minX, maxX, minY, maxY, minZ, maxZ }, 'tris', parts.reduce((a, p) => a + p.tris, 0));

let obj = `# Low-poly anatomical skeleton, derived from open-anatomy-3d (CC BY 4.0).\n# See tools/skeleton-source/ATTRIBUTION.md.\n`;
for (const part of parts) {
  obj += `o ${part.name}\n`;
  const base = obj.split('\n').length; // not used; indices are absolute below
  part.offset = 0;
  for (let i = 0; i < part.positions.length; i += 3)
    obj += `v ${part.positions[i].toFixed(5)} ${part.positions[i + 1].toFixed(5)} ${part.positions[i + 2].toFixed(5)}\n`;
}
// Second pass with running offsets for faces.
let running = 1;
obj = obj.split('\n').filter(l => !l.startsWith('o ')).join('\n');
obj = `# Low-poly anatomical skeleton, derived from open-anatomy-3d (CC BY 4.0).\n# See tools/skeleton-source/ATTRIBUTION.md.\n`;
for (const part of parts) {
  obj += `o ${part.name}\n`;
  for (let i = 0; i < part.positions.length; i += 3)
    obj += `v ${part.positions[i].toFixed(5)} ${part.positions[i + 1].toFixed(5)} ${part.positions[i + 2].toFixed(5)}\n`;
  for (let i = 0; i < part.indices.length; i += 3)
    obj += `f ${running + part.indices[i]} ${running + part.indices[i + 1]} ${running + part.indices[i + 2]}\n`;
  running += part.positions.length / 3;
}
writeFileSync(`${OUT}.obj`, obj);
writeFileSync(`${OUT}.json`, JSON.stringify({
  source: 'open-anatomy-3d anatomy-male.glb (CC BY 4.0, BodyParts3D)',
  parts: parts.map(p => ({ name: p.name, label: p.label, tris: p.tris })), bbox: { minX, maxX, minY, maxY, minZ, maxZ },
}, null, 1));
