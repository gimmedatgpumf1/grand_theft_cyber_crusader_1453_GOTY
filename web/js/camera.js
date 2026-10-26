import * as THREE from './vendor/three.module.min.js';

const clamp = THREE.MathUtils.clamp;
const LOOK_SENSITIVITY = 0.0022;

export class CameraController {
  constructor(camera, resolveCamera = null) {
    this.camera = camera;
    this.resolveCamera = resolveCamera;
    this.mode = 'third';
    this.yaw = Math.PI;
    this.pitch = 0.34;
    this.distance = 5.1;
    this.godDistance = 16;
    this.godYaw = 0.72;
    this.godFocus = new THREE.Vector3(0, 0, 0);
    this.initialized = false;
  }

  setMode(mode, localPosition) {
    if (mode === 'first' && this.mode !== 'first') this.pitch = 0;
    if (mode === 'third' && this.mode !== 'third') this.pitch = 0.34;
    this.mode = mode;
    this.initialized = false;
    if (mode === 'god' && localPosition) this.godFocus.copy(localPosition).setY(0);
  }

  rotate(dx, dy) {
    // Positive mouse X must turn right with our +Z-forward yaw convention.
    this.yaw -= dx * LOOK_SENSITIVITY;
    if (this.mode === 'first') this.pitch = clamp(this.pitch - dy * LOOK_SENSITIVITY, -1.5, 1.25);
    else this.pitch = clamp(this.pitch + dy * LOOK_SENSITIVITY, 0.06, 1.05);
  }

  zoom(delta) {
    if (this.mode === 'god') this.godDistance = clamp(this.godDistance + delta * 0.012, 8, 25);
    else this.distance = clamp(this.distance + delta * 0.006, 2.8, 8);
  }

  pan(keys, dt) {
    let forwardAmount = 0;
    let rightAmount = 0;
    if (keys.has('KeyW')) forwardAmount += 1;
    if (keys.has('KeyS')) forwardAmount -= 1;
    if (keys.has('KeyD')) rightAmount += 1;
    if (keys.has('KeyA')) rightAmount -= 1;
    const boost = keys.has('ShiftLeft') || keys.has('ShiftRight') ? 1.6 : 1;
    const speed = dt * this.godDistance * 1.05 * boost;
    this.godFocus.x += (-Math.sin(this.godYaw) * forwardAmount + Math.cos(this.godYaw) * rightAmount) * speed;
    this.godFocus.z += (-Math.cos(this.godYaw) * forwardAmount - Math.sin(this.godYaw) * rightAmount) * speed;
    this.godFocus.x = clamp(this.godFocus.x, -9, 9);
    this.godFocus.z = clamp(this.godFocus.z, -7, 7);
  }

  update(dt, localView) {
    if (!localView) {
      const time = performance.now() * 0.00008;
      this.camera.position.set(Math.sin(time) * 10 + 4, 8.5, Math.cos(time) * 8 + 5);
      this.camera.lookAt(0, 0.7, -2.5);
      return;
    }
    const player = localView.group.position;
    if (this.mode === 'first') {
      const seated = localView.data.onToilet && !(localView.data.male && localView.data.toiletAction === 'piss');
      const eyeHeight = seated ? 1.43 : 1.65;
      const bodyForward = new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw));
      // Eyes sit ahead of the torso, so a natural downward look includes chest, legs and feet.
      this.camera.position.copy(player).add(new THREE.Vector3(0, eyeHeight, 0)).addScaledVector(bodyForward, 0.17);
      const horizontal = Math.cos(this.pitch);
      const direction = new THREE.Vector3(
        bodyForward.x * horizontal,
        Math.sin(this.pitch),
        bodyForward.z * horizontal
      );
      this.camera.lookAt(this.camera.position.clone().add(direction));
    } else if (this.mode === 'god') {
      const offset = new THREE.Vector3(
        Math.sin(this.godYaw) * this.godDistance * 0.72,
        this.godDistance * 0.82,
        Math.cos(this.godYaw) * this.godDistance * 0.72
      );
      const desired = this.godFocus.clone().add(offset);
      this.camera.position.lerp(desired, 1 - Math.exp(-12 * dt));
      this.camera.lookAt(this.godFocus.x, 0, this.godFocus.z);
    } else {
      const forward = new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw));
      const right = new THREE.Vector3(-Math.cos(this.yaw), 0, Math.sin(this.yaw));
      // Shift aim and camera together: W follows the exact view direction, not a shoulder-biased line.
      const target = player.clone().add(new THREE.Vector3(0, 1.25, 0)).addScaledVector(right, 0.42);
      const horizontal = Math.cos(this.pitch) * this.distance;
      const desired = target.clone()
        .addScaledVector(forward, -horizontal)
        .add(new THREE.Vector3(0, Math.sin(this.pitch) * this.distance, 0));
      const safePosition = this.resolveCamera ? this.resolveCamera(target, desired) : desired;
      const smoothing = this.initialized ? 1 - Math.exp(-16 * dt) : 1;
      this.camera.position.lerp(safePosition, smoothing);
      this.camera.lookAt(target);
    }
    this.initialized = true;
  }

  movement(keys) {
    let forwardAmount = 0;
    let rightAmount = 0;
    if (keys.has('KeyW')) forwardAmount += 1;
    if (keys.has('KeyS')) forwardAmount -= 1;
    if (keys.has('KeyD')) rightAmount += 1;
    if (keys.has('KeyA')) rightAmount -= 1;
    // right = forward × up = (-cos(yaw), 0, sin(yaw)).
    return {
      x: Math.sin(this.yaw) * forwardAmount - Math.cos(this.yaw) * rightAmount,
      z: Math.cos(this.yaw) * forwardAmount + Math.sin(this.yaw) * rightAmount,
    };
  }
}
