import * as THREE from './vendor/three.module.min.js';
import { humanData } from './vendor/human-data.js';
import { PALETTES, normalizeAppearance } from './appearance.js';

const geometryCache = new Map();
const maskCache = new Map();
const tintCache = new Map();
let clothMap = null;

function fabricTexture() {
  if (clothMap) return clothMap;
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 128;
  const context = canvas.getContext('2d');
  context.fillStyle = '#f4f4f4';
  context.fillRect(0, 0, 128, 128);
  for (let i = 0; i < 128; i += 4) {
    context.strokeStyle = i % 8 ? 'rgba(20,20,20,.055)' : 'rgba(255,255,255,.12)';
    context.beginPath();
    context.moveTo(i, 0);
    context.lineTo(i, 128);
    context.moveTo(0, i);
    context.lineTo(128, i);
    context.stroke();
  }
  clothMap = new THREE.CanvasTexture(canvas);
  clothMap.wrapS = clothMap.wrapT = THREE.RepeatWrapping;
  clothMap.repeat.set(5, 5);
  clothMap.colorSpace = THREE.SRGBColorSpace;
  return clothMap;
}

function surface(color, options = {}) {
  return new THREE.MeshStandardMaterial({
    color, roughness: options.roughness ?? 0.82, metalness: options.metalness ?? 0,
    flatShading: options.flat ?? false, side: options.side ?? THREE.FrontSide,
    vertexColors: options.vertexColors ?? false,
  });
}

function subset(base, indices, groups = null) {
  const geometry = new THREE.BufferGeometry();
  for (const [name, attribute] of Object.entries(base.attributes)) geometry.setAttribute(name, attribute);
  geometry.setIndex(new THREE.BufferAttribute(indices, 1));
  if (groups) for (const group of groups) geometry.addGroup(group.start, group.count, group.materialIndex);
  geometry.userData.shared = true;
  return geometry;
}

function shapedPositions(male) {
  return new Float32Array(humanData(male).position);
}

function baseGeometry(male) {
  const key = male ? 'male' : 'female';
  if (geometryCache.has(key)) return geometryCache.get(key);
  const data = humanData(male);
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(shapedPositions(male), 3));
  geometry.setAttribute('uv', new THREE.BufferAttribute(data.uv, 2));
  geometry.setAttribute('skinIndex', new THREE.BufferAttribute(data.skinIndex, 4));
  geometry.setAttribute('skinWeight', new THREE.BufferAttribute(data.skinWeight, 4, true));
  const allIndices = new Uint16Array(data.headIndex.length + data.bodyIndex.length);
  allIndices.set(data.headIndex);
  allIndices.set(data.bodyIndex, data.headIndex.length);
  geometry.setIndex(new THREE.BufferAttribute(allIndices, 1));
  geometry.computeVertexNormals();
  const positions = geometry.attributes.position;
  const normals = geometry.attributes.normal;
  for (let i = 0; i < positions.count; ++i) {
    const x = positions.getX(i);
    const y = positions.getY(i);
    const rawY = y / data.scale - data.ground;
    const hand = Math.abs(x) > data.regions.handX * data.scale && rawY < data.regions.handY;
    if (rawY < data.regions.head && rawY > data.regions.boots && !hand) {
      const cloth = 0.008 + (rawY > data.regions.pants ? Math.sin(rawY * 11 + x * 38 + positions.getZ(i) * 17) * 0.002 : 0);
      positions.setXYZ(i, x + normals.getX(i) * cloth, y + normals.getY(i) * cloth,
        positions.getZ(i) + normals.getZ(i) * cloth);
    }
  }
  geometry.computeVertexNormals();
  geometry.userData.shared = true;
  geometryCache.set(key, geometry);
  return geometry;
}

function humanGeometries(appearance) {
  const key = `${appearance.male ? 1 : 0}:${appearance.hair}:${appearance.coat ? 1 : 0}`;
  if (geometryCache.has(key)) return geometryCache.get(key);
  const data = humanData(appearance.male);
  const base = baseGeometry(appearance.male);
  const body = subset(base, data.bodyIndex, data.bodyGroups);
  const head = subset(base, data.headIndex);
  let coat = null;
  if (appearance.coat) {
    coat = base.clone();
    const positions = coat.attributes.position;
    const normals = coat.attributes.normal;
    for (let i = 0; i < positions.count; ++i) {
      positions.setXYZ(i, positions.getX(i) + normals.getX(i) * 0.009,
        positions.getY(i) + normals.getY(i) * 0.009, positions.getZ(i) + normals.getZ(i) * 0.009);
    }
    coat.setIndex(new THREE.BufferAttribute(data.coatIndex, 1));
    coat.userData.shared = true;
  }
  let hair = null;
  if (appearance.hair > 0) {
    hair = base.clone();
    const positions = hair.attributes.position;
    const normals = hair.attributes.normal;
    const colors = new Float32Array(positions.count * 3);
    for (let i = 0; i < positions.count; ++i) {
      const rawX = positions.getX(i) / data.scale;
      const rawY = positions.getY(i) / data.scale - data.ground;
      const rawZ = positions.getZ(i) / data.scale;
      let thickness = 0.005;
      let part = false;
      if (appearance.hair === 2) {
        thickness = 0.007 + 0.006 * THREE.MathUtils.clamp((rawY - 7.4 - data.headShift) / 1.1, 0, 1);
        if (Math.abs(rawX + 0.18) < 0.022 && rawZ > 0.25 && rawY > 7.55 + data.headShift) {
          thickness = 0.001;
          part = true;
        }
      }
      positions.setXYZ(i, positions.getX(i) + normals.getX(i) * thickness,
        positions.getY(i) + normals.getY(i) * thickness, positions.getZ(i) + normals.getZ(i) * thickness);
      const strand = (part ? 0.25 : 0.55) + 0.65 *
        (0.5 + 0.5 * Math.sin(positions.getX(i) * 270 + positions.getY(i) * 61 + positions.getZ(i) * 90));
      colors[i * 3] = strand;
      colors[i * 3 + 1] = strand;
      colors[i * 3 + 2] = strand;
    }
    hair.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    hair.setIndex(new THREE.BufferAttribute(data[`hair${appearance.hair}Index`], 1));
    hair.userData.shared = true;
  }
  const result = { body, head, coat, hair };
  geometryCache.set(key, result);
  return result;
}

function mask(name, repaint) {
  if (maskCache.has(name)) return maskCache.get(name);
  const image = new Image();
  image.onload = repaint;
  image.src = `assets/human/${name}.png`;
  maskCache.set(name, image);
  return image;
}

function tintedMask(image, color) {
  const key = `${image.src}:${color}`;
  if (tintCache.has(key)) return tintCache.get(key);
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 1024;
  const context = canvas.getContext('2d');
  context.drawImage(image, 0, 0, 1024, 1024);
  context.globalCompositeOperation = 'source-in';
  context.fillStyle = color;
  context.fillRect(0, 0, 1024, 1024);
  tintCache.set(key, canvas);
  return canvas;
}

function paintSkin(canvas, appearance) {
  const size = canvas.width;
  const context = canvas.getContext('2d');
  const data = humanData(appearance.male);
  const point = name => {
    const uv = data.landmarks[name];
    return [uv[0] * size, (1 - uv[1]) * size];
  };
  context.globalAlpha = 1;
  context.globalCompositeOperation = 'source-over';
  context.fillStyle = PALETTES.skin[appearance.skin];
  context.fillRect(0, 0, size, size);
  for (let i = 0; i < 1700; ++i) {
    const x = (Math.sin(i * 91.731 + appearance.skin) * 0.5 + 0.5) * size;
    const y = (Math.sin(i * 37.117 + 4.2) * 0.5 + 0.5) * size;
    context.fillStyle = i % 2 ? 'rgba(80,38,24,.025)' : 'rgba(255,235,205,.035)';
    context.fillRect(x, y, 1 + i % 3, 1 + (i * 7) % 3);
  }
  const cheekColor = appearance.skin > 3 ? 'rgba(155,78,58,.10)' : 'rgba(190,85,70,.12)';
  for (const name of ['cheekL', 'cheekR']) {
    const [x, y] = point(name);
    const gradient = context.createRadialGradient(x, y, 2, x, y, 48);
    gradient.addColorStop(0, cheekColor);
    gradient.addColorStop(1, 'rgba(0,0,0,0)');
    context.fillStyle = gradient;
    context.fillRect(x - 50, y - 50, 100, 100);
  }
  const nose = point('nose');
  for (const name of ['noseL', 'noseR']) {
    const [x, y] = point(name);
    const gradient = context.createRadialGradient(x, y, 1, x, y, 22);
    gradient.addColorStop(0, 'rgba(65,32,24,.10)');
    gradient.addColorStop(1, 'rgba(0,0,0,0)');
    context.fillStyle = gradient;
    context.fillRect(x - 24, y - 24, 48, 48);
  }
  if (appearance.beard > 0) {
    const mouth = point('mouth');
    const chin = point('chin');
    context.save();
    context.beginPath();
    if (appearance.beard === 1) {
      const jawL = point('jawL');
      const jawR = point('jawR');
      context.moveTo(...point('mouthL'));
      context.lineTo(...jawL);
      context.lineTo(...chin);
      context.lineTo(...jawR);
      context.lineTo(...point('mouthR'));
    } else {
      context.moveTo(mouth[0] + 7, mouth[1] - 11);
      context.bezierCurveTo(mouth[0] + 32, mouth[1] - 14, chin[0] - 10, chin[1] - 8, chin[0] - 2, chin[1]);
      context.bezierCurveTo(chin[0] - 10, chin[1] + 8, mouth[0] + 32, mouth[1] + 14, mouth[0] + 7, mouth[1] + 11);
      context.closePath();
    }
    context.closePath();
    context.clip();
    context.globalAlpha = appearance.beard === 1 ? 0.14 : 0.72;
    context.fillStyle = PALETTES.hairColor[appearance.hairColor];
    context.fillRect(0, 0, size, size);
    context.globalAlpha = appearance.beard === 1 ? 0.24 : 0.40;
    for (let i = 0; i < 500; ++i) {
      const x = mouth[0] + (Math.sin(i * 17.13) * 0.5 + 0.5) * (chin[0] - mouth[0] + 25);
      const y = chin[1] + Math.sin(i * 43.77) * (appearance.beard === 1 ? 100 : 18);
      context.fillRect(x, y, 1, 2);
    }
    context.restore();
    context.globalAlpha = appearance.beard === 1 ? 0.13 : 0.62;
    context.fillStyle = PALETTES.hairColor[appearance.hairColor];
    context.beginPath();
    context.ellipse((nose[0] + mouth[0]) / 2, mouth[1], 3, 17, 0, 0, Math.PI * 2);
    context.fill();
    context.globalAlpha = 1;
  }
  const eyelids = maskCache.get('eyelids');
  if (eyelids?.complete) {
    context.globalAlpha = 0.30;
    context.drawImage(tintedMask(eyelids, '#382820'), 0, 0, size, size);
  }
  const lips = maskCache.get('lips');
  if (lips?.complete) {
    context.globalAlpha = appearance.male ? 0.22 : 0.34;
    context.drawImage(tintedMask(lips, appearance.male ? '#743f39' : '#9e4c58'), 0, 0, size, size);
  } else {
    const mouth = point('mouth');
    context.globalAlpha = 0.4;
    context.fillStyle = appearance.male ? '#743f39' : '#9e4c58';
    context.beginPath();
    context.ellipse(mouth[0], mouth[1], 4, 27, 0, 0, Math.PI * 2);
    context.fill();
  }
  context.globalAlpha = 0.60;
  context.strokeStyle = PALETTES.hairColor[appearance.hairColor];
  context.lineCap = 'round';
  for (const side of ['L', 'R']) {
    const inner = point(`browInner${side}`);
    const outer = point(`browOuter${side}`);
    context.lineWidth = 3;
    context.beginPath();
    context.moveTo(...inner);
    context.quadraticCurveTo((inner[0] + outer[0]) / 2 - 4, (inner[1] + outer[1]) / 2, ...outer);
    context.stroke();
  }
  context.globalAlpha = 1;
}

function skinTexture(appearance) {
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 1024;
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  const repaint = () => {
    paintSkin(canvas, appearance);
    texture.needsUpdate = true;
  };
  mask('lips', repaint);
  mask('eyelids', repaint);
  repaint();
  return texture;
}

function makeRig(visual, male) {
  const data = humanData(male);
  const bones = data.bones.map(definition => new THREE.Bone());
  const byName = {};
  data.bones.forEach((definition, index) => {
    const bone = bones[index];
    bone.name = definition.name;
    byName[bone.name] = bone;
    const scale = data.scale;
    const head = new THREE.Vector3(definition.head[0] * scale,
      (definition.head[1] + data.ground) * scale, definition.head[2] * scale);
    if (definition.parent >= 0) {
      const parent = data.bones[definition.parent];
      head.sub(new THREE.Vector3(parent.head[0] * scale, (parent.head[1] + data.ground) * scale, parent.head[2] * scale));
      bones[definition.parent].add(bone);
    } else visual.add(bone);
    bone.position.copy(head);
  });
  visual.updateMatrixWorld(true);
  for (let i = 0; i < bones.length; ++i) {
    const definition = data.bones[i];
    bones[i].userData.bindInverse = bones[i].matrixWorld.clone().invert();
    const tail = new THREE.Vector3(definition.tail[0] * data.scale,
      (definition.tail[1] + data.ground) * data.scale, definition.tail[2] * data.scale);
    bones[i].userData.tailLocal = bones[i].worldToLocal(tail.clone());
  }
  const skeleton = new THREE.Skeleton(bones);
  const aim = (name, direction) => {
    const bone = byName[name];
    visual.updateMatrixWorld(true);
    const origin = bone.getWorldPosition(new THREE.Vector3());
    const tail = bone.localToWorld(bone.userData.tailLocal.clone());
    const delta = new THREE.Quaternion().setFromUnitVectors(tail.sub(origin).normalize(), direction.clone().normalize());
    const parent = bone.parent.getWorldQuaternion(new THREE.Quaternion());
    const local = parent.clone().invert().multiply(delta).multiply(parent);
    bone.quaternion.premultiply(local);
    visual.updateMatrixWorld(true);
  };
  for (const [suffix, side] of [['L', 1], ['R', -1]]) {
    aim(`upperarm01.${suffix}`, new THREE.Vector3(side * 0.17, -1, 0.025));
    aim(`lowerarm01.${suffix}`, new THREE.Vector3(side * 0.07, -1, 0.12));
    aim(`wrist.${suffix}`, new THREE.Vector3(side * 0.025, -1, 0.12));
    aim(`upperleg01.${suffix}`, new THREE.Vector3(side * 0.025, -1, 0.02));
    aim(`lowerleg01.${suffix}`, new THREE.Vector3(side * 0.012, -1, 0.025));
  }
  visual.updateMatrixWorld(true);
  for (const bone of bones) {
    bone.userData.neutral = bone.quaternion.clone();
    bone.userData.restPosition = bone.position.clone();
    bone.userData.parentNeutral = bone.parent.getWorldQuaternion(new THREE.Quaternion());
  }
  return { bones, byName, skeleton };
}

function skinned(visual, geometry, materials, skeleton, owned) {
  const object = new THREE.SkinnedMesh(geometry, materials);
  object.castShadow = true;
  object.receiveShadow = true;
  object.frustumCulled = false;
  visual.add(object);
  object.bind(skeleton, new THREE.Matrix4());
  owned.objects.push(object);
  return object;
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

function addEyes(rig, appearance, owned) {
  const data = humanData(appearance.male);
  const white = surface('#d8d5ca', { roughness: 0.32 });
  const iris = surface(appearance.sunglasses ? '#1c2328' : '#40545d', { roughness: 0.25 });
  const pupil = surface('#090c0d', { roughness: 0.2 });
  const parts = [];
  for (const side of ['L', 'R']) {
    const center = data.eyes[side];
    const eye = smallMesh(rig.byName.head, new THREE.SphereGeometry(0.0125, 12, 8), white, owned);
    attach(rig.byName.head, eye, center);
    const colored = smallMesh(rig.byName.head, new THREE.SphereGeometry(1, 10, 6), iris, owned);
    colored.scale.set(0.0049, 0.0049, 0.0018);
    attach(rig.byName.head, colored, [center[0], center[1], center[2] + 0.0113]);
    const dark = smallMesh(rig.byName.head, new THREE.SphereGeometry(1, 8, 5), pupil, owned);
    dark.scale.set(0.0021, 0.0021, 0.001);
    attach(rig.byName.head, dark, [center[0], center[1], center[2] + 0.013]);
    parts.push(eye, colored, dark);
  }
  return parts;
}

function addSunglasses(rig, appearance, owned) {
  const data = humanData(appearance.male);
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
    const center = data.eyes[side];
    const sign = side === 'L' ? 1 : -1;
    const glass = smallMesh(rig.byName.head, new THREE.ShapeGeometry(shape), lens, owned);
    attach(rig.byName.head, glass, [center[0], center[1] + 0.002, 0.176]);
    parts.push(glass);
    for (const [w, h, x, y] of [[0.054, 0.004, 0, 0.010], [0.044, 0.002, 0, -0.010],
      [0.003, 0.016, -0.025, 0.001], [0.003, 0.016, 0.025, 0.001]]) {
      const bar = smallMesh(rig.byName.head, new THREE.BoxGeometry(w, h, 0.004), frame, owned);
      attach(rig.byName.head, bar, [center[0] + x, center[1] + 0.002 + y, 0.179]);
      parts.push(bar);
    }
    const temple = smallMesh(rig.byName.head, new THREE.BoxGeometry(0.042, 0.003, 0.004), frame, owned);
    temple.rotation.y = sign * 0.85;
    attach(rig.byName.head, temple, [sign * 0.068, center[1] + 0.007, 0.145]);
    parts.push(temple);
  }
  const bridge = smallMesh(rig.byName.head, new THREE.BoxGeometry(0.016, 0.003, 0.004), frame, owned);
  attach(rig.byName.head, bridge, [0, data.eyes.L[1] + 0.005, 0.18]);
  parts.push(bridge);
  return parts;
}

function addShoes(rig, male, owned) {
  const data = humanData(male);
  const leather = surface('#151a1d', { roughness: 0.48 });
  const rubber = surface('#0d1113', { roughness: 0.8 });
  for (const [suffix, sign] of [['L', 1], ['R', -1]]) {
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

function loft(rings, material, arcStart = 0, arcLength = Math.PI * 2) {
  const positions = [];
  const indices = [];
  const segments = 16;
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

function addClothDetails(view, appearance, owned) {
  const dark = surface('#151a1d', { roughness: 0.62, side: THREE.DoubleSide });
  const belt = loft([[0.945, 0.24, 0.166], [0.975, 0.24, 0.166]], dark);
  belt.castShadow = true;
  view.visual.add(belt);
  owned.geometries.add(belt.geometry);
  owned.materials.add(dark);
  view.belt = belt;
  if (!appearance.coat) return;
  const coatColor = new THREE.Color(PALETTES.shirt[appearance.shirt]).multiplyScalar(0.82);
  const coat = surface(coatColor, { side: THREE.DoubleSide });
  coat.map = fabricTexture();
  const skirt = loft([[0.43, 0.34, 0.21], [0.70, 0.29, 0.18], [0.98, 0.23, 0.15]],
    coat, Math.PI / 2 + 0.42, Math.PI * 2 - 0.84);
  skirt.castShadow = true;
  view.visual.add(skirt);
  owned.geometries.add(skirt.geometry);
  owned.materials.add(coat);
  view.coatSkirt = skirt;
  const data = humanData(appearance.male);
  const scale = data.scale;
  const ground = data.ground;
  const rawMesh = (raw, indices, material) => {
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.Float32BufferAttribute(raw.flatMap(p =>
      [p[0] * scale, (p[1] + ground) * scale, p[2] * scale]), 3));
    geometry.setIndex(indices);
    geometry.computeVertexNormals();
    const object = new THREE.Mesh(geometry, material);
    attach(view.rig.byName.spine01, object, [0, 0, 0]);
    owned.geometries.add(geometry);
    view.coatDetails.push(object);
    return object;
  };
  const lapelMaterial = surface(coatColor.clone().multiplyScalar(1.4), { side: THREE.DoubleSide });
  for (const side of [-1, 1]) {
    rawMesh([[side * 0.72, 5.35 + data.bodyShift, 1.06],
      [side * 0.13, 5.62 + data.bodyShift, 0.91],
      [side * 0.30, 3.82 + data.bodyShift, 1.71]], [0, 1, 2], lapelMaterial);
  }
  owned.materials.add(lapelMaterial);
}

export function createHuman(data) {
  const appearance = normalizeAppearance(data);
  const group = new THREE.Group();
  const visual = new THREE.Group();
  group.add(visual);
  const owned = { objects: [], geometries: new Set(), materials: new Set() };
  const rig = makeRig(visual, appearance.male);
  const geometries = humanGeometries(appearance);
  const texture = skinTexture(appearance);
  const skin = surface('#ffffff', { side: THREE.DoubleSide, roughness: 0.76 });
  skin.map = texture;
  const shirt = surface(PALETTES.shirt[appearance.shirt]);
  const pants = surface(PALETTES.pants[appearance.pants]);
  const boots = surface('#171c20', { roughness: 0.62 });
  const coat = surface(new THREE.Color(PALETTES.shirt[appearance.shirt]).multiplyScalar(0.82), { side: THREE.DoubleSide, roughness: 0.9 });
  shirt.map = pants.map = coat.map = fabricTexture();
  const sleeve = appearance.coat || appearance.shirt % 2 === 0 ? (appearance.coat ? coat : shirt) : skin;
  const torsoMaterial = shirt.clone();
  const body = skinned(visual, geometries.body, [skin, shirt, sleeve, pants, boots, torsoMaterial], rig.skeleton, owned);
  const head = skinned(visual, geometries.head, skin, rig.skeleton, owned);
  const headParts = [head];
  let hair = null;
  if (geometries.hair) {
    const hairMaterial = surface(PALETTES.hairColor[appearance.hairColor], { vertexColors: true, roughness: 0.68, side: THREE.DoubleSide });
    hair = skinned(visual, geometries.hair, hairMaterial, rig.skeleton, owned);
    owned.materials.add(hairMaterial);
    headParts.push(hair);
  }
  let coatMesh = null;
  if (geometries.coat) coatMesh = skinned(visual, geometries.coat, coat, rig.skeleton, owned);
  const view = { group, visual, rig, body, head, hair, coatMesh, headParts, coatDetails: [], coatSkirt: null,
    appearance, texture, owned, belt: null, eyeParts: [], sunglasses: [], torsoMaterial };
  view.eyeParts = addEyes(rig, appearance, owned);
  headParts.push(...view.eyeParts);
  if (appearance.sunglasses) {
    for (const eye of view.eyeParts) eye.visible = false;
    view.sunglasses = addSunglasses(rig, appearance, owned);
    headParts.push(...view.sunglasses);
  }
  addShoes(rig, appearance.male, owned);
  addClothDetails(view, appearance, owned);
  owned.materials.add(skin);
  owned.materials.add(shirt);
  owned.materials.add(torsoMaterial);
  owned.materials.add(pants);
  owned.materials.add(boots);
  owned.materials.add(coat);
  return view;
}

export function resetPose(view) {
  for (const bone of view.rig.bones) {
    bone.quaternion.copy(bone.userData.neutral);
    bone.position.copy(bone.userData.restPosition);
    bone.scale.setScalar(1);
  }
}

export function rotateBone(view, name, axis, angle) {
  const bone = view.rig.byName[name];
  if (!bone || Math.abs(angle) < 0.0001) return;
  const localAxis = axis.clone().applyQuaternion(bone.userData.parentNeutral.clone().invert()).normalize();
  bone.quaternion.multiply(new THREE.Quaternion().setFromAxisAngle(localAxis, angle));
}

export function disposeHuman(scene, view) {
  scene.remove(view.group);
  for (const material of view.owned.materials) material.dispose();
  for (const geometry of view.owned.geometries) geometry.dispose();
  view.texture.dispose();
  view.rig.skeleton.dispose();
}
