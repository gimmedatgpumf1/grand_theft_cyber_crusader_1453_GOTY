export class InputController {
  constructor(canvas, camera, network, scene, callbacks) {
    this.canvas = canvas;
    this.camera = camera;
    this.network = network;
    this.scene = scene;
    this.callbacks = callbacks;
    this.keys = new Set();
    this.mode = 'third';
    this.dragging = false;
    this.pissing = false;
    this.lastYaw = Math.PI;
    this.bindEvents();
  }

  bindEvents() {
    window.addEventListener('keydown', event => {
      if (event.target instanceof HTMLInputElement) return;
      if (['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyF', 'Space'].includes(event.code)) event.preventDefault();
      if (event.code === 'KeyH' && !event.repeat) return this.callbacks.onHelp();
      if (event.code === 'Escape' && this.callbacks.isHelpOpen()) return this.callbacks.onHelp(false);
      if (this.callbacks.isHelpOpen()) return;
      if (event.code === 'Digit1') this.setMode('third');
      if (event.code === 'Digit2') this.setMode('god');
      if (event.code === 'Digit3') this.setMode('first');
      if (event.code === 'KeyE' && !event.repeat) this.network.toilet();
      if (event.code === 'KeyF') this.pissing = this.mode === 'first';
      this.keys.add(event.code);
    });
    window.addEventListener('keyup', event => {
      this.keys.delete(event.code);
      if (event.code === 'KeyF') this.pissing = false;
    });
    window.addEventListener('blur', () => this.clear());
    this.canvas.addEventListener('contextmenu', event => event.preventDefault());
    this.canvas.addEventListener('mousedown', event => this.mouseDown(event));
    window.addEventListener('mouseup', event => {
      if (event.button === 2) this.dragging = false;
    });
    window.addEventListener('mousemove', event => {
      const locked = document.pointerLockElement === this.canvas;
      if ((locked && this.mode === 'first') || (this.dragging && this.mode === 'third')) {
        this.camera.rotate(event.movementX, event.movementY);
      }
    });
    this.canvas.addEventListener('wheel', event => {
      event.preventDefault();
      this.camera.zoom(event.deltaY);
    }, { passive: false });
  }

  mouseDown(event) {
    if (this.callbacks.isHelpOpen()) return;
    if (event.button === 2) {
      this.dragging = true;
      return;
    }
    if (event.button !== 0) return;
    if (this.mode === 'first' && document.pointerLockElement !== this.canvas) {
      this.canvas.requestPointerLock?.();
      return;
    }
    const locked = document.pointerLockElement === this.canvas;
    const hit = this.scene.pick(locked ? window.innerWidth / 2 : event.clientX,
      locked ? window.innerHeight / 2 : event.clientY, this.camera.camera);
    if (!hit) return;
    if (hit.kind === 'toilet') this.network.toilet();
    if (hit.kind === 'ground') {
      this.network.path(hit.point);
      this.scene.showDestination(hit.point);
    }
  }

  setMode(mode) {
    this.mode = mode;
    this.pissing = false;
    this.clear();
    if (mode !== 'first' && document.pointerLockElement === this.canvas) document.exitPointerLock?.();
    this.camera.setMode(mode, this.callbacks.localPosition());
    this.callbacks.onMode(mode);
  }

  clear() {
    this.keys.clear();
    this.pissing = false;
    this.dragging = false;
  }

  update(dt) {
    if (this.callbacks.isHelpOpen()) {
      return { x: 0, z: 0, sprint: false, piss: false, yaw: this.lastYaw, pitch: 0 };
    }
    if (this.mode === 'god') {
      this.camera.pan(this.keys, dt);
      return { x: 0, z: 0, sprint: false, piss: false, yaw: this.callbacks.localYaw(), pitch: 0 };
    }
    const move = this.camera.movement(this.keys);
    this.lastYaw = this.camera.yaw;
    return {
      ...move,
      sprint: this.keys.has('ShiftLeft') || this.keys.has('ShiftRight'),
      piss: this.pissing && this.mode === 'first',
      yaw: this.camera.yaw,
      pitch: this.camera.pitch,
    };
  }
}
