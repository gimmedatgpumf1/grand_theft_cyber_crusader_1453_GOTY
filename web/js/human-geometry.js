import * as THREE from './vendor/three.module.min.js';
import { humanData } from './vendor/human-data.js';
import { hairData } from './vendor/human-hair-data.js';
import { MORPH_DEFS } from './morphs.js';

function subset(base, indices, groups = null) {
  const geometry = new THREE.BufferGeometry();
  for (const [name, attribute] of Object.entries(base.attributes)) geometry.setAttribute(name, attribute);
  geometry.setIndex(new THREE.BufferAttribute(indices, 1));
  if (groups) for (const group of groups) geometry.addGroup(group.start, group.count, group.materialIndex);
  return geometry;
}

function applyMorphs(data, positions, weights) {
  const eyes = { L: [...data.eyes.L], R: [...data.eyes.R] };
  for (let i = 0; i < MORPH_DEFS.length; ++i) {
    const weight = (weights[i] || 0) / 100;
    if (!weight) continue;
    const direction = weight > 0 ? 'positive' : 'negative';
    const sections = data.morphs[MORPH_DEFS[i].key][direction];
    if (!sections) continue;
    const indices = data[sections.index];
    const deltas = data[sections.delta];
    // MakeHuman direction targets already contain signed deltas; the slider supplies magnitude.
    const amount = Math.abs(weight) / data.morphQuant;
    for (let j = 0; j < indices.length; ++j) {
      const vertex = indices[j] * 3;
      const delta = j * 3;
      positions[vertex] += deltas[delta] * amount;
      positions[vertex + 1] += deltas[delta + 1] * amount;
      positions[vertex + 2] += deltas[delta + 2] * amount;
    }
    const eyeDeltas = data[sections.eyes];
    ['L', 'R'].forEach((side, sideIndex) => {
      for (let axis = 0; axis < 3; ++axis) eyes[side][axis] += eyeDeltas[sideIndex * 3 + axis] * amount;
    });
  }
  return eyes;
}

function morphedHeadBounds(data, positions) {
  const min = new THREE.Vector3(Infinity, Infinity, Infinity);
  const max = new THREE.Vector3(-Infinity, -Infinity, -Infinity);
  const visited = new Set();
  for (const index of data.headIndex) {
    if (visited.has(index)) continue;
    visited.add(index);
    const offset = index * 3;
    const rawY = positions[offset + 1] / data.scale - data.ground;
    if (rawY < data.regions.head - 0.25) continue;
    min.x = Math.min(min.x, positions[offset]);
    min.y = Math.min(min.y, positions[offset + 1]);
    min.z = Math.min(min.z, positions[offset + 2]);
    max.x = Math.max(max.x, positions[offset]);
    max.y = Math.max(max.y, positions[offset + 1]);
    max.z = Math.max(max.z, positions[offset + 2]);
  }
  return { min, max, center: min.clone().add(max).multiplyScalar(0.5) };
}

function hairGeometry(data, appearance, headBounds, eyes) {
  if (appearance.hair <= 0) return null;
  const pack = hairData(appearance.male);
  const style = pack.styles[appearance.hair];
  if (!style) return null;
  const morphed = new Float32Array(pack.basePosition);
  for (let i = 0; i < MORPH_DEFS.length; ++i) {
    const weight = (appearance.morphs[i] || 0) / 100;
    if (!weight) continue;
    const section = pack.morphs[MORPH_DEFS[i].key][weight > 0 ? 'positive' : 'negative'];
    if (!section) continue;
    const indices = pack[section.index];
    const deltas = pack[section.delta];
    const amount = Math.abs(weight) / pack.morphQuant; // Direction deltas are already signed.
    for (let j = 0; j < indices.length; ++j) {
      const vertex = indices[j] * 3;
      const delta = j * 3;
      morphed[vertex] += deltas[delta] * amount;
      morphed[vertex + 1] += deltas[delta + 1] * amount;
      morphed[vertex + 2] += deltas[delta + 2] * amount;
    }
  }
  const ratios = [1, 1, 1];
  for (let axis = 0; axis < style.scaleDefaults.length; ++axis) {
    const a = style.scaleRefs[axis * 2] * 3 + axis;
    const b = style.scaleRefs[axis * 2 + 1] * 3 + axis;
    ratios[axis] = Math.abs(morphed[a] - morphed[b]) / (style.scaleDefaults[axis] * pack.scale);
  }
  const refs = pack[`${style.prefix}_refs`];
  const bary = pack[`${style.prefix}_bary`];
  const offsets = pack[`${style.prefix}_offsets`];
  const count = refs.length / 3;
  const positions = new Float32Array(count * 3);
  const skinIndex = new Uint8Array(count * 4);
  const skinWeight = new Float32Array(count * 4);
  for (let vertex = 0; vertex < count; ++vertex) {
    const influences = new Map();
    for (let axis = 0; axis < 3; ++axis) {
      let value = offsets[vertex * 3 + axis] * ratios[axis] * pack.scale;
      for (let anchor = 0; anchor < 3; ++anchor) {
        value += morphed[refs[vertex * 3 + anchor] * 3 + axis] * bary[vertex * 3 + anchor];
      }
      positions[vertex * 3 + axis] = value;
    }
    if (style.receded || style.key === 'waves' || style.key === 'long-bangs') {
      const rawY = positions[vertex * 3 + 1] / pack.scale - pack.ground;
      const rawZ = positions[vertex * 3 + 2] / pack.scale;
      if (style.receded) {
        const front = THREE.MathUtils.clamp((rawZ - 0.30) / 1.1, 0, 1);
        const low = THREE.MathUtils.clamp((8.75 - rawY) / 1.3, 0, 1);
        positions[vertex * 3 + 1] += front * low * 0.12;
      } else {
        const front = THREE.MathUtils.clamp((rawZ - 0.45) / 0.85, 0, 1);
        const low = THREE.MathUtils.clamp((8.85 - rawY) / 1.25, 0, 1);
        positions[vertex * 3 + 1] += front * low * 0.045;
      }
    }
    for (let anchor = 0; anchor < 3; ++anchor) {
      const ref = refs[vertex * 3 + anchor];
      const amount = bary[vertex * 3 + anchor];
      for (let influence = 0; influence < 4; ++influence) {
        const bone = pack.baseSkinIndex[ref * 4 + influence];
        const weight = pack.baseSkinWeight[ref * 4 + influence] / 255 * amount;
        influences.set(bone, (influences.get(bone) || 0) + weight);
      }
    }
    const strongest = [...influences.entries()].filter(entry => entry[1] > 0)
      .sort((a, b) => b[1] - a[1]).slice(0, 4);
    const total = strongest.reduce((sum, entry) => sum + entry[1], 0) || 1;
    strongest.forEach((entry, index) => {
      skinIndex[vertex * 4 + index] = entry[0];
      skinWeight[vertex * 4 + index] = entry[1] / total;
    });
  }
  if (style.key === 'waves') {
    const center = headBounds.center;
    for (let vertex = 0; vertex < count; ++vertex) {
      const offset = vertex * 3;
      positions[offset] = center.x + (positions[offset] - center.x) * 0.74;
      positions[offset + 1] = center.y + (positions[offset + 1] - center.y) * 0.85;
      positions[offset + 2] = center.z + (positions[offset + 2] - center.z) * 0.74;
    }
  } else if (['short01', 'short02', 'short04'].includes(style.key)) {
    const top = headBounds.max.y - 0.035;
    const factor = style.key === 'short04' ? 1.15 : 0.48;
    for (let vertex = 0; vertex < count; ++vertex) {
      const offset = vertex * 3 + 1;
      if (positions[offset] > top) positions[offset] = top + (positions[offset] - top) * factor;
    }
  }
  if (style.key === 'wavy-bob' || style.key === 'curly-bob') {
    const center = headBounds.center;
    const factor = style.key === 'wavy-bob' ? 0.84 : 0.90;
    for (let vertex = 0; vertex < count; ++vertex) {
      const offset = vertex * 3;
      positions[offset] = center.x + (positions[offset] - center.x) * factor;
      positions[offset + 2] = center.z + (positions[offset + 2] - center.z) * factor;
    }
  }
  if (style.key === 'long-bangs') {
    const fringeTop = eyes.L[1] + 0.12;
    for (let vertex = 0; vertex < count; ++vertex) {
      const offset = vertex * 3;
      const x = positions[offset];
      const y = positions[offset + 1];
      const front = THREE.MathUtils.clamp((positions[offset + 2] - 0.11) / 0.05, 0, 1);
      const lower = THREE.MathUtils.clamp((fringeTop - y) / 0.10, 0, 1);
      const side = THREE.MathUtils.clamp((x + 0.09) / 0.18, 0, 1);
      const amount = front * lower;
      positions[offset] -= amount * 0.025;
      positions[offset + 1] += amount * side * 0.035;
      positions[offset + 2] -= amount * 0.008;
      const length = THREE.MathUtils.clamp((eyes.L[1] - 0.03 - y) / 0.22, 0, 1);
      const edge = THREE.MathUtils.clamp((Math.abs(x) - 0.055) / 0.045, 0, 1);
      const phase = y * 55 + (x < 0 ? 0.8 : 0);
      positions[offset] += Math.sign(x) * Math.sin(phase) * length * edge * 0.025;
      positions[offset + 2] += Math.cos(phase) * length * edge * 0.018;
    }
  }
  if (style.key === 'wavy-bob') {
    const top = headBounds.max.y + 0.005;
    for (let vertex = 0; vertex < count; ++vertex) {
      const offset = vertex * 3;
      if (positions[offset + 1] > top) positions[offset + 1] = top + (positions[offset + 1] - top) * 0.78;
      const lower = THREE.MathUtils.clamp((eyes.L[1] - positions[offset + 1] + 0.02) / 0.16, 0, 1);
      const front = THREE.MathUtils.clamp((positions[offset + 2] - eyes.L[2] * 0.65) / (eyes.L[2] * 0.5), 0, 1);
      const side = THREE.MathUtils.clamp((Math.abs(positions[offset]) - 0.035) / 0.07, 0, 1);
      const amount = lower * front * side;
      positions[offset] += Math.sign(positions[offset]) * amount * 0.018;
      positions[offset + 2] -= amount * 0.07;
    }
  }
  if (style.key === 'fifties-updo') {
    const crown = headBounds.max.y - 0.025;
    for (let vertex = 0; vertex < count; ++vertex) {
      const offset = vertex * 3 + 1;
      if (positions[offset] > crown) positions[offset] = crown + (positions[offset] - crown) * 0.65;
    }
  }
  if (['long-waves', 'curly-bob', 'wavy-bob'].includes(style.key)) {
    const lift = style.key === 'wavy-bob' ? 0.02 : 0.065;
    for (let vertex = 0; vertex < count; ++vertex) {
      const offset = vertex * 3;
      const front = THREE.MathUtils.clamp((positions[offset + 2] - eyes.L[2] * 0.68) / (eyes.L[2] * 0.45), 0, 1);
      const band = THREE.MathUtils.clamp(1 - Math.abs(positions[offset + 1] - eyes.L[1] - 0.025) / 0.075, 0, 1);
      positions[offset + 1] += front * band * lift;
      positions[offset + 2] -= front * band * 0.018;
      if (style.key === 'wavy-bob') {
        const side = THREE.MathUtils.clamp((Math.abs(positions[offset]) - 0.025) / 0.04, 0, 1);
        const eyeBand = THREE.MathUtils.clamp(1 - Math.abs(positions[offset + 1] - eyes.L[1] - 0.005) / 0.055, 0, 1);
        positions[offset + 1] += front * side * eyeBand * 0.025;
        positions[offset + 2] -= front * side * eyeBand * 0.055;
      }
    }
  }
  const hair = new THREE.BufferGeometry();
  hair.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  hair.setAttribute('uv', new THREE.BufferAttribute(pack[`${style.prefix}_uv`], 2));
  hair.setAttribute('skinIndex', new THREE.BufferAttribute(skinIndex, 4));
  hair.setAttribute('skinWeight', new THREE.BufferAttribute(skinWeight, 4));
  hair.setIndex(new THREE.BufferAttribute(pack[`${style.prefix}_index`], 1));
  hair.computeVertexNormals();
  hair.userData.hairTexture = style.texture;
  return hair;
}

export function buildHumanGeometry(appearance) {
  const data = humanData(appearance.male);
  const positions = new Float32Array(data.position);
  const eyes = applyMorphs(data, positions, appearance.morphs);
  const headBounds = morphedHeadBounds(data, positions);
  const base = new THREE.BufferGeometry();
  base.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  base.setAttribute('uv', new THREE.BufferAttribute(data.uv, 2));
  base.setAttribute('skinIndex', new THREE.BufferAttribute(data.skinIndex, 4));
  base.setAttribute('skinWeight', new THREE.BufferAttribute(data.skinWeight, 4, true));
  const allIndices = new Uint16Array(data.headIndex.length + data.bodyIndex.length);
  allIndices.set(data.headIndex);
  allIndices.set(data.bodyIndex, data.headIndex.length);
  base.setIndex(new THREE.BufferAttribute(allIndices, 1));
  base.computeVertexNormals();
  const attribute = base.attributes.position;
  const normals = base.attributes.normal;
  for (let i = 0; i < attribute.count; ++i) {
    const x = attribute.getX(i);
    const y = attribute.getY(i);
    const rawY = y / data.scale - data.ground;
    const hand = Math.abs(x) > data.regions.handX * data.scale && rawY < data.regions.handY;
    if (rawY < data.regions.collar && rawY > data.regions.boots && !hand) {
      const cloth = 0.008 + (rawY > data.regions.pants ?
        Math.sin(rawY * 11 + x * 38 + attribute.getZ(i) * 17) * 0.002 : 0);
      attribute.setXYZ(i, x + normals.getX(i) * cloth, y + normals.getY(i) * cloth,
        attribute.getZ(i) + normals.getZ(i) * cloth);
    }
  }
  base.computeVertexNormals();
  const body = subset(base, data.bodyIndex, data.bodyGroups);
  const head = subset(base, data.headIndex);
  let scalp = null;
  const capStyle = appearance.hair === 8 ? 0 : (({ 9: 3, 10: 3, 11: 5, 12: 6, 13: 5, 14: 6, 15: 5 })[appearance.hair] || appearance.hair);
  const capIndices = capStyle > 0 ? data[`hair${capStyle}Index`] : null;
  if (capIndices?.length) {
    scalp = subset(base, capIndices).clone();
    const scalpPositions = scalp.attributes.position;
    const scalpNormals = scalp.attributes.normal;
    for (let i = 0; i < scalpPositions.count; ++i) {
      scalpPositions.setXYZ(i, scalpPositions.getX(i) + scalpNormals.getX(i) * 0.0024,
        scalpPositions.getY(i) + scalpNormals.getY(i) * 0.0024,
        scalpPositions.getZ(i) + scalpNormals.getZ(i) * 0.0024);
    }
    scalp.computeVertexNormals();
  }
  let coat = null;
  if (appearance.coat) {
    coat = base.clone();
    const coatPositions = coat.attributes.position;
    const coatNormals = coat.attributes.normal;
    for (let i = 0; i < coatPositions.count; ++i) {
      coatPositions.setXYZ(i, coatPositions.getX(i) + coatNormals.getX(i) * 0.009,
        coatPositions.getY(i) + coatNormals.getY(i) * 0.009,
        coatPositions.getZ(i) + coatNormals.getZ(i) * 0.009);
    }
    coat.setIndex(new THREE.BufferAttribute(data.coatIndex, 1));
    coat.computeVertexNormals();
  }
  return { data, body, head, scalp, coat, hair: hairGeometry(data, appearance, headBounds, eyes), eyes, headBounds };
}
