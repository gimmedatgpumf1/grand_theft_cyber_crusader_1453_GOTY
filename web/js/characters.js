import * as THREE from './vendor/three.module.min.js';
import { appearanceKey } from './appearance.js';
import { createHuman, disposeHuman, resetPose, rotateBone } from './human.js';

const X = new THREE.Vector3(1, 0, 0);
const Y = new THREE.Vector3(0, 1, 0);

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
  sprite.position.y = 2.08;
  sprite.renderOrder = 10;
  sprite.userData.ai = ai;
  return sprite;
}

function createView(data, scene) {
  const view = createHuman(data);
  const { group } = view;
  const ring = new THREE.Mesh(new THREE.RingGeometry(0.36, 0.41, 24),
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
  return Object.assign(view, { ring, label, stream, droplets, streamGeometry,
    target: new THREE.Vector3(data.x, 0, data.z), data, name: data.name,
    appearanceKey: appearanceKey(data), walkPhase: data.id * 0.7 });
}

function disposeView(scene, view) {
  view.ring.geometry.dispose();
  view.ring.material.dispose();
  view.label.material.map.dispose();
  view.label.material.dispose();
  view.streamGeometry.dispose();
  view.stream.material.dispose();
  view.droplets.material.dispose();
  scene.remove(view.stream, view.droplets);
  disposeHuman(scene, view);
}

export class CharacterManager {
  constructor(scene) {
    this.scene = scene;
    this.views = new Map();
    this.pathLine = null;
    this.pathTick = -1;
  }

  sync(state, localId, mode, camera, time, dt, creatorOpen = false) {
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
      const preview = creatorOpen && local;
      view.group.visible = !creatorOpen || local;
      if (local) localData = data;
      const blend = 1 - Math.exp(-15 * dt);
      view.group.position.lerp(view.target, data.onToilet || data.toiletAction === 'piss' ? 1 : blend);
      const angleDelta = Math.atan2(Math.sin(data.yaw - view.group.rotation.y), Math.cos(data.yaw - view.group.rotation.y));
      if (data.toiletAction !== 'none') view.group.rotation.y = data.yaw;
      else view.group.rotation.y += angleDelta * blend;
      const cameraDistance = view.group.position.distanceTo(camera.position);
      const hideHead = local && (mode === 'first' || cameraDistance < 0.85);
      for (const part of view.headParts) part.visible = !hideHead;
      if (view.appearance.sunglasses) for (const eye of view.eyeParts) eye.visible = false;
      const firstBody = local && mode === 'first';
      if (view.coatSkirt) view.coatSkirt.visible = !firstBody;
      for (const detail of view.coatDetails) detail.visible = !firstBody;
      view.ring.visible = local && (preview || (mode !== 'first' && data.toiletAction === 'none'));
      view.label.visible = !local && cameraDistance > 1.6;
      const labelScale = THREE.MathUtils.clamp(cameraDistance / 4, 0.55, 1);
      view.label.scale.set(1.9 * labelScale, 0.3 * labelScale, 1);
      if (view.name !== data.name || view.label.userData.ai !== data.ai) {
        view.group.remove(view.label);
        view.label.material.map.dispose();
        view.label.material.dispose();
        view.label = nameSprite(data.name, data.color, data.ai);
        view.group.add(view.label);
        view.name = data.name;
      }
      this.animate(view, preview ? { ...data, onToilet: false, toiletAction: 'none', speed: 0 } : data, time, dt, preview);
      const seated = !preview && data.onToilet && !(data.male && data.toiletAction === 'piss');
      view.torsoMaterial.visible = true;
      if (view.coatMesh) view.coatMesh.visible = !firstBody || seated;
      view.visual.position.z = firstBody && !seated ? -0.18 : 0;
      if (firstBody && !seated) {
        for (const suffix of ['L', 'R']) {
          const shoulder = view.rig.byName[`shoulder01.${suffix}`];
          shoulder.position.y = shoulder.userData.restPosition.y - 0.14;
          view.rig.byName[`upperarm01.${suffix}`].scale.setScalar(0.8);
          rotateBone(view, `upperarm01.${suffix}`, X, -0.34);
          if (suffix === 'L' || data.toiletAction !== 'piss') rotateBone(view, `lowerarm01.${suffix}`, X, -0.24);
        }
      }
      this.updateStream(view, data, local, mode, camera, time);
      if (creatorOpen && !local) view.stream.visible = view.droplets.visible = false;
      if (preview) view.stream.visible = view.droplets.visible = false;
    }
    for (const [id, view] of this.views) {
      if (!alive.has(id)) {
        disposeView(this.scene, view);
        this.views.delete(id);
      }
    }
    this.updatePath(localData, state.tick);
  }

  animate(view, data, time, dt, preview = false) {
    resetPose(view);
    view.walkPhase += dt * (4 + data.speed * 2.2);
    const walking = data.speed > 0.1 && data.toiletAction === 'none';
    const swing = walking ? Math.sin(view.walkPhase) * 0.52 : 0;
    const seated = data.onToilet && !(data.male && data.toiletAction === 'piss');
    if (view.coatSkirt) view.coatSkirt.rotation.x = seated ? -0.12 : 0;
    if (seated) {
      view.visual.position.y = -0.20;
      for (const [suffix, side] of [['L', 1], ['R', -1]]) {
        rotateBone(view, `upperleg01.${suffix}`, Y, side * 0.08);
        rotateBone(view, `upperleg01.${suffix}`, X, -Math.PI / 2);
        rotateBone(view, `lowerleg01.${suffix}`, X, Math.PI / 2);
        rotateBone(view, `upperarm01.${suffix}`, X, data.toiletAction === 'shit' ? -0.78 : -0.55);
        rotateBone(view, `lowerarm01.${suffix}`, X, -0.65);
      }
      if (data.toiletAction === 'shit') rotateBone(view, 'head', X, 0.14);
      return;
    }
    view.visual.position.y = walking ? Math.abs(Math.sin(view.walkPhase)) * 0.02 : 0;
    rotateBone(view, 'head', Y, walking || preview ? 0 : Math.sin(time * 0.45 + data.id) * 0.035);
    rotateBone(view, 'spine02', X, preview ? 0 : Math.sin(time * 2 + data.id) * 0.006);
    for (const [suffix, direction] of [['L', 1], ['R', -1]]) {
      const legSwing = swing * direction;
      rotateBone(view, `upperleg01.${suffix}`, X, legSwing);
      rotateBone(view, `lowerleg01.${suffix}`, X, Math.max(0, -legSwing) * 0.68);
      rotateBone(view, `upperarm01.${suffix}`, X, -legSwing * 0.68);
      rotateBone(view, `lowerarm01.${suffix}`, X, -0.12 - Math.max(0, legSwing) * 0.22);
    }
    if (data.toiletAction === 'piss') {
      rotateBone(view, 'upperarm01.R', X, -0.48);
      rotateBone(view, 'lowerarm01.R', X, -0.62);
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
