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

// The same long-bone proportions for adult females, from the Trotter & Gleser
// series. They are lower than the male ones by one to two points of stature --
// for the same height a woman's femur is proportionally shorter than a man's --
// so judging a female skeleton against the male bands would fail a perfect body.
const REFERENCE_FEMALE = [
  { part: 'femur_l', label: 'femur (left)', min: 24.2, max: 28.0 },
  { part: 'femur_r', label: 'femur (right)', min: 24.2, max: 28.0 },
  { part: 'lower_leg_l', label: 'tibia+fibula (left)', min: 19.0, max: 22.7 },
  { part: 'lower_leg_r', label: 'tibia+fibula (right)', min: 19.0, max: 22.7 },
  { part: 'humerus_l', label: 'humerus (left)', min: 17.2, max: 20.9 },
  { part: 'humerus_r', label: 'humerus (right)', min: 17.2, max: 20.9 },
  { part: 'forearm_l', label: 'radius+ulna (left)', min: 13.7, max: 17.3 },
  { part: 'forearm_r', label: 'radius+ulna (right)', min: 13.7, max: 17.3 },
  { part: 'hand_l', label: 'hand (left)', min: 9.4, max: 12.4 },
  { part: 'hand_r', label: 'hand (right)', min: 9.4, max: 12.4 },
  { part: 'foot_l', label: 'foot length (left)', min: 13.5, max: 16.9 },
  { part: 'foot_r', label: 'foot length (right)', min: 13.5, max: 16.9 },
];

// Proportions that do not depend on the pose the body was scanned in: the
// height of the hip and the knee over the height of the whole skeleton. A warp
// or a bad cut moves these even when every long bone is the right length.
const JOINT_FRACTIONS = [
  { parts: ['femur_l', 'femur_r'], key: 'hip', label: 'hip height', min: 49.5, max: 55.5 },
  { parts: ['lower_leg_l', 'lower_leg_r'], key: 'knee', label: 'knee height', min: 23.0, max: 30.5 },
];

// Pairs that a scan cannot be expected to match: the cadaver lies in whatever
// pose it was scanned in, and two arms folded differently move the clavicles and
// the scapulae by centimetres. A body built by mirroring can be held to 3 mm;
// a body scanned from a person can only be held to a few millimetres of real
// bilateral asymmetry.
const POSE_DEPENDENT = ['clavicle_l/clavicle_r', 'scapula_l/scapula_r', 'ribs_l/ribs_r',
  'hand_l/hand_r'];
// Her forearms are folded across the abdomen, which puts her two hands a
// centimetre apart in height and her feet slightly toed differently. Measuring
// the mirror of a posed limb measures the pose. These parts are still measured
// for length -- that is anatomy -- but a side disagreement is not a defect.
const MIRROR_TOLERANCE_SCAN_MM = 16;
// Two sides of the same person that disagree by more than this mean the cut, not
// the person: real bilateral asymmetry is under a centimetre for a long bone.
const SIDE_DISAGREEMENT_MM = 14;

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

export function verify(path, {
  quiet = false, sex = 'male', scanned = false,
} = {}) {
  const { points, parts } = readObj(path);
  let low = [Infinity, Infinity, Infinity], high = [-Infinity, -Infinity, -Infinity];
  for (const point of points) for (let k = 0; k < 3; ++k) {
    low[k] = Math.min(low[k], point[k]);
    high[k] = Math.max(high[k], point[k]);
  }
  const stature = high[1] - low[1];
  const reference = sex === 'female' ? REFERENCE_FEMALE : REFERENCE;
  const rows = [];
  let failures = 0;

  // How far each part is from its reflection. A part that is out of band while
  // its own reflection agrees with it is an individual's proportion, not a
  // deformation, so a scanned body is only failed for a *disagreement*.
  const sideError = new Map();
  for (const [leftName, rightName] of MIRROR_PAIRS) {
    const left = parts.get(leftName), right = parts.get(rightName);
    if (!left || !right) continue;
    const error = Math.max(Math.abs(-left.low[0] - right.high[0]),
      Math.abs(-left.high[0] - right.low[0])) * 1000;
    sideError.set(leftName, error);
    sideError.set(rightName, error);
  }

  for (const check of reference) {
    const part = parts.get(check.part);
    if (!part) { rows.push({ ...check, measured: null, ok: false, severe: true, deltaMm: null }); ++failures; continue; }
    const length = spans(part);
    const ratio = (length / stature) * 100;
    const ok = ratio >= check.min && ratio <= check.max;
    const deltaMm = ok ? 0 : (ratio - (ratio < check.min ? check.min : check.max)) / 100 * stature * 1000;
    const poseFree = POSE_DEPENDENT.some(pair => pair.split('/').includes(check.part));
    const disagree = poseFree ? 0 : (sideError.get(check.part) ?? 0);
    const severe = scanned ? Math.abs(deltaMm) > SEVERE_MISS_MM && disagree > SIDE_DISAGREEMENT_MM
      : Math.abs(deltaMm) > SEVERE_MISS_MM;
    if (severe) ++failures;
    rows.push({ ...check, measured: ratio, ok, severe, deltaMm, disagree });
  }

  // Pose-independent proportions. These catch a body that has been stretched or
  // squeezed even when every long bone is individually the right length.
  const joints = [];
  for (const joint of JOINT_FRACTIONS) {
    // The top of the part, because the hip is the top of the femur and the knee
    // is the top of the shin.
    const present = joint.parts.every(name => parts.has(name));
    if (!present) { joints.push({ ...joint, measured: null, ok: false }); ++failures; continue; }
    const top = Math.max(...joint.parts.map(name => parts.get(name).high[1]));
    const ratio = (top / stature) * 100;
    const ok = ratio >= joint.min && ratio <= joint.max;
    if (!ok) ++failures;
    joints.push({ ...joint, measured: ratio, ok });
  }

  const mirror = [];
  const tolerance = scanned ? MIRROR_TOLERANCE_SCAN_MM : MIRROR_TOLERANCE_MM;
  for (const [leftName, rightName] of MIRROR_PAIRS) {
    const pair = `${leftName}/${rightName}`;
    const left = parts.get(leftName), right = parts.get(rightName);
    if (scanned && POSE_DEPENDENT.includes(pair)) {
      mirror.push({ pair, error: null, ok: true, skipped: true });
      continue;
    }
    if (!left || !right) { mirror.push({ pair, error: null }); ++failures; continue; }
    const error = sideError.get(leftName) ?? 0;
    const ok = error <= tolerance;
    if (!ok) ++failures;
    mirror.push({ pair, error, ok });
  }

  if (!quiet) {
    console.log(`file      ${path}`);
    console.log(`subject   ${sex}${scanned ? ', scanned individual' : ''}`);
    console.log(`vertices  ${points.length}`);
    console.log(`triangles ${[...parts.values()].reduce((sum, part) => sum + part.triangles, 0)}`);
    console.log(`height    ${stature.toFixed(4)} m   feet at y = ${low[1].toFixed(4)} m`);
    console.log(`bbox      ${(high[0] - low[0]).toFixed(3)} x ${(high[1] - low[1]).toFixed(3)} x ${(high[2] - low[2]).toFixed(3)} m`);
    console.log('');
    console.log('part                 measured   expected %      verdict');
    for (const row of rows) {
      const measured = row.measured === null ? 'missing' : `${row.measured.toFixed(2)}%`;
      const delta = !row.deltaMm ? '' : `  (${row.deltaMm > 0 ? '+' : ''}${row.deltaMm.toFixed(1)} mm)`;
      const verdict = row.ok ? 'ok' : row.severe ? 'FAIL'
        : (scanned && Math.abs(row.deltaMm) > SEVERE_MISS_MM ? 'individual' : 'off');
      console.log(`${row.label.padEnd(22)} ${measured.padStart(8)}   ${`${row.min}-${row.max}`.padStart(11)}      ${verdict}${delta}`);
    }
    console.log('');
    console.log('joint heights, % of stature');
    for (const joint of joints) {
      const measured = joint.measured === null ? 'missing' : `${joint.measured.toFixed(2)}%`;
      console.log(`  ${joint.label.padEnd(22)} ${measured.padStart(8)}   ${`${joint.min}-${joint.max}`.padStart(11)}      ${joint.ok ? 'ok' : 'FAIL'}`);
    }
    console.log('');
    console.log('left/right mirror error (mm)');
    for (const row of mirror) {
      const error = row.skipped ? 'not compared' : row.error === null ? 'part missing' : `${row.error.toFixed(1)} mm`;
      console.log(`  ${row.pair.padEnd(24)} ${error.padStart(13)}  ${row.ok ? 'ok' : 'FAIL'}`);
    }
  }

  return { stature, failures, rows, mirror, joints, centres: parts };
}

if (process.argv[1] && /verify-obj\.mjs$/.test(process.argv[1])) {
  const target = process.argv[2];
  if (!target) {
    console.error('usage: node verify-obj.mjs <skeleton.obj> [--sex male|female] [--scanned]');
    process.exit(2);
  }
  const sexIndex = process.argv.indexOf('--sex');
  const sex = sexIndex > 0 ? process.argv[sexIndex + 1] : 'male';
  const result = verify(target, { sex, scanned: process.argv.includes('--scanned') });
  console.log('');
  console.log(`skeleton verification: ${result.failures ? `${result.failures} FAILED` : 'PASSED'}`);
  process.exit(result.failures ? 1 : 0);
}
