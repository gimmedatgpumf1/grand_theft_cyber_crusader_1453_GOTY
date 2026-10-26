import * as THREE from './vendor/three.module.min.js';
import { PALETTES, appearanceKey, normalizeAppearance } from './appearance.js';

function material(color, options = {}) {
  return new THREE.MeshStandardMaterial({
    color, roughness: options.roughness ?? 0.78, metalness: options.metalness ?? 0,
    flatShading: options.flat ?? true, side: options.side ?? THREE.FrontSide,
    transparent: options.transparent ?? false, opacity: options.opacity ?? 1,
  });
}

function mesh(parent, geometry, surface, position = [0, 0, 0], scale = null) {
  const object = new THREE.Mesh(geometry, surface);
  object.position.set(...position);
  if (scale) object.scale.set(...scale);
  object.castShadow = true;
  object.receiveShadow = true;
  parent.add(object);
  return object;
}

function ellipsoid(parent, surface, position, scale, segments = 12, rings = 8) {
  return mesh(parent, new THREE.SphereGeometry(1, segments, rings), surface, position, scale);
}

function box(parent, surface, position, scale) {
  return mesh(parent, new THREE.BoxGeometry(1, 1, 1), surface, position, scale);
}

function loftGeometry(rings, segments = 12, caps = true, arcStart = 0, arcLength = Math.PI * 2) {
  const positions = [];
  const indices = [];
  for (const ring of rings) {
    for (let i = 0; i <= segments; ++i) {
      const angle = arcStart + arcLength * i / segments;
      positions.push((ring.x || 0) + Math.cos(angle) * ring.rx, ring.y,
        (ring.z || 0) + Math.sin(angle) * ring.rz);
    }
  }
  const stride = segments + 1;
  for (let r = 0; r < rings.length - 1; ++r) {
    for (let i = 0; i < segments; ++i) {
      const a = r * stride + i;
      const b = a + 1;
      const c = a + stride;
      const d = c + 1;
      indices.push(a, c, b, b, c, d);
    }
  }
  if (caps) {
    const bottom = positions.length / 3;
    positions.push(rings[0].x || 0, rings[0].y, rings[0].z || 0);
    const top = positions.length / 3;
    const last = rings[rings.length - 1];
    positions.push(last.x || 0, last.y, last.z || 0);
    const topStart = (rings.length - 1) * stride;
    for (let i = 0; i < segments; ++i) {
      indices.push(bottom, i, i + 1);
      indices.push(top, topStart + i + 1, topStart + i);
    }
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}

function panelGeometry(vertices) {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(vertices.flat(), 3));
  const indices = [];
  for (let i = 1; i < vertices.length - 1; ++i) indices.push(0, i, i + 1);
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}

function nameSprite(name, color, ai) {
  const canvas = document.createElement('canvas');
  canvas.width = 512;
  canvas.height = 80;
  const context = canvas.getContext('2d');
  context.fillStyle = 'rgba(7,12,8,.82)';
  context.fillRect(28, 10, 456, 56);
  context.strokeStyle = color;
  context.lineWidth = 2;
  context.strokeRect(28, 10, 456, 56);
  context.font = '700 25px monospace';
  context.textAlign = 'center';
  context.textBaseline = 'middle';
  context.fillStyle = color;
  context.fillText(`${ai ? '◇ ' : ''}${name}`.slice(0, 25), 256, 39);
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: texture, transparent: true, depthTest: false }));
  sprite.scale.set(1.9, 0.3, 1);
  sprite.position.y = 2.34;
  sprite.renderOrder = 10;
  return sprite;
}

function makeArm(parent, side, shoulderX, sleeve, skin, longSleeve) {
  const shoulder = new THREE.Group();
  shoulder.position.set(side * shoulderX, 1.42, 0);
  parent.add(shoulder);
  ellipsoid(shoulder, sleeve, [0, -0.035, 0], [0.105, 0.105, 0.105], 10, 6);
  mesh(shoulder, loftGeometry([
    { y: -0.34, rx: 0.057, rz: 0.062 },
    { y: -0.19, rx: 0.074, rz: 0.078 },
    { y: 0, rx: 0.092, rz: 0.094 },
  ], 10), sleeve);
  const elbow = new THREE.Group();
  elbow.position.y = -0.34;
  shoulder.add(elbow);
  ellipsoid(elbow, longSleeve ? sleeve : skin, [0, 0, 0], [0.066, 0.06, 0.066], 10, 6);
  mesh(elbow, loftGeometry([
    { y: -0.31, rx: 0.043, rz: 0.047 },
    { y: -0.18, rx: 0.061, rz: 0.064 },
    { y: 0, rx: 0.068, rz: 0.071 },
  ], 10), longSleeve ? sleeve : skin);
  const hand = new THREE.Group();
  hand.position.y = -0.33;
  elbow.add(hand);
  ellipsoid(hand, skin, [0, -0.035, 0.008], [0.058, 0.085, 0.04], 10, 6);
  ellipsoid(hand, skin, [side * -0.052, -0.015, 0.025], [0.022, 0.052, 0.024], 8, 5);
  for (let i = 0; i < 3; ++i) {
    ellipsoid(hand, skin, [(i - 1) * 0.027, -0.098, 0.012], [0.015, 0.047, 0.019], 7, 5);
  }
  return { shoulder, elbow, hand };
}

function makeLeg(parent, side, hipX, trousers, bootMaterial) {
  const hip = new THREE.Group();
  hip.position.set(side * hipX, 0.86, 0);
  parent.add(hip);
  mesh(hip, loftGeometry([
    { y: -0.4, rx: 0.073, rz: 0.08 },
    { y: -0.22, rx: 0.098, rz: 0.104 },
    { y: 0, rx: 0.12, rz: 0.125 },
  ], 10), trousers);
  const knee = new THREE.Group();
  knee.position.y = -0.4;
  hip.add(knee);
  ellipsoid(knee, trousers, [0, 0, 0.018], [0.077, 0.068, 0.083], 10, 6);
  mesh(knee, loftGeometry([
    { y: -0.36, rx: 0.061, rz: 0.066 },
    { y: -0.2, rx: 0.079, rz: 0.085 },
    { y: 0, rx: 0.074, rz: 0.078 },
  ], 10), trousers);
  ellipsoid(knee, bootMaterial, [0, -0.345, 0], [0.076, 0.11, 0.082], 10, 6);
  ellipsoid(knee, bootMaterial, [0, -0.405, 0.085], [0.088, 0.065, 0.17], 10, 6);
  box(knee, bootMaterial, [0, -0.435, -0.025], [0.13, 0.055, 0.12]);
  return { hip, knee };
}

function noseGeometry() {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute([
    -0.022, 0.275, 0.176, 0.022, 0.275, 0.176,
    -0.036, 0.13, 0.18, 0.036, 0.13, 0.18,
    0, 0.155, 0.224, 0, 0.108, 0.172,
  ], 3));
  geometry.setIndex([0, 1, 4, 0, 4, 2, 1, 3, 4, 2, 4, 5, 4, 3, 5]);
  geometry.computeVertexNormals();
  return geometry;
}

function buildHead(parent, appearance, skin, hairColor) {
  const head = new THREE.Group();
  head.position.y = 1.55;
  if (!appearance.male) head.scale.set(0.95, 0.98, 0.95);
  parent.add(head);
  mesh(head, loftGeometry([
    { y: 0, rx: 0.08, rz: 0.09, z: 0.015 },
    { y: 0.055, rx: 0.145, rz: 0.14, z: 0.01 },
    { y: 0.16, rx: 0.202, rz: 0.172 },
    { y: 0.29, rx: 0.214, rz: 0.18, z: -0.006 },
    { y: 0.4, rx: 0.188, rz: 0.163, z: -0.015 },
    { y: 0.48, rx: 0.105, rz: 0.105, z: -0.02 },
    { y: 0.5, rx: 0.025, rz: 0.035, z: -0.02 },
  ], 18), skin);
  for (const side of [-1, 1]) {
    ellipsoid(head, skin, [side * 0.214, 0.215, -0.005], [0.035, 0.064, 0.026], 10, 6);
    ellipsoid(head, material('#7d5142'), [side * 0.226, 0.215, 0.008], [0.014, 0.036, 0.014], 8, 5);
  }
  const eyeWhite = material('#dedfd7', { flat: false, roughness: 0.35 });
  const iris = material(appearance.sunglasses ? '#20262b' : '#536d78', { flat: false, roughness: 0.3 });
  const eyeParts = [];
  for (const side of [-1, 1]) {
    eyeParts.push(ellipsoid(head, eyeWhite, [side * 0.079, 0.265, 0.174], [0.054, 0.027, 0.019], 12, 6));
    eyeParts.push(ellipsoid(head, iris, [side * 0.079, 0.265, 0.192], [0.018, 0.019, 0.008], 10, 6));
    const brow = box(head, hairColor, [side * 0.08, 0.312, 0.177], [0.09, 0.017, 0.018]);
    brow.rotation.z = side * -0.09;
  }
  mesh(head, noseGeometry(), skin);
  const nostril = material('#4c322b');
  for (const side of [-1, 1]) ellipsoid(head, nostril, [side * 0.021, 0.127, 0.204], [0.008, 0.005, 0.007], 7, 4);
  const lip = material(appearance.male ? '#805247' : '#a15f60', { flat: false });
  ellipsoid(head, lip, [0, 0.073, 0.162], [0.066, 0.011, 0.014], 12, 5);
  ellipsoid(head, lip, [0, 0.058, 0.158], [0.058, 0.013, 0.013], 12, 5);

  if (appearance.beard > 0) {
    const beard = material(PALETTES.hairColor[appearance.hairColor], {
      transparent: appearance.beard === 1, opacity: appearance.beard === 1 ? 0.48 : 1,
    });
    ellipsoid(head, beard, [0, 0.025, 0.13], [0.072, 0.047, 0.022], 12, 7);
    for (const side of [-1, 1]) {
      ellipsoid(head, beard, [side * 0.036, 0.102, 0.174], [0.045, 0.012, 0.015], 10, 5);
      if (appearance.beard === 1) ellipsoid(head, beard, [side * 0.13, 0.09, 0.105], [0.055, 0.09, 0.035], 10, 6);
    }
  }

  if (appearance.hair > 0) {
    const capScale = appearance.hair === 1 ? [0.215, 0.15, 0.18] : [0.225, 0.18, 0.19];
    const cap = mesh(head, new THREE.SphereGeometry(1, 14, 7, 0, Math.PI * 2, 0, Math.PI * 0.5), hairColor,
      [0, 0.38, -0.018], capScale);
    cap.rotation.z = appearance.hair === 2 ? -0.08 : 0;
    if (appearance.hair === 2 || appearance.hair === 4) {
      for (let i = 0; i < 4; ++i) {
        const lock = ellipsoid(head, hairColor, [-0.11 + i * 0.065, 0.36 - i * 0.012, 0.14],
          [0.065, 0.045, 0.055], 8, 5);
        lock.rotation.z = appearance.hair === 2 ? -0.3 : 0.35;
      }
    }
    if (appearance.hair === 3) {
      ellipsoid(head, hairColor, [0, 0.22, -0.105], [0.2, 0.23, 0.11], 12, 7);
      for (const side of [-1, 1]) ellipsoid(head, hairColor, [side * 0.195, 0.18, -0.015], [0.055, 0.2, 0.105], 10, 7);
    }
    for (const side of [-1, 1]) box(head, hairColor, [side * 0.19, 0.245, -0.01], [0.025, 0.12, 0.055]);
  }

  if (appearance.sunglasses) {
    for (const part of eyeParts) part.visible = false;
    const frame = material('#101417', { metalness: 0.45, roughness: 0.25 });
    const lens = material('#243842', { metalness: 0.35, roughness: 0.18 });
    for (const side of [-1, 1]) {
      ellipsoid(head, lens, [side * 0.078, 0.267, 0.207], [0.066, 0.035, 0.013], 12, 6);
      const rim = mesh(head, new THREE.TorusGeometry(0.052, 0.007, 5, 12), frame, [side * 0.078, 0.267, 0.216], [1.22, 0.68, 1]);
      rim.castShadow = false;
      box(head, frame, [side * 0.164, 0.274, 0.135], [0.14, 0.012, 0.014]).rotation.y = side * 0.55;
    }
    box(head, frame, [0, 0.273, 0.215], [0.052, 0.011, 0.012]);
  }
  return head;
}

function createView(data, scene) {
  const appearance = normalizeAppearance(data);
  const group = new THREE.Group();
  const visual = new THREE.Group();
  group.add(visual);
  const skin = material(PALETTES.skin[appearance.skin], { flat: false, side: THREE.DoubleSide });
  const shirt = material(PALETTES.shirt[appearance.shirt]);
  const trousers = material(PALETTES.pants[appearance.pants]);
  const hairColor = material(PALETTES.hairColor[appearance.hairColor]);
  const boot = material('#151a1d', { roughness: 0.6 });
  const coatColor = new THREE.Color(PALETTES.shirt[appearance.shirt]).multiplyScalar(0.92);
  const coat = material(coatColor, { side: THREE.DoubleSide });
  const chest = appearance.coat ? coat : shirt;

  const pelvis = mesh(visual, loftGeometry([
    { y: 0.7, rx: appearance.male ? 0.2 : 0.23, rz: 0.135 },
    { y: 0.83, rx: appearance.male ? 0.25 : 0.28, rz: 0.16 },
    { y: 0.96, rx: 0.23, rz: 0.14 },
  ], 12), trousers);
  const torso = mesh(visual, loftGeometry([
    { y: 0.9, rx: 0.22, rz: 0.13 },
    { y: 1.05, rx: appearance.male ? 0.22 : 0.205, rz: 0.13 },
    { y: 1.28, rx: appearance.male ? 0.31 : 0.285, rz: appearance.male ? 0.17 : 0.18 },
    { y: 1.42, rx: appearance.male ? 0.345 : 0.31, rz: 0.16 },
    { y: 1.49, rx: 0.19, rz: 0.115 },
  ], 14), chest);
  const chestMeshes = [torso];
  const belt = mesh(visual, loftGeometry([{ y: 0.88, rx: 0.245, rz: 0.15 }, { y: 0.925, rx: 0.24, rz: 0.15 }], 12), boot);
  box(visual, material('#7f8b83', { metalness: 0.55 }), [0, 0.905, 0.155], [0.07, 0.045, 0.018]);

  let coatSkirt = null;
  const coatDetails = [];
  if (appearance.coat) {
    const inner = material(PALETTES.shirt[appearance.shirt], { side: THREE.DoubleSide });
    coatDetails.push(mesh(visual, panelGeometry([[-0.14, 1.02, 0.174], [0.14, 1.02, 0.174], [0.12, 1.47, 0.174], [-0.12, 1.47, 0.174]]), inner));
    const lapel = material(coatColor.clone().multiplyScalar(1.35), { side: THREE.DoubleSide });
    coatDetails.push(mesh(visual, panelGeometry([[-0.3, 1.43, 0.165], [-0.055, 1.48, 0.19], [-0.13, 1.15, 0.19]]), lapel));
    coatDetails.push(mesh(visual, panelGeometry([[0.3, 1.43, 0.165], [0.055, 1.48, 0.19], [0.13, 1.15, 0.19]]), lapel));
    coatSkirt = mesh(visual, loftGeometry([
      { y: 0.52, rx: 0.39, rz: 0.23 }, { y: 0.78, rx: 0.33, rz: 0.2 }, { y: 1.04, rx: 0.25, rz: 0.16 },
    ], 14, false, Math.PI / 2 + 0.38, Math.PI * 2 - 0.76), coat);
    const collar = mesh(visual, new THREE.TorusGeometry(0.135, 0.035, 6, 14), coat, [0, 1.5, 0]);
    collar.rotation.x = Math.PI / 2;
    coatDetails.push(collar);
  }

  const neck = ellipsoid(visual, skin, [0, 1.535, 0], [0.09, 0.105, 0.085], 12, 7);
  const head = buildHead(visual, appearance, skin, hairColor);
  const hipX = appearance.male ? 0.145 : 0.16;
  const leftLeg = makeLeg(visual, -1, hipX, trousers, boot);
  const rightLeg = makeLeg(visual, 1, hipX, trousers, boot);
  const shoulderX = appearance.male ? 0.35 : 0.315;
  const leftArm = makeArm(visual, -1, shoulderX, chest, skin, appearance.coat);
  const rightArm = makeArm(visual, 1, shoulderX, chest, skin, appearance.coat);
  leftArm.shoulder.rotation.z = -0.055;
  rightArm.shoulder.rotation.z = 0.055;

  const ring = new THREE.Mesh(new THREE.RingGeometry(0.39, 0.44, 24),
    new THREE.MeshBasicMaterial({ color: '#d7ff54', transparent: true, opacity: 0.8, side: THREE.DoubleSide, depthWrite: false }));
  ring.rotation.x = -Math.PI / 2;
  ring.position.y = 0.025;
  group.add(ring);
  const label = nameSprite(data.name, data.color, data.ai);
  group.add(label);
  const streamGeometry = new THREE.BufferGeometry();
  streamGeometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(20 * 3), 3));
  const stream = new THREE.Line(streamGeometry, new THREE.LineBasicMaterial({ color: '#fff074', transparent: true, opacity: 0.9, depthTest: false }));
  const droplets = new THREE.Points(streamGeometry, new THREE.PointsMaterial({ color: '#f4df53', size: 0.045, transparent: true, opacity: 0.9, depthTest: false }));
  stream.frustumCulled = droplets.frustumCulled = false;
  stream.renderOrder = droplets.renderOrder = 20;
  scene.add(stream, droplets);
  group.position.set(data.x, 0, data.z);
  scene.add(group);
  return { group, visual, torso, pelvis, chestMeshes, coatSkirt, coatDetails, neck, head, leftLeg, rightLeg, leftArm, rightArm,
    ring, label, stream, droplets, streamGeometry, target: new THREE.Vector3(data.x, 0, data.z),
    data, name: data.name, appearanceKey: appearanceKey(data), walkPhase: data.id * 0.7 };
}

function disposeView(scene, view) {
  const geometries = new Set();
  const materials = new Set();
  view.group.traverse(object => {
    if (object.geometry) geometries.add(object.geometry);
    if (object.material) {
      for (const surface of Array.isArray(object.material) ? object.material : [object.material]) materials.add(surface);
    }
  });
  for (const geometry of geometries) geometry.dispose();
  for (const surface of materials) {
    surface.map?.dispose();
    surface.dispose();
  }
  view.streamGeometry.dispose();
  view.stream.material.dispose();
  view.droplets.material.dispose();
  scene.remove(view.group, view.stream, view.droplets);
}

export class CharacterManager {
  constructor(scene) {
    this.scene = scene;
    this.views = new Map();
    this.pathLine = null;
    this.pathTick = -1;
  }

  sync(state, localId, mode, camera, time, dt) {
    const alive = new Set();
    let localData = null;
    for (const data of state.characters) {
      alive.add(data.id);
      let view = this.views.get(data.id);
      if (view && view.appearanceKey !== appearanceKey(data)) {
        const position = view.group.position.clone();
        const yaw = view.group.rotation.y;
        disposeView(this.scene, view);
        view = createView(data, this.scene);
        view.group.position.copy(position);
        view.group.rotation.y = yaw;
        this.views.set(data.id, view);
      }
      if (!view) {
        view = createView(data, this.scene);
        this.views.set(data.id, view);
      }
      view.data = data;
      view.target.set(data.x, 0, data.z);
      const local = data.id === localId;
      if (local) localData = data;
      const blend = 1 - Math.exp(-15 * dt);
      view.group.position.lerp(view.target, data.onToilet || data.toiletAction === 'piss' ? 1 : blend);
      const angleDelta = Math.atan2(Math.sin(data.yaw - view.group.rotation.y), Math.cos(data.yaw - view.group.rotation.y));
      if (data.toiletAction !== 'none') view.group.rotation.y = data.yaw;
      else view.group.rotation.y += angleDelta * blend;
      const cameraDistance = view.group.position.distanceTo(camera.position);
      const hideHead = local && (mode === 'first' || cameraDistance < 0.85);
      view.head.visible = !hideHead;
      view.neck.visible = !hideHead;
      const firstBody = local && mode === 'first';
      if (view.coatSkirt) view.coatSkirt.visible = !firstBody;
      for (const detail of view.coatDetails) detail.visible = !firstBody;
      for (const arm of [view.leftArm, view.rightArm]) {
        arm.shoulder.position.y = firstBody ? 1.24 : 1.42;
        arm.shoulder.scale.setScalar(firstBody ? 0.8 : 1);
      }
      view.ring.visible = local && mode !== 'first' && data.toiletAction === 'none';
      view.label.visible = !local && cameraDistance > 1.6;
      const labelScale = THREE.MathUtils.clamp(cameraDistance / 4, 0.55, 1);
      view.label.scale.set(1.9 * labelScale, 0.3 * labelScale, 1);
      if (view.name !== data.name || view.label.userData.ai !== data.ai) {
        view.group.remove(view.label);
        view.label.material.map.dispose();
        view.label.material.dispose();
        view.label = nameSprite(data.name, data.color, data.ai);
        view.label.userData.ai = data.ai;
        view.group.add(view.label);
        view.name = data.name;
      }
      this.animate(view, data, time, dt);
      const seated = data.onToilet && !(data.male && data.toiletAction === 'piss');
      if (firstBody && !seated) {
        view.leftArm.shoulder.rotation.x = -0.34;
        view.leftArm.elbow.rotation.x = -0.24;
        if (data.toiletAction !== 'piss') {
          view.rightArm.shoulder.rotation.x = -0.34;
          view.rightArm.elbow.rotation.x = -0.24;
        }
      }
      this.updateStream(view, data, local, mode, camera, time);
    }
    for (const [id, view] of this.views) {
      if (!alive.has(id)) {
        disposeView(this.scene, view);
        this.views.delete(id);
      }
    }
    this.updatePath(localData, state.tick);
  }

  animate(view, data, time, dt) {
    view.walkPhase += dt * (4 + data.speed * 2.2);
    const walking = data.speed > 0.1 && data.toiletAction === 'none';
    const swing = walking ? Math.sin(view.walkPhase) * 0.6 : 0;
    const blend = 1 - Math.exp(-12 * dt);
    for (const chest of view.chestMeshes) chest.scale.y = walking ? 1 : 1 + Math.sin(time * 2 + data.id) * 0.009;
    view.head.rotation.x = 0;
    view.head.rotation.y = walking ? 0 : Math.sin(time * 0.45 + data.id) * 0.045;
    const seated = data.onToilet && !(data.male && data.toiletAction === 'piss');
    if (seated) {
      view.visual.position.y = -0.1;
      view.leftLeg.hip.position.x = -0.23;
      view.rightLeg.hip.position.x = 0.23;
      for (const leg of [view.leftLeg, view.rightLeg]) {
        leg.hip.rotation.x = -Math.PI / 2;
        leg.knee.rotation.x = Math.PI / 2;
      }
      for (const arm of [view.leftArm, view.rightArm]) {
        arm.shoulder.rotation.x = data.toiletAction === 'shit' ? -0.8 : -0.55;
        arm.elbow.rotation.x = -0.7;
      }
      if (data.toiletAction === 'shit') view.head.rotation.x = 0.14;
      if (view.coatSkirt) view.coatSkirt.rotation.x = -0.08;
      return;
    }
    if (view.coatSkirt) view.coatSkirt.rotation.x = 0;
    view.visual.position.y = walking ? Math.abs(Math.sin(view.walkPhase)) * 0.025 : 0;
    view.leftLeg.hip.position.x = -(data.male ? 0.145 : 0.16);
    view.rightLeg.hip.position.x = data.male ? 0.145 : 0.16;
    view.leftLeg.hip.rotation.x = THREE.MathUtils.lerp(view.leftLeg.hip.rotation.x, swing, blend);
    view.rightLeg.hip.rotation.x = THREE.MathUtils.lerp(view.rightLeg.hip.rotation.x, -swing, blend);
    view.leftLeg.knee.rotation.x = THREE.MathUtils.lerp(view.leftLeg.knee.rotation.x, Math.max(0, -swing) * 0.7, blend);
    view.rightLeg.knee.rotation.x = THREE.MathUtils.lerp(view.rightLeg.knee.rotation.x, Math.max(0, swing) * 0.7, blend);
    view.leftArm.shoulder.rotation.x = THREE.MathUtils.lerp(view.leftArm.shoulder.rotation.x, -swing * 0.7, blend);
    view.rightArm.shoulder.rotation.x = THREE.MathUtils.lerp(view.rightArm.shoulder.rotation.x, swing * 0.7, blend);
    view.leftArm.elbow.rotation.x = THREE.MathUtils.lerp(view.leftArm.elbow.rotation.x, -0.12 - Math.max(0, swing) * 0.25, blend);
    view.rightArm.elbow.rotation.x = THREE.MathUtils.lerp(view.rightArm.elbow.rotation.x, -0.12 - Math.max(0, -swing) * 0.25, blend);
    if (data.toiletAction === 'piss') {
      view.rightArm.shoulder.rotation.x = -0.48;
      view.rightArm.elbow.rotation.x = -0.62;
    }
  }

  updateStream(view, data, local, mode, camera, time) {
    view.stream.visible = view.droplets.visible = data.pissing;
    if (!data.pissing) return;
    const origin = new THREE.Vector3();
    const direction = new THREE.Vector3();
    const toiletPiss = data.toiletAction === 'piss';
    if (local && mode === 'first' && !toiletPiss) {
      camera.getWorldDirection(direction);
      origin.copy(camera.position).addScaledVector(direction, 0.18);
      const right = new THREE.Vector3(-direction.z, 0, direction.x).normalize();
      origin.addScaledVector(right, 0.18);
      origin.y -= 0.18;
    } else {
      direction.set(Math.sin(data.yaw) * Math.cos(data.pitch), toiletPiss ? 0 : Math.sin(data.pitch), Math.cos(data.yaw) * Math.cos(data.pitch));
      origin.copy(view.group.position).add(new THREE.Vector3(0, 0.92, 0));
      origin.addScaledVector(direction, 0.22);
    }
    const positions = view.streamGeometry.attributes.position;
    for (let i = 0; i < 20; ++i) {
      const t = (i / 19) * 0.58;
      const jitter = i === 0 ? 0 : Math.sin(time * 35 + i * 4.17) * 0.009;
      positions.setXYZ(i,
        origin.x + direction.x * (toiletPiss ? 2.8 : 2.4) * t + jitter,
        Math.max(0.035, origin.y + direction.y * 2.4 * t - (local && mode === 'first' && !toiletPiss ? 4 : 2) * t * t),
        origin.z + direction.z * (toiletPiss ? 2.8 : 2.4) * t - jitter);
    }
    positions.needsUpdate = true;
  }

  updatePath(localData, tick) {
    if (!localData || localData.path.length === 0 || tick === this.pathTick) {
      if (!localData?.path.length && this.pathLine) this.pathLine.visible = false;
      return;
    }
    this.pathTick = tick;
    if (this.pathLine) {
      this.scene.remove(this.pathLine);
      this.pathLine.geometry.dispose();
      this.pathLine.material.dispose();
    }
    const points = [new THREE.Vector3(localData.x, 0.045, localData.z),
      ...localData.path.map(point => new THREE.Vector3(point[0], 0.045, point[1]))];
    this.pathLine = new THREE.Line(new THREE.BufferGeometry().setFromPoints(points),
      new THREE.LineDashedMaterial({ color: '#d7ff54', dashSize: 0.17, gapSize: 0.12, transparent: true, opacity: 0.6 }));
    this.pathLine.computeLineDistances();
    this.scene.add(this.pathLine);
  }

  localView(id) {
    return this.views.get(id) || null;
  }
}
