// Measures the Z-Anatomy skeletal model so its anatomy can be checked against
// the osteometric literature before it is trusted as a rig base.
//
// Z-Anatomy ships as binary FBX with per-object transforms in Properties70,
// including a negative uniform scale, so the geometry has to be assembled
// through the node hierarchy before it means anything.
//
// Usage:
//   node tools/skeleton-source/measure-zanatomy.mjs "/path/to/SkeletalSystem100.fbx"

import { readFbx, findNodes } from './fbx.mjs';


const clean = value => String(value).split('\u0000')[0].replace(/^[A-Za-z]+::/, '');
const number = value => (typeof value === 'number' ? value : parseFloat(value));

function properties(model) {
  const out = {};
  const group = model.children.find(child => child.name === 'Properties70');
  if (!group) return out;
  for (const entry of group.children) {
    out[clean(entry.properties[0])] = entry.properties.slice(4).map(number);
  }
  return out;
}

// FBX euler order XYZ, scaling applied per axis, translation last.
function localMatrix(translation, rotationDegrees, scaling) {
  const [rx, ry, rz] = rotationDegrees.map(d => (d * Math.PI) / 180);
  const cx = Math.cos(rx), sx = Math.sin(rx);
  const cy = Math.cos(ry), sy = Math.sin(ry);
  const cz = Math.cos(rz), sz = Math.sin(rz);
  const r = [
    [cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
    [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
    [-sy, cy * sx, cy * cx],
  ];
  return [
    [r[0][0] * scaling[0], r[0][1] * scaling[1], r[0][2] * scaling[2], translation[0]],
    [r[1][0] * scaling[0], r[1][1] * scaling[1], r[1][2] * scaling[2], translation[1]],
    [r[2][0] * scaling[0], r[2][1] * scaling[1], r[2][2] * scaling[2], translation[2]],
    [0, 0, 0, 1],
  ];
}

function multiply(a, b) {
  const out = [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]];
  for (let i = 0; i < 4; ++i) for (let j = 0; j < 4; ++j) {
    let sum = 0;
    for (let k = 0; k < 4; ++k) sum += a[i][k] * b[k][j];
    out[i][j] = sum;
  }
  return out;
}

const apply = (m, x, y, z) => [
  m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3],
  m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3],
  m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3],
];

// Fan-triangulates an FBX polygon stream: every negative index marks the last
// vertex of the polygon and is stored as the complement of the real index.
function triangulate(polygonIndices) {
  const out = [];
  let polygon = [];
  for (let i = 0; i < polygonIndices.length; ++i) {
    const raw = polygonIndices[i];
    if (raw < 0) {
      polygon.push(~raw);
      for (let k = 1; k + 1 < polygon.length; ++k) out.push(polygon[0], polygon[k], polygon[k + 1]);
      polygon = [];
    } else {
      polygon.push(raw);
    }
  }
  return Uint32Array.from(out);
}

// Returns every named mesh in the file with its transforms baked, as
// { points, indices }. Meshes that share a name are concatenated.
export function load(pathname) {
  const { roots } = readFbx(pathname);
  const models = new Map();
  for (const model of findNodes(roots, 'Model')) {
    models.set(model.properties[0], {
      name: clean(model.properties[1]), properties: properties(model), parent: -1,
    });
  }
  const parentOf = new Map();
  for (const group of findNodes(roots, 'Connections')) {
    for (const entry of group.children) {
      if (entry.name !== 'C' || entry.properties.length < 3) continue;
      parentOf.set(entry.properties[1], entry.properties[2]);
    }
  }
  for (const [id, model] of models) {
    const parent = parentOf.get(id);
    if (parent !== undefined && models.has(parent)) model.parent = parent;
  }
  const world = new Map();
  const resolve = id => {
    if (world.has(id)) return world.get(id);
    const model = models.get(id);
    const local = localMatrix(model.properties['Lcl Translation'] || [0, 0, 0],
      model.properties['Lcl Rotation'] || [0, 0, 0],
      model.properties['Lcl Scaling'] || [1, 1, 1]);
    const matrix = model.parent >= 0 ? multiply(resolve(model.parent), local) : local;
    world.set(id, matrix);
    return matrix;
  };
  for (const id of models.keys()) resolve(id);

  const geometry = new Map();
  for (const record of findNodes(roots, 'Geometry')) {
    const vertices = record.children.find(child => child.name === 'Vertices');
    const polygons = record.children.find(child => child.name === 'PolygonVertexIndex');
    if (!vertices || !polygons) continue;
    const owner = parentOf.get(record.properties[0]);
    if (owner === undefined || !models.has(owner)) continue;
    const name = models.get(owner).name;
    if (!name) continue;
    const matrix = resolve(owner);
    const source = vertices.properties[0];
    const points = new Float64Array(source.length);
    for (let i = 0; i < source.length; i += 3) {
      const [x, y, z] = apply(matrix, source[i], source[i + 1], source[i + 2]);
      points[i] = x; points[i + 1] = y; points[i + 2] = z;
    }
    const indices = triangulate(polygons.properties[0]);
    const existing = geometry.get(name);
    if (existing) {
      const offset = existing.points.length / 3;
      const merged = new Float64Array(existing.points.length + points.length);
      merged.set(existing.points); merged.set(points, existing.points.length);
      const mergedIndices = new Uint32Array(existing.indices.length + indices.length);
      mergedIndices.set(existing.indices);
      for (let i = 0; i < indices.length; ++i) mergedIndices[existing.indices.length + i] = indices[i] + offset;
      existing.points = merged;
      existing.indices = mergedIndices;
    } else {
      geometry.set(name, { points, indices });
    }
  }
  return geometry;
}

function bounds(points) {
  const low = [Infinity, Infinity, Infinity], high = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < points.length; i += 3) for (let k = 0; k < 3; ++k) {
    low[k] = Math.min(low[k], points[i + k]);
    high[k] = Math.max(high[k], points[i + k]);
  }
  return { low, high };
}

// Length along the bone's own principal axis: a femur in anatomical stance leans,
// so a bounding box underestimates it.
function boneLength(points) {
  const count = points.length / 3;
  const centre = [0, 0, 0];
  for (let i = 0; i < points.length; i += 3) for (let k = 0; k < 3; ++k) centre[k] += points[i + k] / count;
  const covariance = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
  for (let i = 0; i < points.length; i += 3) {
    const d = [points[i] - centre[0], points[i + 1] - centre[1], points[i + 2] - centre[2]];
    for (let a = 0; a < 3; ++a) for (let b = 0; b < 3; ++b) covariance[a][b] += d[a] * d[b] / count;
  }
  let axis = [1, 0, 0];
  for (let iteration = 0; iteration < 300; ++iteration) {
    const next = [0, 0, 0];
    for (let a = 0; a < 3; ++a) for (let b = 0; b < 3; ++b) next[a] += covariance[a][b] * axis[b];
    const length_ = Math.hypot(...next);
    if (!length_) break;
    axis = next.map(value => value / length_);
  }
  let low = Infinity, high = -Infinity;
  for (let i = 0; i < points.length; i += 3) {
    const t = (points[i] - centre[0]) * axis[0] + (points[i + 1] - centre[1]) * axis[1] +
      (points[i + 2] - centre[2]) * axis[2];
    low = Math.min(low, t);
    high = Math.max(high, t);
  }
  return high - low;
}

// The atlas also carries cross-section helpers, muscles and individual surface
// markings, so the stature is taken from bone landmarks rather than the raw
// bounding box of everything in the file.
const SKULL_TOP = /^(Parietal bone|Frontal bone)/;
const SOLE = /^(Calcaneus|Distal phalanx of .* foot)/;

export function measure(geometry) {
  const merged = new Map();
  for (const [name, mesh] of geometry) merged.set(name, mesh.points);
  let top = -Infinity, bottom = Infinity;
  for (const [name, points] of merged) {
    if (!SKULL_TOP.test(name)) continue;
    for (let i = 1; i < points.length; i += 3) top = Math.max(top, points[i]);
  }
  for (const [name, points] of merged) {
    if (!SOLE.test(name)) continue;
    for (let i = 1; i < points.length; i += 3) bottom = Math.min(bottom, points[i]);
  }
  const stature = (top - bottom) / 100; // file units are centimetres
  return { merged, stature, top, bottom };
}

const invokedDirectly = process.argv[1] && /measure-zanatomy\.mjs$/.test(process.argv[1]);
if (invokedDirectly) {
  const path = process.argv[2];
  if (!path) {
    console.error('usage: node measure-zanatomy.mjs <SkeletalSystem100.fbx>');
    process.exit(2);
  }
  const geometry = load(path);
  const { merged, stature, top, bottom } = measure(geometry);
  console.log(`geometries ${geometry.size}, bones ${merged.size}`);
  console.log(`crown ${top.toFixed(2)} cm, sole ${bottom.toFixed(2)} cm, stature ${(stature * 100).toFixed(2)} cm`);

  const bones = {
    'Femur.l': 'femur', 'Tibia.l': 'tibia', 'Fibula.l': 'fibula', 'Humerus.l': 'humerus',
    'Radius.l': 'radius', 'Ulna.l': 'ulna', 'Hip bone.l': 'hip bone', 'Scapula.l': 'scapula',
    'Clavicle.l': 'clavicle', 'Patella.l': 'patella',
  };
  console.log('\nbone             length   % of stature   reference');
  const reference = {
    femur: '26.0-27.5', tibia: '21.5-23.5', humerus: '17.5-18.5', radius: '13.5-15.0', ulna: '14.5-15.5',
  };
  for (const [name, label] of Object.entries(bones)) {
    const points = merged.get(name);
    if (!points) { console.log(`${label.padEnd(14)} ABSENT`); continue; }
    const length_ = boneLength(points) / 100;
    console.log(`${label.padEnd(14)} ${(length_ * 100).toFixed(2).padStart(6)} cm ${((length_ / stature) * 100).toFixed(2).padStart(11)}%   ${reference[label] || ''}`);
  }

  console.log('\nbilateral symmetry (mirror across x = 0):');
  for (const base of ['Femur', 'Tibia', 'Humerus', 'Radius', 'Ulna', 'Patella', 'Scapula', 'Clavicle', 'Hip bone']) {
    const left = merged.get(`${base}.l`), right = merged.get(`${base}.r`);
    if (!left || !right) continue;
    const a = bounds(left), b = bounds(right);
    const error = Math.max(Math.abs(-a.high[0] - b.low[0]), Math.abs(-a.low[0] - b.high[0])) * 10; // mm
    console.log(`  ${base.padEnd(10)} ${error.toFixed(1)} mm`);
  }
}
