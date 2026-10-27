import * as THREE from './vendor/three.module.min.js';
import { humanData } from './vendor/human-data.js';
import { PALETTES, normalizeAppearance } from './appearance.js';
import { buildHumanGeometry } from './human-geometry.js';
import { addEyes, addSunglasses, addShoes, addClothDetails, addAccessories } from './human-parts.js';

const maskCache = new Map();
const tintCache = new Map();
const hairTextures = new Map();
const skinImages = new Map();
const skinSurfaceTextures = new Map();
let clothMap = null;

function skinImage(name, repaint) {
  if (skinImages.has(name)) {
    const cached = skinImages.get(name);
    if (cached.complete && cached.naturalWidth) repaint();
    else cached.addEventListener('load', repaint, { once: true });
    return cached;
  }
  const image = new Image();
  image.addEventListener('load', repaint, { once: true });
  image.src = `assets/human/${name}`;
  skinImages.set(name, image);
  return image;
}

function skinSurfaceTexture(name) {
  if (skinSurfaceTextures.has(name)) return skinSurfaceTextures.get(name);
  const texture = new THREE.TextureLoader().load(`assets/human/${name}`);
  texture.anisotropy = 4;
  skinSurfaceTextures.set(name, texture);
  return texture;
}

function hairTexture(path) {
  if (hairTextures.has(path)) return hairTextures.get(path);
  const texture = new THREE.TextureLoader().load(path);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = 4;
  hairTextures.set(path, texture);
  return texture;
}

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

function makeScalpMaterial(color, geometries, hairstyle) {
  const material = surface(color, { roughness: 0.9 });
  const uniforms = {
    eyeY: { value: geometries.eyes.L[1] },
    eyeZ: { value: geometries.eyes.L[2] },
    hairline: { value: hairstyle === 5 || hairstyle === 13 ? 0.070 : 0.085 },
    width: { value: Math.max(0.06, (geometries.headBounds.max.x - geometries.headBounds.min.x) * 0.5) },
  };
  material.onBeforeCompile = shader => {
    Object.assign(shader.uniforms, uniforms);
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', '#include <common>\nvarying vec3 vGtcScalp;')
      .replace('#include <begin_vertex>', '#include <begin_vertex>\nvGtcScalp = position;');
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', `#include <common>
varying vec3 vGtcScalp;
uniform float eyeY;
uniform float eyeZ;
uniform float hairline;
uniform float width;`)
      .replace('#include <clipping_planes_fragment>', `
float front = smoothstep(eyeZ * 0.45, eyeZ * 0.95, vGtcScalp.z);
float edge = eyeY + hairline + 0.018 * pow(clamp(abs(vGtcScalp.x) / width, 0.0, 1.5), 2.0);
if (vGtcScalp.y < mix(-100.0, edge, front)) discard;
#include <clipping_planes_fragment>`)
      .replace('#include <color_fragment>', `#include <color_fragment>
float strand = sin((vGtcScalp.x * 1.3 + vGtcScalp.z) * 540.0 + sin(vGtcScalp.y * 61.0) * 1.8);
diffuseColor.rgb *= 0.94 + 0.06 * strand;`);
  };
  material.customProgramCacheKey = () => 'gtc-scalp-v1';
  return material;
}

function mask(name, repaint) {
  if (maskCache.has(name)) {
    const cached = maskCache.get(name);
    if (cached.complete) repaint();
    else cached.addEventListener('load', repaint, { once: true });
    return cached;
  }
  const image = new Image();
  image.addEventListener('load', repaint, { once: true });
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

function paintBrow(context, inner, outer, arch, width) {
  const control = [(inner[0] + outer[0]) / 2 - 4, (inner[1] + outer[1]) / 2 - arch];
  const upper = [];
  const lower = [];
  const samples = [];
  for (let i = 0; i <= 18; ++i) {
    const t = i / 18;
    const u = 1 - t;
    const x = u * u * inner[0] + 2 * u * t * control[0] + t * t * outer[0];
    const y = u * u * inner[1] + 2 * u * t * control[1] + t * t * outer[1];
    const dx = 2 * u * (control[0] - inner[0]) + 2 * t * (outer[0] - control[0]);
    const dy = 2 * u * (control[1] - inner[1]) + 2 * t * (outer[1] - control[1]);
    const length = Math.hypot(dx, dy) || 1;
    const normal = [-dy / length, dx / length];
    const taper = (0.84 + Math.sin(Math.PI * t) * 0.24) * (1 - 0.90 * t ** 3);
    const half = width * taper * 0.5;
    upper.push([x + normal[0] * half, y + normal[1] * half]);
    lower.push([x - normal[0] * half, y - normal[1] * half]);
    samples.push({ x, y, normal, half });
  }
  context.beginPath();
  context.moveTo(...upper[0]);
  for (const point of upper.slice(1)) context.lineTo(...point);
  for (const point of lower.reverse()) context.lineTo(...point);
  context.closePath();
  context.fill();
  context.save();
  context.globalAlpha *= 0.42;
  context.lineWidth = 0.55;
  for (let i = 2; i < samples.length - 2; i += 2) {
    const sample = samples[i];
    context.beginPath();
    context.moveTo(sample.x - sample.normal[0] * sample.half * 0.8, sample.y - sample.normal[1] * sample.half * 0.8);
    context.lineTo(sample.x + sample.normal[0] * sample.half * 1.15 + 0.35,
      sample.y + sample.normal[1] * sample.half * 1.15);
    context.stroke();
  }
  context.restore();
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
  const sex = appearance.male ? 'male' : 'female';
  const age = appearance.morphs[0] || 0;
  const oldBlend = Math.max(0, Math.min(1, (age - 24) / 58));
  const ageBlend = oldBlend * oldBlend * (3 - 2 * oldBlend);
  const young = skinImages.get(`skin-${sex}-young-detail.jpg`);
  const old = skinImages.get(`skin-${sex}-old-detail.jpg`);
  if (young?.complete && young.naturalWidth) {
    context.globalCompositeOperation = 'multiply';
    context.globalAlpha = (1 - ageBlend) * 0.94;
    context.drawImage(young, 0, 0, size, size);
  }
  if (old?.complete && old.naturalWidth) {
    context.globalCompositeOperation = 'multiply';
    context.globalAlpha = ageBlend * 0.94;
    context.drawImage(old, 0, 0, size, size);
  }
  context.globalCompositeOperation = 'source-over';
  context.globalAlpha = 1;
  for (let i = 0; i < 500; ++i) {
    const x = (Math.sin(i * 91.731 + appearance.skin) * 0.5 + 0.5) * size;
    const y = (Math.sin(i * 37.117 + 4.2) * 0.5 + 0.5) * size;
    context.fillStyle = i % 2 ? 'rgba(80,38,24,.025)' : 'rgba(255,235,205,.035)';
    context.fillRect(x, y, 1 + i % 3, 1 + (i * 7) % 3);
  }
  const cheekColor = appearance.makeup ? 'rgba(202,82,92,.18)' : appearance.skin > 3 ? 'rgba(155,78,58,.10)' : 'rgba(190,85,70,.12)';
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
  const ageDetail = Math.max(0, Math.min(1, ((appearance.morphs[0] || 0) - 32) / 45));
  if (ageDetail > 0) {
    context.save();
    context.lineCap = 'round';
    context.strokeStyle = `rgba(65,36,29,${0.15 * ageDetail})`;
    context.lineWidth = 1.05;
    const forehead = point('forehead');
    for (let line = 0; line < 3; ++line) {
      const x = forehead[0] - 18 + line * 7;
      context.beginPath();
      context.moveTo(x, forehead[1] - 25 + line * 2);
      context.quadraticCurveTo(x - 2.5, forehead[1], x, forehead[1] + 25 - line * 2);
      context.stroke();
    }
    context.strokeStyle = `rgba(65,36,29,${0.13 * ageDetail})`;
    for (const side of ['L', 'R']) {
      const eye = point(`eye${side}`);
      for (let line = 0; line < 2; ++line) {
        const x = eye[0] + 7 + line * 4;
        context.beginPath();
        context.moveTo(x, eye[1] - 10 + line * 2);
        context.quadraticCurveTo(x + 2.5, eye[1], x, eye[1] + 10 - line * 2);
        context.stroke();
      }
      const noseSide = point(`nose${side}`);
      const mouthSide = point(`mouth${side}`);
      const direction = side === 'L' ? 1 : -1;
      context.beginPath();
      context.moveTo(noseSide[0] + 2, noseSide[1]);
      context.quadraticCurveTo((noseSide[0] + mouthSide[0]) / 2 + 5,
        (noseSide[1] + mouthSide[1]) / 2 + direction * 3, mouthSide[0] - 2, mouthSide[1]);
      context.stroke();
    }
    context.restore();
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
    context.globalAlpha = appearance.makeup ? 0.62 : 0.30;
    context.drawImage(tintedMask(eyelids, appearance.makeup ? '#211923' : '#382820'), 0, 0, size, size);
  }
  const lips = maskCache.get('lips');
  const lipColor = appearance.makeup ? PALETTES.lips[appearance.lipColor] : appearance.male ? '#743f39' : '#9e4c58';
  if (lips?.complete) {
    context.globalAlpha = appearance.makeup ? 0.88 : appearance.male ? 0.22 : 0.34;
    context.drawImage(tintedMask(lips, lipColor), 0, 0, size, size);
  } else {
    const mouth = point('mouth');
    context.globalAlpha = 0.4;
    context.fillStyle = lipColor;
    context.beginPath();
    context.ellipse(mouth[0], mouth[1], 4, 27, 0, 0, Math.PI * 2);
    context.fill();
  }
  context.globalAlpha = appearance.makeup ? 0.76 : young?.naturalWidth ? 0.30 : 0.70;
  const browColor = appearance.hairColor >= 6 && appearance.hairColor <= 7 ? '#68503d' :
    appearance.hairColor === 2 ? '#493326' :
    appearance.hairColor === 8 || appearance.hairColor === 4 ? '#4e4b47' : PALETTES.hairColor[appearance.hairColor];
  context.strokeStyle = context.fillStyle = browColor;
  context.lineCap = 'round';
  for (const side of ['L', 'R']) {
    const inner = point(`browInner${side}`);
    const outer = point(`browOuter${side}`);
    const width = appearance.makeup === 4 ? 2.6 : appearance.makeup ? 3.8 : 4.2;
    const arch = appearance.makeup === 4 ? 12 : appearance.makeup ? 8 : 4;
    paintBrow(context, inner, outer, arch, width);
  }
  if (appearance.makeup === 4) {
    const mouth = point('mouth');
    const cheek = point('cheekR');
    context.globalAlpha = 0.85;
    context.fillStyle = '#3a2925';
    context.beginPath();
    context.arc(mouth[0] * 0.72 + cheek[0] * 0.28 + 5, mouth[1] * 0.72 + cheek[1] * 0.28, 3.2, 0, Math.PI * 2);
    context.fill();
  }
  context.globalAlpha = 1;
}

function skinTexture(appearance) {
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 1024;
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  let queued = false;
  const repaint = () => {
    if (queued) return;
    queued = true;
    queueMicrotask(() => {
      queued = false;
      paintSkin(canvas, appearance);
      texture.needsUpdate = true;
    });
  };
  mask('lips', repaint);
  mask('eyelids', repaint);
  const sex = appearance.male ? 'male' : 'female';
  skinImage(`skin-${sex}-young-detail.jpg`, repaint);
  skinImage(`skin-${sex}-old-detail.jpg`, repaint);
  repaint();
  return texture;
}

function makeRig(visual, data) {
  const bones = data.bones.map(() => new THREE.Bone());
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
  object.receiveShadow = false;
  object.frustumCulled = false;
  visual.add(object);
  object.bind(skeleton, new THREE.Matrix4());
  owned.objects.push(object);
  owned.geometries.add(geometry);
  return object;
}

export function createHuman(data) {
  const appearance = normalizeAppearance(data);
  const group = new THREE.Group();
  const visual = new THREE.Group();
  group.add(visual);
  const owned = { objects: [], geometries: new Set(), materials: new Set() };
  const geometries = buildHumanGeometry(appearance);
  const rig = makeRig(visual, geometries.data);
  const texture = skinTexture(appearance);
  const skin = surface('#ffffff', { side: THREE.DoubleSide, roughness: 0.96 });
  skin.map = texture;
  const sex = appearance.male ? 'male' : 'female';
  const textureAge = (appearance.morphs[0] || 0) >= 44 ? 'old' : 'young';
  skin.normalMap = skinSurfaceTexture(`skin-${sex}-${textureAge}-normal.png`);
  skin.normalScale.set(0.42, 0.42);
  skin.roughnessMap = skinSurfaceTexture(`skin-${sex}-${textureAge}-roughness.jpg`);
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
  const hairBoost = [1.6, 2.2, 2.0, 2.0, 2.8, 2.1, 4.2, 3.4, 2.6][appearance.hairColor] || 1.6;
  let scalp = null;
  if (geometries.scalp) {
    const scalpMaterial = makeScalpMaterial(
      new THREE.Color(PALETTES.hairColor[appearance.hairColor]).multiplyScalar(Math.min(1.25, hairBoost * 0.5)), geometries, appearance.hair);
    scalp = skinned(visual, geometries.scalp, scalpMaterial, rig.skeleton, owned);
    owned.materials.add(scalpMaterial);
    headParts.push(scalp);
  }
  let hair = null;
  if (geometries.hair) {
    const hairMaterial = surface(new THREE.Color(PALETTES.hairColor[appearance.hairColor]).multiplyScalar(hairBoost), { roughness: 0.72, side: THREE.DoubleSide });
    hairMaterial.map = hairTexture(geometries.hair.userData.hairTexture);
    hairMaterial.alphaTest = 0.32;
    hair = skinned(visual, geometries.hair, hairMaterial, rig.skeleton, owned);
    owned.materials.add(hairMaterial);
    headParts.push(hair);
  }
  let coatMesh = null;
  if (geometries.coat) coatMesh = skinned(visual, geometries.coat, coat, rig.skeleton, owned);
  const view = { group, visual, rig, body, head, scalp, hair, coatMesh, headParts, coatDetails: [], coatSkirt: null,
    appearance, texture, owned, belt: null, eyeParts: [], sunglasses: [], torsoMaterial,
    data: geometries.data, eyes: geometries.eyes, headBounds: geometries.headBounds, extraHeadParts: [] };
  view.eyeParts = addEyes(view, owned);
  headParts.push(...view.eyeParts);
  if (appearance.sunglasses) {
    for (const eye of view.eyeParts) eye.visible = false;
    view.sunglasses = addSunglasses(view, owned);
    headParts.push(...view.sunglasses);
  }
  addShoes(view, owned);
  addClothDetails(view, appearance, owned);
  addAccessories(view, appearance, owned);
  headParts.push(...view.extraHeadParts);
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
