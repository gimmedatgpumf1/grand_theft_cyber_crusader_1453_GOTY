import * as THREE from './vendor/three.module.min.js';
import { PALETTES } from './appearance.js';

function surface(color, options = {}) {
  return new THREE.MeshStandardMaterial({
    color, roughness: options.roughness ?? 0.82, metalness: options.metalness ?? 0,
    flatShading: options.flat ?? false, side: options.side ?? THREE.FrontSide,
    vertexColors: options.vertexColors ?? false,
  });
}

function attach(bone, object, position) {
  object.position.copy(new THREE.Vector3(...position).applyMatrix4(bone.userData.bindInverse));
  bone.add(object);
  return object;
}

function smallMesh(parent, geometry, material, owned, position = null) {
  const object = new THREE.Mesh(geometry, material);
  object.castShadow = true;
  if (position) object.position.set(...position);
  parent.add(object);
  owned.geometries.add(geometry);
  owned.materials.add(material);
  return object;
}

function loft(rings, material, arcStart = 0, arcLength = Math.PI * 2) {
  const positions = [];
  const indices = [];
  const segments = 24;
  for (const ring of rings) {
    for (let i = 0; i <= segments; ++i) {
      const angle = arcStart + arcLength * i / segments;
      positions.push(Math.cos(angle) * ring[1], ring[0], Math.sin(angle) * ring[2]);
    }
  }
  for (let r = 0; r < rings.length - 1; ++r) for (let i = 0; i < segments; ++i) {
    const a = r * (segments + 1) + i;
    const b = a + 1;
    const c = a + segments + 1;
    indices.push(a, c, b, b, c, c + 1);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return new THREE.Mesh(geometry, material);
}

export function addEyes(view, owned) {
  const { appearance, rig, eyes } = view;
  const white = surface('#d8d5ca', { roughness: 0.32 });
  const iris = surface(appearance.sunglasses ? '#1c2328' : PALETTES.eyes[appearance.eyeColor], { roughness: 0.25 });
  const pupil = surface('#090c0d', { roughness: 0.2 });
  const parts = [];
  const size = 1 + (appearance.morphs[16] || 0) * 0.0015;
  for (const side of ['L', 'R']) {
    const center = eyes[side];
    const geometry = new THREE.SphereGeometry(0.0110 * size, 20, 12);
    const eye = smallMesh(rig.byName.head, geometry, white, owned);
    attach(rig.byName.head, eye, center);
    parts.push(eye);
    const colored = smallMesh(rig.byName.head, new THREE.SphereGeometry(1, 12, 8), iris, owned);
    colored.scale.set(0.0045 * size, 0.0045 * size, 0.0016);
    attach(rig.byName.head, colored, [center[0], center[1], center[2] + 0.0100 * size]);
    const dark = smallMesh(rig.byName.head, new THREE.SphereGeometry(1, 10, 6), pupil, owned);
    dark.scale.set(0.0019 * size, 0.0019 * size, 0.001);
    attach(rig.byName.head, dark, [center[0], center[1], center[2] + 0.0114 * size]);
    parts.push(colored, dark);
  }
  return parts;
}

export function addSunglasses(view, owned) {
  const { rig, eyes } = view;
  const frame = surface('#101417', { metalness: 0.45, roughness: 0.25 });
  const lens = surface('#294651', { metalness: 0.55, roughness: 0.12, side: THREE.DoubleSide });
  const shape = new THREE.Shape();
  shape.moveTo(-0.025, 0.009);
  shape.lineTo(0.025, 0.009);
  shape.lineTo(0.021, -0.006);
  shape.lineTo(0.010, -0.011);
  shape.lineTo(-0.018, -0.009);
  shape.lineTo(-0.025, 0.002);
  shape.closePath();
  const parts = [];
  for (const side of ['L', 'R']) {
    const center = eyes[side];
    const sign = side === 'L' ? 1 : -1;
    const front = center[2] + 0.025;
    const glass = smallMesh(rig.byName.head, new THREE.ShapeGeometry(shape), lens, owned);
    attach(rig.byName.head, glass, [center[0], center[1] + 0.002, front]);
    parts.push(glass);
    for (const [w, h, x, y] of [[0.054, 0.004, 0, 0.010], [0.044, 0.002, 0, -0.010],
      [0.003, 0.016, -0.025, 0.001], [0.003, 0.016, 0.025, 0.001]]) {
      const bar = smallMesh(rig.byName.head, new THREE.BoxGeometry(w, h, 0.004), frame, owned);
      attach(rig.byName.head, bar, [center[0] + x, center[1] + 0.002 + y, front + 0.003]);
      parts.push(bar);
    }
    const temple = smallMesh(rig.byName.head, new THREE.BoxGeometry(0.042, 0.003, 0.004), frame, owned);
    temple.rotation.y = sign * 0.85;
    attach(rig.byName.head, temple, [sign * 0.068, center[1] + 0.007, front - 0.034]);
    parts.push(temple);
  }
  const bridge = smallMesh(rig.byName.head, new THREE.BoxGeometry(0.016, 0.003, 0.004), frame, owned);
  attach(rig.byName.head, bridge, [0, eyes.L[1] + 0.005, eyes.L[2] + 0.029]);
  parts.push(bridge);
  return parts;
}

export function addShoes(view, owned) {
  const { data, rig } = view;
  const leather = surface('#151a1d', { roughness: 0.48 });
  const rubber = surface('#0d1113', { roughness: 0.8 });
  for (const suffix of ['L', 'R']) {
    const bone = rig.byName[`foot.${suffix}`];
    const center = data.shoes[suffix];
    const upper = smallMesh(bone, new THREE.SphereGeometry(1, 14, 8), leather, owned);
    upper.scale.set(0.064, 0.035, 0.150);
    attach(bone, upper, center);
    const sole = smallMesh(bone, new THREE.SphereGeometry(1, 14, 6), rubber, owned);
    sole.scale.set(0.066, 0.012, 0.153);
    attach(bone, sole, [center[0], 0.016, center[2]]);
  }
}

function rawMesh(view, raw, indices, material, owned) {
  const { data } = view;
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(raw.flatMap(point =>
    [point[0] * data.scale, (point[1] + data.ground) * data.scale, point[2] * data.scale]), 3));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  const object = new THREE.Mesh(geometry, material);
  object.castShadow = true;
  attach(view.rig.byName.spine01, object, [0, 0, 0]);
  owned.geometries.add(geometry);
  owned.materials.add(material);
  view.coatDetails.push(object);
  return object;
}

export function addClothDetails(view, appearance, owned) {
  const dark = surface('#151a1d', { roughness: 0.62, side: THREE.DoubleSide });
  const belt = loft([[0.945, 0.24, 0.166], [0.975, 0.24, 0.166]], dark);
  belt.castShadow = true;
  view.visual.add(belt);
  owned.geometries.add(belt.geometry);
  owned.materials.add(dark);
  view.belt = belt;
  const { data } = view;
  const neckY = (data.regions.collar + data.ground) * data.scale;
  const collarMaterial = surface(appearance.suit ? '#dfe3df' : PALETTES.shirt[appearance.shirt],
    { side: THREE.DoubleSide, roughness: 0.84 });
  const collarHeight = appearance.beret ? 0.035 : appearance.suit ? 0.026 : 0.020;
  const collarX = appearance.suit ? 0.112 : 0.094;
  const collarZ = appearance.suit ? 0.095 : 0.082;
  const neckline = loft([[neckY - 0.018, collarX, collarZ],
    [neckY + collarHeight, collarX * 0.93, collarZ * 0.94]], collarMaterial);
  neckline.castShadow = true;
  attach(view.rig.byName.spine01, neckline, [0, 0, 0]);
  owned.geometries.add(neckline.geometry);
  owned.materials.add(collarMaterial);
  view.coatDetails.push(neckline);
  if (appearance.coat) {
    const coatColor = new THREE.Color(PALETTES.shirt[appearance.shirt]).multiplyScalar(0.82);
    const coat = surface(coatColor, { side: THREE.DoubleSide });
    const skirt = loft([[0.43, 0.34, 0.21], [0.70, 0.29, 0.18], [0.98, 0.23, 0.15]],
      coat, Math.PI / 2 + 0.42, Math.PI * 2 - 0.84);
    skirt.castShadow = true;
    view.visual.add(skirt);
    owned.geometries.add(skirt.geometry);
    owned.materials.add(coat);
    view.coatSkirt = skirt;
    const lapelMaterial = surface(coatColor.clone().multiplyScalar(1.4), { side: THREE.DoubleSide });
    for (const side of [-1, 1]) {
      rawMesh(view, [[side * 0.72, 5.35 + data.bodyShift, 1.06],
        [side * 0.13, 5.62 + data.bodyShift, 0.91],
        [side * 0.30, 3.82 + data.bodyShift, 1.71]], [0, 1, 2], lapelMaterial, owned);
    }
  }
  if (appearance.dress) {
    const material = surface(PALETTES.shirt[appearance.shirt], { side: THREE.DoubleSide, roughness: 0.72 });
    const skirt = loft([[0.40, 0.36, 0.25], [0.72, 0.29, 0.21], [1.04, 0.22, 0.16]], material);
    skirt.castShadow = true;
    view.visual.add(skirt);
    owned.geometries.add(skirt.geometry);
    owned.materials.add(material);
    view.coatDetails.push(skirt);
  }
  if (appearance.suit) {
    const jacket = surface(new THREE.Color(PALETTES.shirt[appearance.shirt]).multiplyScalar(1.18), { side: THREE.DoubleSide });
    const collar = surface('#e4e5df', { side: THREE.DoubleSide, roughness: 0.72 });
    for (const side of [-1, 1]) {
      rawMesh(view, [[side * 0.72, 5.35 + data.bodyShift, 1.06],
        [side * 0.16, 5.57 + data.bodyShift, 0.94],
        [side * 0.28, 4.02 + data.bodyShift, 1.66]], [0, 1, 2], jacket, owned);
      rawMesh(view, [[side * 0.08, 5.64 + data.bodyShift, 0.97],
        [side * 0.42, 5.50 + data.bodyShift, 1.13],
        [side * 0.19, 5.15 + data.bodyShift, 1.40]], [0, 1, 2], collar, owned);
    }
    if (appearance.tie > 0) {
      const tie = surface(PALETTES.tie[appearance.tie], { side: THREE.DoubleSide, roughness: 0.55 });
      rawMesh(view, [[-0.10, 5.40 + data.bodyShift, 1.43], [0.10, 5.40 + data.bodyShift, 1.43],
        [0.16, 4.15 + data.bodyShift, 1.73], [0, 3.94 + data.bodyShift, 1.76],
        [-0.16, 4.15 + data.bodyShift, 1.73]], [0, 1, 2, 0, 2, 4, 4, 2, 3], tie, owned);
      rawMesh(view, [[-0.11, 5.52 + data.bodyShift, 1.38], [0.11, 5.52 + data.bodyShift, 1.38],
        [0.08, 5.30 + data.bodyShift, 1.48], [-0.08, 5.30 + data.bodyShift, 1.48]], [0, 1, 2, 0, 2, 3], tie, owned);
    }
  }
}

let strandMap = null;
function strandTexture() {
  if (strandMap) return strandMap;
  const canvas = document.createElement('canvas');
  canvas.width = 64;
  canvas.height = 128;
  const context = canvas.getContext('2d');
  context.fillStyle = '#e8e8e8';
  context.fillRect(0, 0, 64, 128);
  for (let x = 0; x < 64; x += 3) {
    context.strokeStyle = x % 2 ? 'rgba(30,30,30,.18)' : 'rgba(255,255,255,.22)';
    context.beginPath();
    context.moveTo(x, 0);
    context.bezierCurveTo(x + 3, 40, x - 3, 86, x + 1, 128);
    context.stroke();
  }
  strandMap = new THREE.CanvasTexture(canvas);
  strandMap.colorSpace = THREE.SRGBColorSpace;
  return strandMap;
}

function hairLock(view, points, width, material, owned) {
  const curve = new THREE.CatmullRomCurve3(points.map(point => new THREE.Vector3(...point)));
  const samples = curve.getPoints(16);
  const positions = [];
  const uvs = [];
  const indices = [];
  samples.forEach((point, index) => {
    const before = samples[Math.max(0, index - 1)];
    const after = samples[Math.min(samples.length - 1, index + 1)];
    const tangent = after.clone().sub(before).normalize();
    const side = new THREE.Vector3(-tangent.y, tangent.x, 0);
    if (side.lengthSq() < 0.001) side.set(1, 0, 0);
    const taper = 0.45 + 0.55 * Math.sin(Math.PI * index / (samples.length - 1));
    side.normalize().multiplyScalar(width * 1.8 * taper);
    const left = point.clone().add(side);
    const right = point.clone().sub(side);
    positions.push(left.x, left.y, left.z, right.x, right.y, right.z);
    uvs.push(0, index / 16, 1, index / 16);
    if (index < samples.length - 1) {
      const a = index * 2;
      indices.push(a, a + 2, a + 1, a + 1, a + 2, a + 3);
    }
  });
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  const mesh = new THREE.Mesh(geometry, material);
  mesh.castShadow = true;
  attach(view.rig.byName.head, mesh, [0, 0, 0]);
  owned.geometries.add(mesh.geometry);
  owned.materials.add(material);
  view.extraHeadParts.push(mesh);
  return mesh;
}

export function addHairDetails(view, appearance, owned) {
  if (![5, 6, 7].includes(appearance.hair)) return;
  const material = surface(PALETTES.hairColor[appearance.hairColor], { roughness: 0.64, side: THREE.DoubleSide });
  material.map = strandTexture();
  const { headBounds, eyes } = view;
  const width = Math.max(0.085, (headBounds.max.x - headBounds.min.x) * 0.43);
  const top = headBounds.max.y;
  const front = eyes.L[2];
  const back = headBounds.min.z;
  if (appearance.hair === 5) {
    for (const side of [-1, 1]) for (let i = 0; i < 4; ++i) {
      const x = side * (width * 0.88 + 0.006);
      const y = eyes.L[1] + 0.07 - i * 0.035;
      hairLock(view, [[x * 0.82, y + 0.035, front * 0.35], [x * 1.08, y + 0.015, front * 0.52],
        [x * 1.12, y - 0.015, front * 0.22], [x * 0.92, y - 0.030, back * 0.25]], 0.015, material, owned);
    }
    for (let i = 0; i < 3; ++i) {
      hairLock(view, [[-width * 0.85, top - 0.035 - i * 0.012, front * 0.22],
        [-width * 0.20, top + 0.005 - i * 0.012, front * 0.72],
        [width * 0.65, top - 0.055 - i * 0.010, front * 0.58]], 0.014, material, owned);
    }
  } else if (appearance.hair === 6) {
    for (const side of [-1, 1]) for (let i = 0; i < 5; ++i) {
      const x = side * (width * (0.68 + i * 0.09));
      const z = back * 0.45 + i * 0.018;
      hairLock(view, [[x * 0.85, top - 0.07, z], [x, eyes.L[1] - 0.07, z + 0.015],
        [x * 1.08, 1.13, z + 0.025], [x * 0.92, 1.00 + i * 0.008, z + 0.04]], 0.014, material, owned);
    }
    for (let i = 0; i < 5; ++i) {
      const x = -width * 0.75 + i * width * 0.35;
      hairLock(view, [[x - 0.025, top - 0.045, front * 0.45], [x, eyes.L[1] + 0.095, front * 0.92],
        [x + 0.035, eyes.L[1] + 0.040 + i * 0.003, front * 0.98]], 0.012, material, owned);
    }
  } else {
    for (let i = 0; i < 6; ++i) {
      const z = back * 0.25 + i * (front - back * 0.25) / 6;
      hairLock(view, [[-width * 0.90, top - 0.045, z - 0.015], [-width * 0.25, top + 0.014, z],
        [width * 0.50, top - 0.005, z + 0.012], [width * 0.90, top - 0.060, z + 0.018]], 0.012, material, owned);
    }
  }
}

export function addAccessories(view, appearance, owned) {
  const { rig, headBounds, eyes } = view;
  if (appearance.beret) {
    const red = surface('#a92f35', { roughness: 0.72 });
    const width = (headBounds.max.x - headBounds.min.x) * 0.50;
    const top = headBounds.max.y + 0.005;
    const cap = smallMesh(rig.byName.head, new THREE.SphereGeometry(1, 24, 12), red, owned);
    cap.scale.set(width * 1.14, 0.032, 0.098);
    cap.rotation.z = -0.12;
    attach(rig.byName.head, cap, [0.008, top, -0.008]);
    view.extraHeadParts.push(cap);
    const badge = smallMesh(rig.byName.head, new THREE.OctahedronGeometry(0.009, 0),
      surface('#d8bd62', { metalness: 0.6, roughness: 0.3 }), owned);
    attach(rig.byName.head, badge, [0.018, top - 0.005, eyes.L[2] * 0.82]);
    view.extraHeadParts.push(badge);
  }
  if (appearance.earrings) {
    const jewel = surface('#e5e5dc', { metalness: 0.65, roughness: 0.18 });
    for (const side of [-1, 1]) {
      const earring = smallMesh(rig.byName.head, new THREE.OctahedronGeometry(0.009, 1), jewel, owned);
      earring.scale.y = 1.5;
      attach(rig.byName.head, earring, [side * (headBounds.max.x - 0.008), eyes.L[1] - 0.055, eyes.L[2] - 0.065]);
      view.extraHeadParts.push(earring);
    }
  }
  if (appearance.necklace) {
    const pearl = surface('#eee9d8', { metalness: 0.25, roughness: 0.24 });
    for (let i = 0; i < 18; ++i) {
      const angle = i / 18 * Math.PI * 2;
      const bead = smallMesh(rig.byName.spine01, new THREE.SphereGeometry(0.0055, 8, 6), pearl, owned);
      attach(rig.byName.spine01, bead, [Math.cos(angle) * 0.070, 1.405 + Math.sin(angle) * 0.006, Math.sin(angle) * 0.050]);
    }
  }
  if (appearance.fur) {
    const fur = surface('#e8e5da', { roughness: 0.96 });
    for (let i = 0; i < 2; ++i) {
      const collar = smallMesh(rig.byName.spine01, new THREE.TorusGeometry(0.145 + i * 0.018, 0.027, 8, 32), fur, owned);
      collar.rotation.x = Math.PI / 2;
      collar.scale.y = 0.72;
      attach(rig.byName.spine01, collar, [0, 1.415 - i * 0.018, 0]);
    }
  }
}
