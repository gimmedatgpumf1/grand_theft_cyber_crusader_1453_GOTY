import * as THREE from './vendor/three.module.min.js';

function material(color, roughness = 0.85) {
  return new THREE.MeshStandardMaterial({ color, roughness, flatShading: true });
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
  sprite.position.y = 2.28;
  sprite.renderOrder = 10;
  return sprite;
}

function faceTexture(variation) {
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 64;
  const context = canvas.getContext('2d');
  context.clearRect(0, 0, 64, 64);
  context.fillStyle = '#3a2921';
  context.fillRect(10, 15, 16, 3);
  context.fillRect(38, 15, 16, 3);
  context.fillStyle = '#eee7d2';
  context.fillRect(11, 21, 14, 7);
  context.fillRect(39, 21, 14, 7);
  context.fillStyle = variation % 2 ? '#334b45' : '#49382c';
  context.fillRect(16, 22, 5, 6);
  context.fillRect(43, 22, 5, 6);
  context.fillStyle = '#111713';
  context.fillRect(18, 23, 3, 5);
  context.fillRect(43, 23, 3, 5);
  context.fillStyle = 'rgba(74,43,32,.5)';
  context.fillRect(29, 29, 4, 10);
  context.fillRect(33, 37, 6, 2);
  context.fillStyle = '#633e35';
  context.fillRect(23, 46, 19, 2);
  context.fillRect(27, 49, 12, 2);
  context.fillStyle = 'rgba(238,174,143,.35)';
  context.fillRect(8, 35, 8, 3);
  context.fillRect(49, 35, 8, 3);
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.magFilter = THREE.NearestFilter;
  texture.minFilter = THREE.NearestFilter;
  texture.generateMipmaps = false;
  return texture;
}

function arm(shirt, skin) {
  const shoulder = new THREE.Group();
  const upper = new THREE.Mesh(new THREE.CylinderGeometry(0.065, 0.085, 0.3, 6), material(shirt));
  upper.position.y = -0.15;
  upper.castShadow = true;
  shoulder.add(upper);
  const elbow = new THREE.Group();
  elbow.position.y = -0.3;
  const forearm = new THREE.Mesh(new THREE.CylinderGeometry(0.052, 0.067, 0.28, 6), material(skin));
  forearm.position.y = -0.14;
  forearm.castShadow = true;
  elbow.add(forearm);
  const hand = new THREE.Mesh(new THREE.DodecahedronGeometry(0.072, 0), material(skin));
  hand.scale.set(0.75, 1.15, 0.65);
  hand.position.y = -0.31;
  hand.castShadow = true;
  elbow.add(hand);
  shoulder.add(elbow);
  return { shoulder, elbow };
}

function leg(trousers) {
  const hip = new THREE.Group();
  const upper = new THREE.Mesh(new THREE.CylinderGeometry(0.085, 0.115, 0.38, 6), material(trousers));
  upper.position.y = -0.19;
  upper.castShadow = true;
  hip.add(upper);
  const knee = new THREE.Group();
  knee.position.y = -0.38;
  const lower = new THREE.Mesh(new THREE.CylinderGeometry(0.065, 0.09, 0.38, 6), material(trousers));
  lower.position.y = -0.19;
  lower.castShadow = true;
  knee.add(lower);
  const boot = new THREE.Mesh(new THREE.BoxGeometry(0.17, 0.12, 0.3), material('#171c18'));
  boot.position.set(0, -0.36, 0.07);
  boot.castShadow = true;
  knee.add(boot);
  hip.add(knee);
  return { hip, knee };
}

function createView(data, scene) {
  const group = new THREE.Group();
  const visual = new THREE.Group();
  group.add(visual);
  const skinTones = ['#b98b69', '#d0a17d', '#8b654c', '#c48f70'];
  const skin = skinTones[data.id % skinTones.length];
  const trousers = data.id % 2 ? '#28352f' : '#30343b';
  const shirt = data.color;
  const torso = new THREE.Mesh(new THREE.CylinderGeometry(0.33, 0.25, 0.64, 7, 1, true), material(shirt));
  torso.position.y = 1.17;
  torso.scale.z = 0.64;
  torso.castShadow = true;
  visual.add(torso);
  const pelvis = new THREE.Mesh(new THREE.CylinderGeometry(0.25, 0.27, 0.24, 7, 1, true), material(trousers));
  pelvis.position.y = 0.78;
  pelvis.scale.z = 0.66;
  pelvis.castShadow = true;
  visual.add(pelvis);
  const belt = new THREE.Mesh(new THREE.CylinderGeometry(0.265, 0.265, 0.065, 7, 1, true), material('#171c18'));
  belt.position.y = 0.88;
  belt.scale.z = 0.67;
  visual.add(belt);
  const neck = new THREE.Mesh(new THREE.CylinderGeometry(0.085, 0.1, 0.17, 7), material(skin));
  neck.position.y = 1.52;
  neck.castShadow = true;
  visual.add(neck);

  const headParts = [neck];
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.26, 8, 6), material(skin));
  head.position.y = 1.79;
  head.scale.set(0.86, 1.1, 0.92);
  head.castShadow = true;
  visual.add(head);
  headParts.push(head);
  const face = new THREE.Mesh(new THREE.PlaneGeometry(0.34, 0.34),
    new THREE.MeshBasicMaterial({ map: faceTexture(data.id), transparent: true, depthWrite: false }));
  face.position.set(0, 1.79, 0.242);
  visual.add(face);
  headParts.push(face);
  const nose = new THREE.Mesh(new THREE.ConeGeometry(0.048, 0.14, 4), material(skin));
  nose.rotation.x = Math.PI / 2;
  nose.position.set(0, 1.77, 0.275);
  visual.add(nose);
  headParts.push(nose);
  for (const x of [-0.225, 0.225]) {
    const ear = new THREE.Mesh(new THREE.SphereGeometry(0.052, 5, 4), material(skin));
    ear.scale.set(0.55, 1, 0.65);
    ear.position.set(x, 1.79, 0);
    visual.add(ear);
    headParts.push(ear);
  }
  const hairColors = ['#2a211b', '#4a3423', '#596036', '#242a25'];
  const hair = new THREE.Mesh(new THREE.SphereGeometry(0.268, 8, 4, 0, Math.PI * 2, 0, Math.PI * 0.53), material(hairColors[data.id % 4]));
  hair.position.y = 1.81;
  hair.scale.set(0.87, 1.04, 0.94);
  hair.castShadow = true;
  visual.add(hair);
  headParts.push(hair);

  const leftLeg = leg(trousers);
  const rightLeg = leg(trousers);
  leftLeg.hip.position.set(-0.15, 0.78, 0);
  rightLeg.hip.position.set(0.15, 0.78, 0);
  visual.add(leftLeg.hip, rightLeg.hip);
  const leftArm = arm(shirt, skin);
  const rightArm = arm(shirt, skin);
  leftArm.shoulder.position.set(-0.33, 1.43, 0);
  rightArm.shoulder.position.set(0.33, 1.43, 0);
  leftArm.shoulder.rotation.z = -0.08;
  rightArm.shoulder.rotation.z = 0.08;
  visual.add(leftArm.shoulder, rightArm.shoulder);

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
  const droplets = new THREE.Points(streamGeometry, new THREE.PointsMaterial({ color: '#f4df53', size: 0.055, transparent: true, opacity: 0.9, depthTest: false }));
  stream.frustumCulled = droplets.frustumCulled = false;
  stream.renderOrder = droplets.renderOrder = 20;
  scene.add(stream, droplets);
  group.position.set(data.x, 0, data.z);
  scene.add(group);
  return { group, visual, torso, headParts, leftLeg, rightLeg, leftArm, rightArm, ring, label,
    stream, droplets, streamGeometry, target: new THREE.Vector3(data.x, 0, data.z),
    data, name: data.name, walkPhase: data.id * 0.7 };
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
      if (!view) {
        view = createView(data, this.scene);
        this.views.set(data.id, view);
      }
      view.data = data;
      view.target.set(data.x, 0, data.z);
      const local = data.id === localId;
      if (local) localData = data;
      const blend = 1 - Math.exp(-15 * dt);
      view.group.position.lerp(view.target, data.onToilet ? 1 : blend);
      const angleDelta = Math.atan2(Math.sin(data.yaw - view.group.rotation.y), Math.cos(data.yaw - view.group.rotation.y));
      if (data.onToilet) view.group.rotation.y = data.yaw;
      else view.group.rotation.y += angleDelta * blend;
      view.visual.visible = true;
      view.ring.visible = local && mode !== 'first' && !data.onToilet;
      const cameraDistance = view.group.position.distanceTo(camera.position);
      const hideHead = local && (mode === 'first' || cameraDistance < 0.85);
      for (const part of view.headParts) part.visible = !hideHead;
      const firstPersonBody = local && mode === 'first';
      const shoulderHeight = firstPersonBody ? 1.24 : 1.43;
      view.leftArm.shoulder.position.y = view.rightArm.shoulder.position.y = shoulderHeight;
      view.leftArm.shoulder.scale.setScalar(firstPersonBody ? 0.8 : 1);
      view.rightArm.shoulder.scale.setScalar(firstPersonBody ? 0.8 : 1);
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
      this.updateStream(view, data, local, mode, camera, time);
    }
    for (const [id, view] of this.views) {
      if (!alive.has(id)) {
        this.scene.remove(view.group, view.stream, view.droplets);
        this.views.delete(id);
      }
    }
    this.updatePath(localData, state.tick);
  }

  animate(view, data, time, dt) {
    view.walkPhase += dt * (4 + data.speed * 2.2);
    const walking = data.speed > 0.1 && !data.onToilet;
    const swing = walking ? Math.sin(view.walkPhase) * 0.55 : 0;
    const blend = 1 - Math.exp(-12 * dt);
    view.torso.scale.y = walking || data.onToilet ? 1 : 1 + Math.sin(time * 2 + data.id) * 0.012;
    if (data.onToilet) {
      view.visual.position.y = -0.05;
      view.leftLeg.hip.position.x = -0.23;
      view.rightLeg.hip.position.x = 0.23;
      for (const leg of [view.leftLeg, view.rightLeg]) {
        leg.hip.rotation.x = -Math.PI / 2;
        leg.knee.rotation.x = Math.PI / 2;
      }
      for (const arm of [view.leftArm, view.rightArm]) {
        arm.shoulder.rotation.x = -0.65;
        arm.elbow.rotation.x = -0.75;
      }
      view.torso.rotation.z = Math.sin(time * 9) * 0.018;
    } else {
      view.visual.position.y = walking ? Math.abs(Math.sin(view.walkPhase)) * 0.025 : 0;
      view.leftLeg.hip.position.x = -0.15;
      view.rightLeg.hip.position.x = 0.15;
      view.leftLeg.hip.rotation.x = THREE.MathUtils.lerp(view.leftLeg.hip.rotation.x, swing, blend);
      view.rightLeg.hip.rotation.x = THREE.MathUtils.lerp(view.rightLeg.hip.rotation.x, -swing, blend);
      view.leftLeg.knee.rotation.x = THREE.MathUtils.lerp(view.leftLeg.knee.rotation.x, Math.max(0, -swing) * 0.65, blend);
      view.rightLeg.knee.rotation.x = THREE.MathUtils.lerp(view.rightLeg.knee.rotation.x, Math.max(0, swing) * 0.65, blend);
      view.leftArm.shoulder.rotation.x = THREE.MathUtils.lerp(view.leftArm.shoulder.rotation.x, -swing * 0.7, blend);
      view.rightArm.shoulder.rotation.x = THREE.MathUtils.lerp(view.rightArm.shoulder.rotation.x, swing * 0.7, blend);
      view.leftArm.elbow.rotation.x = THREE.MathUtils.lerp(view.leftArm.elbow.rotation.x, -0.15 - Math.max(0, swing) * 0.25, blend);
      view.rightArm.elbow.rotation.x = THREE.MathUtils.lerp(view.rightArm.elbow.rotation.x, -0.15 - Math.max(0, -swing) * 0.25, blend);
      view.torso.rotation.z = data.pissing ? -0.04 : 0;
    }
  }

  updateStream(view, data, local, mode, camera, time) {
    view.stream.visible = view.droplets.visible = data.pissing;
    if (!data.pissing) return;
    const origin = new THREE.Vector3();
    const direction = new THREE.Vector3();
    if (local && mode === 'first') {
      camera.getWorldDirection(direction);
      origin.copy(camera.position).addScaledVector(direction, 0.18);
      const right = new THREE.Vector3(-direction.z, 0, direction.x).normalize();
      origin.addScaledVector(right, 0.18);
      origin.y -= 0.18;
    } else {
      direction.set(Math.sin(data.yaw) * Math.cos(data.pitch), Math.sin(data.pitch), Math.cos(data.yaw) * Math.cos(data.pitch));
      origin.copy(view.group.position).add(new THREE.Vector3(0, 1.12, 0));
      origin.addScaledVector(direction, 0.3);
    }
    const positions = view.streamGeometry.attributes.position;
    for (let i = 0; i < 20; ++i) {
      const t = (i / 19) * 0.58;
      const jitter = i === 0 ? 0 : Math.sin(time * 35 + i * 4.17) * 0.012;
      positions.setXYZ(i,
        origin.x + direction.x * 2.4 * t + jitter,
        Math.max(0.035, origin.y + direction.y * 2.4 * t - (local && mode === 'first' ? 4 : 2) * t * t),
        origin.z + direction.z * 2.4 * t - jitter);
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
