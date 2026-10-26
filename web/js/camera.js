import * as THREE from './vendor/three.module.min.js';

const clamp = THREE.MathUtils.clamp;

export class CameraController {
  constructor(camera) {
    this.camera = camera;
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
    if (mode === 'god' && localPosition) this.godFocus.copy(localPosition).setY(0);
  }

  rotate(dx, dy) {
    this.yaw += dx * 0.004;
    if (this.mode === 'first') this.pitch = clamp(this.pitch - dy * 0.003, -1.25, 1.25);
    else this.pitch = clamp(this.pitch + dy * 0.003, 0.06, 1.05);
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
    const speed = dt * this.godDistance * 0.45;
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
      this.camera.position.copy(player).add(new THREE.Vector3(0, 1.55, 0));
      const horizontal = Math.cos(this.pitch);
      const direction = new THREE.Vector3(
        Math.sin(this.yaw) * horizontal,
        Math.sin(this.pitch),
        Math.cos(this.yaw) * horizontal
      );
      this.camera.lookAt(this.camera.position.clone().add(direction));
    } else if (this.mode === 'god') {
      const offset = new THREE.Vector3(
        Math.sin(this.godYaw) * this.godDistance * 0.72,
        this.godDistance * 0.82,
        Math.cos(this.godYaw) * this.godDistance * 0.72
      );
      const desired = this.godFocus.clone().add(offset);
      this.camera.position.lerp(desired, 1 - Math.exp(-8 * dt));
      this.camera.lookAt(this.godFocus.x, 0, this.godFocus.z);
    } else {
      const forward = new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw));
      const right = new THREE.Vector3(Math.cos(this.yaw), 0, -Math.sin(this.yaw));
      const target = player.clone().add(new THREE.Vector3(0, 1.25, 0));
      const horizontal = Math.cos(this.pitch) * this.distance;
      const desired = target.clone()
        .addScaledVector(forward, -horizontal)
        .addScaledVector(right, 0.55)
        .add(new THREE.Vector3(0, Math.sin(this.pitch) * this.distance, 0));
      const smoothing = this.initialized ? 1 - Math.exp(-13 * dt) : 1;
      this.camera.position.lerp(desired, smoothing);
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
    return {
      x: Math.sin(this.yaw) * forwardAmount + Math.cos(this.yaw) * rightAmount,
      z: Math.cos(this.yaw) * forwardAmount - Math.sin(this.yaw) * rightAmount,
    };
  }
}
