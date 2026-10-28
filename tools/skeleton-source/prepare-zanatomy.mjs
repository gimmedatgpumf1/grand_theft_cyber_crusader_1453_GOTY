// Builds the shipped skeleton OBJ from the Z-Anatomy skeletal model.
//
// Z-Anatomy (LluisV/Z-Anatomy, CC BY-SA 4.0) is a curated, medically derived
// atlas: 177 skeletal objects for a single adult body, every bone named down to
// "Distal phalanx of fifth finger of foot", stored at full resolution (721,832
// vertices, 1,069,941 triangles) with the pieces left in a half-body layout.
// This script does four things to it, and nothing else:
//
//   1. keeps only skeletal objects (muscle markings and cross-section helpers
//      are dropped by suffix, not by hand),
//   2. mirrors the left-hand bones to make the body complete,
//   3. groups the bones into the 27 parts the renderer and rig already use,
//   4. simplifies each *bone* separately towards a per-part triangle budget, so
//      a phalanx never collapses because a skull sits in the same group.
//
// It never scales to fit a target height: the only factors applied are the
// centimetre-to-metre conversion (x0.01) and a translation that puts the lowest
// bone on y = 0. The output height is whatever the source body is.
//
// Usage:
//   npm i meshoptimizer            (inside tools/skeleton-source)
//   node tools/skeleton-source/prepare-zanatomy.mjs /path/to/SkeletalSystem100.fbx

import { MeshoptSimplifier } from 'meshoptimizer';
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { load } from './measure-zanatomy.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const SOURCE = process.argv[2];
if (!SOURCE) {
  console.error('usage: node prepare-zanatomy.mjs <SkeletalSystem100.fbx>');
  process.exit(2);
}
await MeshoptSimplifier.ready;

// ---------------------------------------------------------------- selection
// A muscle in this file is a sub-mesh of a bone and is named with a suffix:
// .i/.j for surface markings, .el/.er/.ol/.or and .e1l/.o2r-style for muscle
// origins and insertions. Real bones end in .l/.r or in nothing at all.
const NOT_BONE = /\.(i|j|[eo]\d*[lr])$/;
const NOT_ANATOMY = /^(Cross Section|Reference|Take a picture)/i;

// Triangle budgets per shipped part, chosen so the two bodies stay inside the
// software rasteriser's frame budget while the bones keep readable silhouettes.
// The budgets are the shape of the body; DETAIL multiplies all of them at
// once. The shipped male was built at DETAIL 1, which left a rib with 58
// triangles and the whole body at 18,699 -- visibly faceted next to the
// 130,000-triangle scanned female.
const DETAIL = 3.4;

const BUDGET = {
  skull: 2600, mandible: 220, teeth: 480,
  spine_cervical: 520, spine_thoracic: 1100, spine_lumbar: 620,
  ribs_l: 700, ribs_r: 700, sternum: 180,
  scapula_l: 340, scapula_r: 340, clavicle_l: 80, clavicle_r: 80,
  pelvis: 800,
  humerus_l: 140, humerus_r: 140, forearm_l: 180, forearm_r: 180,
  hand_l: 760, hand_r: 760,
  femur_l: 190, femur_r: 190, patella_l: 50, patella_r: 50,
  lower_leg_l: 210, lower_leg_r: 210, foot_l: 760, foot_r: 760,
};

// Ordered rules: the first one that matches decides where a bone is filed.
// Every rule is written against the Z-Anatomy vocabulary, which is a closed
// list of 160 names; anything unmatched is reported rather than guessed at.
const RULES = [
  [/tooth|incisor|canine|premolar|molar/i, () => 'teeth'],
  [/^mandible$/i, () => 'mandible'],
  [/^(frontal|parietal|occipital|temporal|sphenoid|ethmoid|vomer|nasal bone|inferior nasal concha|lacrimal|zygomatic|maxilla|palatine) bone$|^(sinus of|anterior cells of ethmoid|middle cells of ethmoid|posterior cells of ethmoid)/i, () => 'skull'],
  [/^(malleus|incus|stapes)$/i, () => 'skull'],
  [/nasal septal cartilage|major alar cartilage/i, () => 'skull'],
  [/^(atlas \(c1\)|axis \(c2\)|vertebra c\d)$/i, () => 'spine_cervical'],
  [/^vertebra t\d+$/i, () => 'spine_thoracic'],
  [/^vertebra l\d$/i, () => 'spine_lumbar'],
  [/^(sacrum|coccyx)$/i, () => 'pelvis'],
  [/^(hip bone)$/i, () => 'pelvis'],
  [/costal cartilage|rib$/i, side => `ribs_${side}`],
  [/^(body of sternum|manubrium of sternum|xiphoid process)$/i, () => 'sternum'],
  [/^(clavicle)$/i, side => `clavicle_${side}`],
  [/^(scapula)$/i, side => `scapula_${side}`],
  [/^(humerus)$/i, side => `humerus_${side}`],
  [/^(radius|ulna)$/i, side => `forearm_${side}`],
  [/^(femur)$/i, side => `femur_${side}`],
  [/^(patella)$/i, side => `patella_${side}`],
  [/^(tibia|fibula)$/i, side => `lower_leg_${side}`],
  [/metacarpal|phalanx of .* finger of hand|^(capitate|hamate|lunate|pisiform|scaphoid|trapezium|trapezoid|triquetrum) bone$/i, side => `hand_${side}`],
  [/metatarsal|phalanx of .* finger of foot|sesamoid bones of foot|^(calcaneus|talus|navicular bone|cuboid bone|intermediate cuneiform bone|lateral cuneiform bone|medial cuneiform bone)$/i, side => `foot_${side}`],
  // Laryngeal skeleton and hyoid ride with the neck.
  [/^(hyoid bone|thyroid cartilage|cricoid cartilage|arytenoid cartilage|corniculate cartilage)$/i, () => 'spine_cervical'],
];

function classify(rawName) {
  const side = /\.l$/.test(rawName) ? 'l' : /\.r$/.test(rawName) ? 'r' : null;
  const base = rawName.replace(/\.(l|r)$/, '');
  for (const [pattern, resolve] of RULES) {
    if (pattern.test(base)) {
      const part = resolve(side);
      if (!part.endsWith('_null')) return { part, side, base };
    }
  }
  return { part: null, side, base };
}

// ---------------------------------------------------------------- geometry
function weld(points, indices) {
  const lookup = new Map();
  const remap = new Uint32Array(points.length / 3);
  const out = [];
  for (let i = 0; i < points.length / 3; ++i) {
    const key = `${points[i * 3].toFixed(5)},${points[i * 3 + 1].toFixed(5)},${points[i * 3 + 2].toFixed(5)}`;
    let target = lookup.get(key);
    if (target === undefined) {
      target = out.length / 3;
      lookup.set(key, target);
      out.push(points[i * 3], points[i * 3 + 1], points[i * 3 + 2]);
    }
    remap[i] = target;
  }
  return { points: Float64Array.from(out), indices: indices.map(index => remap[index]) };
}

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

// A shell may never fall below 40 triangles. At 12 a rib, a phalanx or a tooth
// stops being a surface and becomes a faceted shard that reads as debris
// floating next to the body, which is worse than spending the triangles.
const FLOOR_TRIANGLES = 40;

function simplifyShell(points, indices, target) {
  const current = indices.length / 3;
  const wanted = Math.max(FLOOR_TRIANGLES, Math.min(current, target));
  if (current <= wanted) return indices;
  const [simplified] = MeshoptSimplifier.simplify(Uint32Array.from(indices), Float32Array.from(points), 3,
    wanted * 3, 1e-3, []);
  return simplified.length >= FLOOR_TRIANGLES ? Array.from(simplified) : indices;
}

function decimate(points, indices, budget) {
  let groups = shells(Array.from(indices)).map(group => ({ group, indices: group }));
  const total = () => groups.reduce((sum, shell) => sum + shell.indices.length / 3, 0);
  for (let pass = 0; pass < 4 && total() > budget; ++pass) {
    const factor = budget / total();
    groups = groups.map(shell => {
      const current = shell.indices.length / 3;
      if (current <= FLOOR_TRIANGLES) return shell;
      const target = Math.max(FLOOR_TRIANGLES, Math.floor(current * factor));
      return { group: shell.group, indices: simplifyShell(points, shell.group, target) };
    });
  }
  return groups.flatMap(shell => shell.indices);
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
  return { points: Float64Array.from(kept), indices: remapped };
}

function mirrorX(points, indices) {
  const out = Float64Array.from(points);
  for (let i = 0; i < out.length; i += 3) out[i] = -out[i];
  const flipped = [];
  for (let i = 0; i < indices.length; i += 3) flipped.push(indices[i], indices[i + 2], indices[i + 1]);
  return { points: out, indices: flipped };
}

// ---------------------------------------------------------------- main
const geometry = load(SOURCE);
const bones = new Map();
const rejected = [];
for (const [name, mesh] of geometry) {
  if (NOT_BONE.test(name) || NOT_ANATOMY.test(name)) { rejected.push(name); continue; }
  const { part, side, base } = classify(name);
  if (!part) { rejected.push(name); continue; }
  bones.set(name, { name, base, side, part, ...mesh });
}

// Z-Anatomy stores the left side of every paired bone and no right twin, so the
// right side is a mirror of the left. Where a right twin already exists (the
// costal cartilages and the skull plates) the real one is kept.
let mirrored = 0;
for (const [name, bone] of [...bones]) {
  if (bone.side !== 'l') continue;
  const twin = name.replace(/\.l$/, '.r');
  if (bones.has(twin)) continue;
  const { points, indices } = mirrorX(bone.points, bone.indices);
  bones.set(twin, { name: twin, base: bone.base, side: 'r', part: bone.part.replace(/_l$/, '_r'), points, indices });
  ++mirrored;
}

const parts = new Map();
for (const bone of bones.values()) {
  if (!parts.has(bone.part)) parts.set(bone.part, []);
  parts.get(bone.part).push(bone);
}

// Lowest bone in the source frame sets y = 0; everything else is measured from
// it, so the stance height is the source height and nothing is stretched.
let floor = Infinity;
for (const bone of bones.values()) for (let i = 1; i < bone.points.length; i += 3) floor = Math.min(floor, bone.points[i]);

const output = [];
const partReport = [];
const boneReport = [];
for (const [part, list] of [...parts].sort((a, b) => a[0].localeCompare(b[0]))) {
  const points = [];
  const indices = [];
  for (const bone of list) {
    const offset = points.length / 3;
    for (let i = 0; i < bone.points.length; i += 3) {
      points.push(bone.points[i] * 0.01, (bone.points[i + 1] - floor) * 0.01, bone.points[i + 2] * 0.01);
    }
    for (const index of bone.indices) indices.push(index + offset);
    let low = [Infinity, Infinity, Infinity], high = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < bone.points.length; i += 3) for (let k = 0; k < 3; ++k) {
      low[k] = Math.min(low[k], bone.points[i + k]); high[k] = Math.max(high[k], bone.points[i + k]);
    }
    boneReport.push({
      name: bone.name, part, side: bone.side, triangles: bone.indices.length / 3,
      centre: low.map((value, k) => ((value + high[k]) / 2 - (k === 1 ? floor : 0)) * 0.01),
      extent: low.map((value, k) => (high[k] - value) * 0.01),
    });
  }
  const before = indices.length / 3;
  const welded = weld(points, Uint32Array.from(indices));
  const decimated = decimate(welded.points, welded.indices, (BUDGET[part] || 200) * DETAIL);
  const final = compact(welded.points, decimated);
  output.push({ name: part, ...final });
  partReport.push({ name: part, bones: list.length, sourceTriangles: before, triangles: final.indices.length / 3 });
}

// ---------------------------------------------------------------- write
const lines = [
  '# Low-poly human skeleton, one bone object per Z-Anatomy skeleton piece.',
  '# Source: Z-Anatomy (CC BY-SA 4.0), derived from BodyParts3D (CC BY-SA 2.1 Japan).',
  '# Units: metres, lowest bone on y = 0, facing +z. See tools/skeleton-source/ATTRIBUTION.md.',
];
let running = 1;
for (const part of output) {
  lines.push(`o ${part.name}`);
  for (let i = 0; i < part.points.length; i += 3) {
    lines.push(`v ${part.points[i].toFixed(5)} ${part.points[i + 1].toFixed(5)} ${part.points[i + 2].toFixed(5)}`);
  }
  for (let i = 0; i < part.indices.length; i += 3) {
    lines.push(`f ${running + part.indices[i]} ${running + part.indices[i + 1]} ${running + part.indices[i + 2]}`);
  }
  running += part.points.length / 3;
}

const target = process.argv[3] || join(HERE, 'skeleton-male.obj');
writeFileSync(target, lines.join('\n') + '\n');

let low = [Infinity, Infinity, Infinity], high = [-Infinity, -Infinity, -Infinity];
for (const part of output) {
  for (let i = 0; i < part.points.length; i += 3) for (let k = 0; k < 3; ++k) {
    low[k] = Math.min(low[k], part.points[i + k]); high[k] = Math.max(high[k], part.points[i + k]);
  }
}
const vertices = output.reduce((sum, part) => sum + part.points.length / 3, 0);
const triangles = output.reduce((sum, part) => sum + part.indices.length / 3, 0);

writeFileSync(target.replace(/\.obj$/, '.parts.json'), JSON.stringify({
  source: 'Z-Anatomy skeletal system (CC BY-SA 4.0), derived from BodyParts3D (CC BY-SA 2.1 Japan)',
  units: 'metres, lowest bone on y = 0, facing +z',
  sourceObjects: bones.size,
  mirroredObjects: mirrored,
  vertices,
  triangles,
  height: high[1] - low[1],
  bbox: { min: low, max: high },
  parts: partReport,
  bones: boneReport.sort((a, b) => a.name.localeCompare(b.name)),
}, null, 1) + '\n');

console.log(`bones kept      ${bones.size} (${mirrored} mirrored from the left side)`);
console.log(`rejected meshes ${rejected.length}`);
console.log(`parts           ${partReport.length}`);
console.log(`triangles       ${triangles}   vertices ${vertices}`);
console.log(`height          ${(high[1] - low[1]).toFixed(4)} m   bbox ${(high[0] - low[0]).toFixed(3)} x ${(high[1] - low[1]).toFixed(3)} x ${(high[2] - low[2]).toFixed(3)} m`);
console.log(`written         ${target}`);
