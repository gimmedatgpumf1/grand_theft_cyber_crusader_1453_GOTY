import * as THREE from './vendor/three.module.min.js';

function material(color, roughness = 0.8) {
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

function limb(radius, length, color) {
  const pivot = new THREE.Group();
  const mesh = new THREE.Mesh(new THREE.CylinderGeometry(radius * 0.8, radius, length, 5), material(color));
  mesh.position.y = -length / 2;
  mesh.castShadow = true;
  pivot.add(mesh);
  return pivot;
}

function leg(color) {
  const hip = new THREE.Group();
  const upper = new THREE.Mesh(new THREE.CylinderGeometry(0.105, 0.13, 0.38, 5), material(color));
  upper.position.y = -0.19;
  upper.castShadow = true;
  hip.add(upper);
  const knee = new THREE.Group();
  knee.position.y = -0.38;
  const lower = new THREE.Mesh(new THREE.CylinderGeometry(0.09, 0.115, 0.38, 5), material(color));
  lower.position.y = -0.19;
  lower.castShadow = true;
  knee.add(lower);
  const boot = new THREE.Mesh(new THREE.BoxGeometry(0.24, 0.15, 0.37), material('#141d18'));
  boot.position.set(0, -0.365, 0.08);
  boot.castShadow = true;
  knee.add(boot);
  hip.add(knee);
  return { hip, knee };
}

function createView(data, scene) {
  const group = new THREE.Group();
  const visual = new THREE.Group();
  group.add(visual);
  const skinTones = ['#b9926f', '#d0aa82', '#8c684f', '#c39272'];
  const skin = skinTones[data.id % skinTones.length];
  const trousers = '#263a35';
  const torso = new THREE.Mesh(new THREE.CylinderGeometry(0.31, 0.41, 0.72, 5, 1, true), material(data.color));
  torso.position.y = 1.18;
  torso.castShadow = true;
  visual.add(torso);
  const belt = new THREE.Mesh(new THREE.CylinderGeometry(0.39, 0.39, 0.11, 5, 1, true), material('#17231e'));
  belt.position.y = 0.82;
  visual.add(belt);
  const headParts = [];
  const head = new THREE.Mesh(new THREE.DodecahedronGeometry(0.29, 0), material(skin));
  head.position.y = 1.79;
  head.castShadow = true;
  visual.add(head);
  headParts.push(head);
  const nose = new THREE.Mesh(new THREE.ConeGeometry(0.075, 0.19, 5), material(skin));
  nose.rotation.x = Math.PI / 2;
  nose.position.set(0, 1.77, 0.31);
  visual.add(nose);
  headParts.push(nose);
  for (const x of [-0.105, 0.105]) {
    const eye = new THREE.Mesh(new THREE.BoxGeometry(0.055, 0.045, 0.035), material('#101610'));
    eye.position.set(x, 1.86, 0.265);
    visual.add(eye);
    headParts.push(eye);
  }
  const hair = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.22, 0.38), material(data.id % 2 ? '#342a21' : '#66703c'));
  hair.position.set(0, 2.04, -0.02);
  hair.rotation.x = -0.16;
  visual.add(hair);
  headParts.push(hair);
  const leftLeg = leg(trousers);
  const rightLeg = leg(trousers);
  leftLeg.hip.position.set(-0.2, 0.82, 0);
  rightLeg.hip.position.set(0.2, 0.82, 0);
  visual.add(leftLeg.hip, rightLeg.hip);
  const leftArm = limb(0.1, 0.68, data.color);
  const rightArm = limb(0.1, 0.68, data.color);
  leftArm.position.set(-0.4, 1.47, 0);
  rightArm.position.set(0.4, 1.47, 0);
  leftArm.rotation.z = -0.12;
  rightArm.rotation.z = 0.12;
  visual.add(leftArm, rightArm);
  const ring = new THREE.Mesh(new THREE.RingGeometry(0.43, 0.49, 24),
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
    data, name: data.name, walkPhase: 0 };
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
    const swing = walking ? Math.sin(view.walkPhase) * 0.65 : 0;
    const blend = 1 - Math.exp(-12 * dt);
    if (data.onToilet) {
      view.visual.position.y = -0.12;
      view.leftLeg.hip.position.x = -0.28;
      view.rightLeg.hip.position.x = 0.28;
      for (const leg of [view.leftLeg, view.rightLeg]) {
        leg.hip.rotation.x = -Math.PI / 2;
        leg.knee.rotation.x = Math.PI / 2;
      }
      view.leftArm.rotation.x = -0.9;
      view.rightArm.rotation.x = -0.9;
      view.torso.rotation.z = Math.sin(time * 9) * 0.025;
    } else {
      view.visual.position.y = walking ? Math.abs(Math.sin(view.walkPhase)) * 0.035 : 0;
      view.leftLeg.hip.position.x = -0.2;
      view.rightLeg.hip.position.x = 0.2;
      view.leftLeg.hip.rotation.x = THREE.MathUtils.lerp(view.leftLeg.hip.rotation.x, swing, blend);
      view.rightLeg.hip.rotation.x = THREE.MathUtils.lerp(view.rightLeg.hip.rotation.x, -swing, blend);
      view.leftLeg.knee.rotation.x = THREE.MathUtils.lerp(view.leftLeg.knee.rotation.x, Math.max(0, -swing) * 0.65, blend);
      view.rightLeg.knee.rotation.x = THREE.MathUtils.lerp(view.rightLeg.knee.rotation.x, Math.max(0, swing) * 0.65, blend);
      view.leftArm.rotation.x = THREE.MathUtils.lerp(view.leftArm.rotation.x, -swing * 0.75, blend);
      view.rightArm.rotation.x = THREE.MathUtils.lerp(view.rightArm.rotation.x, swing * 0.75, blend);
      view.torso.rotation.z = data.pissing ? -0.06 : 0;
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
