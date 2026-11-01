// Checks that the pinned skeleton OBJs are faithful to the source: same size,
// same position, no vertex moved, and proportions that actually hold up against
// human reference values.
//
// Run against a checkout of open-anatomy-3d:
//   npm i @gltf-transform/core @gltf-transform/extensions meshoptimizer
//   OPEN_ANATOMY=/path/to/open-anatomy-3d node tools/skeleton-source/verify.mjs

import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { MeshoptDecoder } from 'meshoptimizer';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const OPEN_ANATOMY = process.env.OPEN_ANATOMY || '';
const HERE = dirname(fileURLToPath(import.meta.url));
const CATALOG = join(OPEN_ANATOMY, 'packages/open-anatomy-3d/catalog.json');

const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({ 'meshopt.decoder': MeshoptDecoder });
const catalog = JSON.parse(readFileSync(CATALOG, 'utf8'));
const wanted = new Set(catalog.filter(entry => entry.system === 'skeleton').map(entry => entry.key));

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
  return chain.reduce((outer, node) => {
    const inner = localMatrix(node);
    const out = new Array(16).fill(0);
    for (let c = 0; c < 4; ++c) for (let r = 0; r < 4; ++r) {
      let sum = 0;
      for (let k = 0; k < 4; ++k) sum += outer[k * 4 + r] * inner[c * 4 + k];
      out[c * 4 + r] = sum;
    }
    return out;
  }, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
}

const apply = (m, p) => [
  m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
  m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
  m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14],
];

function bounds(points) {
  const low = [Infinity, Infinity, Infinity], high = [-Infinity, -Infinity, -Infinity];
  for (const point of points) for (let k = 0; k < 3; ++k) {
    low[k] = Math.min(low[k], point[k]);
    high[k] = Math.max(high[k], point[k]);
  }
  return { low, high };
}

// Length along a bone's own principal axis. A bounding box under-reports a bone
// that lies at an angle (a femur in anatomical stance leans), so the axis is
// found from the covariance rather than assumed to be vertical.
function boneLength(points) {
  const centre = [0, 0, 0];
  for (const point of points) for (let k = 0; k < 3; ++k) centre[k] += point[k] / points.length;
  const covariance = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
  for (const point of points) {
    const d = [point[0] - centre[0], point[1] - centre[1], point[2] - centre[2]];
    for (let i = 0; i < 3; ++i) for (let j = 0; j < 3; ++j) covariance[i][j] += d[i] * d[j] / points.length;
  }
  let axis = [1, 0, 0];
  for (let iteration = 0; iteration < 200; ++iteration) {
    const next = [0, 0, 0];
    for (let i = 0; i < 3; ++i) for (let j = 0; j < 3; ++j) next[i] += covariance[i][j] * axis[j];
    const length_ = Math.hypot(...next);
    if (!length_) break;
    axis = next.map(value => value / length_);
  }
  let low = Infinity, high = -Infinity;
  for (const point of points) {
    const t = (point[0] - centre[0]) * axis[0] + (point[1] - centre[1]) * axis[1] + (point[2] - centre[2]) * axis[2];
    low = Math.min(low, t);
    high = Math.max(high, t);
  }
  return high - low;
}

function readObj(path) {
  const parts = new Map();
  const points = [];
  let group = null;
  for (const line of readFileSync(path, 'utf8').split('\n')) {
    if (line.startsWith('o ')) {
      group = line.slice(2).trim();
      parts.set(group, []);
    } else if (line.startsWith('v ')) {
      const point = line.slice(2).split(' ').map(Number);
      points.push(point);
      if (group) parts.get(group).push(point);
    }
  }
  return { parts, points };
}

// Reference proportions for adult long bones, as a fraction of stature. Ranges
// rather than points, because these vary with sex, ancestry and method.
const REFERENCE = {
  femur: [26.0, 27.5], tibia: [21.5, 23.5], humerus: [17.5, 18.5],
  forearm: [14.5, 15.5], hand: [10.5, 11.5],
};

let failures = 0;
const check = (ok, message) => {
  if (!ok) ++failures;
  console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${message}`);
};

for (const sex of ['male', 'female']) {
  console.log(`\n=== ${sex}`);
  const doc = await io.read(join(OPEN_ANATOMY, `packages/open-anatomy-3d/models/anatomy-${sex}.glb`));
  const source = new Map();
  for (const node of doc.getRoot().listNodes()) {
    const name = node.getName();
    const mesh = node.getMesh();
    if (!mesh || !wanted.has(name)) continue;
    const matrix = worldMatrix(node);
    const points = [];
    for (const primitive of mesh.listPrimitives()) {
      const position = primitive.getAttribute('POSITION');
      for (let i = 0; i < position.getCount(); ++i) points.push(apply(matrix, position.getElement(i, [])));
    }
    source.set(name, points);
  }
  const { parts, points } = readObj(join(HERE, `skeleton-${sex}.obj`));
  const allSource = [...source.values()].flat();
  const sourceBox = bounds(allSource);
  const objBox = bounds(points);

  const sourceHeight = sourceBox.high[1] - sourceBox.low[1];
  const objHeight = objBox.high[1] - objBox.low[1];
  console.log(`  source ${(sourceHeight * 100).toFixed(3)} cm, OBJ ${(objHeight * 100).toFixed(3)} cm`);
  check(Math.abs(sourceHeight - objHeight) < 0.005, 'skeletal height within 5 mm of the source');
  check(Math.abs(sourceBox.low[1] - objBox.low[1]) < 0.005, 'feet on the same ground plane as the source');
  check(Math.abs(sourceBox.high[1] - objBox.high[1]) < 0.005, 'crown at the same height as the source');

  // Decimation may delete vertices, but it must never move one.
  const key = point => point.map(value => Math.round(value * 1e5) / 1e5).join(',');
  const sourceKeys = new Set(allSource.map(key));
  const moved = points.filter(point => !sourceKeys.has(key(point))).length;
  check(moved === 0, `all ${points.length} OBJ vertices exist verbatim in the source (${moved} moved)`);

  console.log('  proportions against adult reference ranges:');
  const stature = sourceHeight;
  for (const [bone, range] of Object.entries(REFERENCE)) {
    const length_ = boneLength(source.get(`${bone === 'tibia' ? 'lower_leg' : bone}_l`)) / stature * 100;
    const ok = length_ >= range[0] - 0.8 && length_ <= range[1] + 1.2;
    check(ok, `${bone.padEnd(8)} ${length_.toFixed(2)}% of stature (reference ${range[0]}-${range[1]}%)`);
  }

  console.log('  bilateral symmetry of the source:');
  for (const bone of ['femur', 'humerus', 'forearm', 'hand', 'foot', 'lower_leg', 'patella', 'clavicle', 'scapula']) {
    const left = bounds(source.get(`${bone}_l`)), right = bounds(source.get(`${bone}_r`));
    const error = Math.max(Math.abs(-left.high[0] - right.low[0]), Math.abs(-left.low[0] - right.high[0])) * 1000;
    check(error < 3.0, `${bone.padEnd(9)} mirrors within 3 mm (${error.toFixed(1)} mm)`);
  }
  console.log(`  parts: ${parts.size}`);
}

console.log(failures === 0 ? '\nskeleton verification: PASS' : `\nskeleton verification: ${failures} FAILED`);
process.exit(failures === 0 ? 0 : 1);
