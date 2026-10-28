// Reads the shipped skeleton OBJ and checks it against human osteometry.
//
// This is deliberately independent of the source data: it parses only the file
// the game loads, so it answers "is the thing we actually ship anatomically
// right", not "does the exporter think it copied correctly". No dependencies.
//
// Usage:
//   node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-male.obj
// Exits 0 when every check passes, 1 when a check fails, 2 on bad input.

import { readFileSync } from 'node:fs';

// Adult reference proportions as a fraction of standing height, from the
// Trotter & Gleser skeletal series (mean +/- two standard deviations for adult
// males). Long bone ratios are maximum length over stature.
//
// A body outside these bands is not automatically broken: they are population
// limits, and one real individual can sit outside them. What they catch is a
// *deformation* -- a limb stretched or squeezed by a transform -- so a miss of
// more than ten millimetres is treated as a fault and a smaller one is
// reported and tolerated.
const REFERENCE = [
  { part: 'femur_l', label: 'femur (left)', min: 25.5, max: 27.5 },
  { part: 'femur_r', label: 'femur (right)', min: 25.5, max: 27.5 },
  // The shipped part is tibia plus fibula, and the fibula is a little longer
  // than the tibia, so the window opens upward.
  { part: 'lower_leg_l', label: 'tibia+fibula (left)', min: 21.0, max: 24.2 },
  { part: 'lower_leg_r', label: 'tibia+fibula (right)', min: 21.0, max: 24.2 },
  { part: 'humerus_l', label: 'humerus (left)', min: 17.6, max: 19.6 },
  { part: 'humerus_r', label: 'humerus (right)', min: 17.6, max: 19.6 },
  // Forearm part is radius plus ulna; the ulna sets the longer dimension.
  { part: 'forearm_l', label: 'radius+ulna (left)', min: 14.2, max: 16.2 },
  { part: 'forearm_r', label: 'radius+ulna (right)', min: 14.2, max: 16.2 },
  { part: 'hand_l', label: 'hand (left)', min: 10.0, max: 11.8 },
  { part: 'hand_r', label: 'hand (right)', min: 10.0, max: 11.8 },
  { part: 'foot_l', label: 'foot length (left)', min: 14.0, max: 16.2 },
  { part: 'foot_r', label: 'foot length (right)', min: 14.0, max: 16.2 },
];

// A proportion this far outside its band is read as a deformation rather than
// as one person's build.
const SEVERE_MISS_MM = 10;

// Paired parts must sit at mirrored positions; a bad export or a warp shows up
// here before it shows up in the silhouette.
const MIRROR_PAIRS = [
  ['femur_l', 'femur_r'], ['lower_leg_l', 'lower_leg_r'], ['humerus_l', 'humerus_r'],
  ['forearm_l', 'forearm_r'], ['hand_l', 'hand_r'], ['foot_l', 'foot_r'],
  ['scapula_l', 'scapula_r'], ['clavicle_l', 'clavicle_r'], ['patella_l', 'patella_r'],
  ['ribs_l', 'ribs_r'],
];

const MIRROR_TOLERANCE_MM = 3;

export function readObj(path) {
  const groups = new Map();
  const points = [];
  let current = [];
  for (const line of readFileSync(path, 'utf8').split('\n')) {
    if (line.startsWith('v ')) {
      const [x, y, z] = line.slice(2).trim().split(/\s+/).map(Number);
      points.push([x, y, z]);
    } else if (line.startsWith('o ')) {
      const name = line.slice(2).trim();
      if (!groups.has(name)) groups.set(name, { name, faceCount: 0, vertexIndex: new Set() });
      current = groups.get(name);
    } else if (line.startsWith('f ') && current) {
      const indices = line.slice(2).trim().split(/\s+/).map(token => Number(token.split('/')[0]) - 1);
      for (const index of indices) current.vertexIndex.add(index);
      current.faceCount += 1;
    }
  }
  const parts = new Map();
  for (const [name, group] of groups) {
    const list = [];
    for (const index of group.vertexIndex) list.push(points[index]);
    let low = [Infinity, Infinity, Infinity], high = [-Infinity, -Infinity, -Infinity];
    for (const point of list) for (let k = 0; k < 3; ++k) {
      low[k] = Math.min(low[k], point[k]);
      high[k] = Math.max(high[k], point[k]);
    }
    parts.set(name, { name, points: list, triangles: group.faceCount, low, high });
  }
  return { points, parts };
}

// Longest extent along the part's own principal axis. A femur stands at an
// angle, so its bounding-box height is shorter than the bone.
function principalLength(points) {
  const count = points.length;
  const centre = [0, 0, 0];
  for (const point of points) for (let k = 0; k < 3; ++k) centre[k] += point[k] / count;
  const covariance = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
  for (const point of points) {
    const d = [point[0] - centre[0], point[1] - centre[1], point[2] - centre[2]];
    for (let a = 0; a < 3; ++a) for (let b = 0; b < 3; ++b) covariance[a][b] += (d[a] * d[b]) / count;
  }
  let axis = [0, 1, 0];
  for (let iteration = 0; iteration < 256; ++iteration) {
    const next = [0, 0, 0];
    for (let a = 0; a < 3; ++a) for (let b = 0; b < 3; ++b) next[a] += covariance[a][b] * axis[b];
    const magnitude = Math.hypot(...next);
    if (!magnitude) break;
    axis = next.map(value => value / magnitude);
  }
  let low = Infinity, high = -Infinity;
  for (const point of points) {
    const t = (point[0] - centre[0]) * axis[0] + (point[1] - centre[1]) * axis[1] + (point[2] - centre[2]) * axis[2];
    low = Math.min(low, t);
    high = Math.max(high, t);
  }
  return high - low;
}

function spans(part) {
  return principalLength(part.points);
}

export function verify(path, { quiet = false } = {}) {
  const { points, parts } = readObj(path);
  let low = [Infinity, Infinity, Infinity], high = [-Infinity, -Infinity, -Infinity];
  for (const point of points) for (let k = 0; k < 3; ++k) {
    low[k] = Math.min(low[k], point[k]);
    high[k] = Math.max(high[k], point[k]);
  }
  const stature = high[1] - low[1];
  const rows = [];
  let failures = 0;

  for (const check of REFERENCE) {
    const part = parts.get(check.part);
    if (!part) { rows.push({ ...check, measured: null, ok: false, severe: true, deltaMm: null }); ++failures; continue; }
    const length = spans(part);
    const ratio = (length / stature) * 100;
    const ok = ratio >= check.min && ratio <= check.max;
    // How far, in millimetres of bone, this part sits outside its band.
    const deltaMm = ok ? 0 : (ratio - (ratio < check.min ? check.min : check.max)) / 100 * stature * 1000;
    const severe = Math.abs(deltaMm) > SEVERE_MISS_MM;
    if (severe) ++failures;
    rows.push({ ...check, measured: ratio, ok, severe, deltaMm });
  }

  const mirror = [];
  for (const [leftName, rightName] of MIRROR_PAIRS) {
    const left = parts.get(leftName), right = parts.get(rightName);
    if (!left || !right) { mirror.push({ pair: `${leftName}/${rightName}`, error: null }); ++failures; continue; }
    // Mirror the left part through x = 0 and compare its extent with the right.
    const error = Math.max(Math.abs(-left.low[0] - right.high[0]), Math.abs(-left.high[0] - right.low[0])) * 1000;
    const ok = error <= MIRROR_TOLERANCE_MM;
    if (!ok) ++failures;
    mirror.push({ pair: `${leftName}/${rightName}`, error, ok });
  }

  if (!quiet) {
    console.log(`file      ${path}`);
    console.log(`vertices  ${points.length}`);
    console.log(`triangles ${[...parts.values()].reduce((sum, part) => sum + part.triangles, 0)}`);
    console.log(`height    ${stature.toFixed(4)} m   feet at y = ${low[1].toFixed(4)} m`);
    console.log(`bbox      ${(high[0] - low[0]).toFixed(3)} x ${(high[1] - low[1]).toFixed(3)} x ${(high[2] - low[2]).toFixed(3)} m`);
    console.log('');
    console.log('part                 measured   expected %      verdict');
    for (const row of rows) {
      const measured = row.measured === null ? 'missing' : `${row.measured.toFixed(2)}%`;
      const delta = !row.deltaMm ? '' : `  (${row.deltaMm > 0 ? '+' : ''}${row.deltaMm.toFixed(1)} mm)`;
      const verdict = row.ok ? 'ok' : row.severe ? 'FAIL' : 'off';
      console.log(`${row.label.padEnd(22)} ${measured.padStart(8)}   ${`${row.min}-${row.max}`.padStart(11)}      ${verdict}${delta}`);
    }
    console.log('');
    console.log('left/right mirror error (mm)');
    for (const row of mirror) {
      const error = row.error === null ? 'part missing' : `${row.error.toFixed(1)} mm`;
      console.log(`  ${row.pair.padEnd(24)} ${error.padStart(12)}  ${row.ok ? 'ok' : 'FAIL'}`);
    }
  }

  return { stature, failures, rows, mirror, centres: parts };
}

if (process.argv[1] && /verify-obj\.mjs$/.test(process.argv[1])) {
  const target = process.argv[2];
  if (!target) {
    console.error('usage: node verify-obj.mjs <skeleton.obj>');
    process.exit(2);
  }
  const result = verify(target);
  console.log('');
  console.log(`skeleton verification: ${result.failures ? `${result.failures} FAILED` : 'PASSED'}`);
  process.exit(result.failures ? 1 : 0);
}
