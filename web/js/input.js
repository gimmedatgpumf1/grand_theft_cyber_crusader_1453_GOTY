export class InputController {
  constructor(canvas, camera, network, scene, callbacks) {
    this.canvas = canvas;
    this.camera = camera;
    this.network = network;
    this.scene = scene;
    this.callbacks = callbacks;
    this.keys = new Set();
    this.mode = 'third';
    this.pointer = { x: window.innerWidth / 2, y: window.innerHeight / 2 };
    this.bind();
  }

  blocked() {
    return this.callbacks.isHelpOpen() || this.callbacks.isInputBlocked?.();
  }

  bind() {
    window.addEventListener('keydown', event => {
      if (event.target instanceof HTMLInputElement) return;
      if (event.code === 'KeyH' && !event.repeat) {
        this.callbacks.onHelp();
        event.preventDefault();
        return;
      }
      if (event.code === 'F1') {
        event.preventDefault();
        if (!event.repeat) this.callbacks.onCheat?.();
        return;
      }
      if (event.code === 'Escape') {
        this.clear();
        if (this.callbacks.isInputBlocked?.()) this.callbacks.onCheat?.(false);
        else if (this.callbacks.isHelpOpen()) this.callbacks.onHelp(false);
        else if (document.pointerLockElement) document.exitPointerLock?.();
        return;
      }
      if (this.blocked()) return;
      if (['Digit1', 'Digit2', 'Digit3'].includes(event.code) && !event.repeat) {
        this.setMode({ Digit1: 'third', Digit2: 'god', Digit3: 'first' }[event.code]);
        return;
      }
      if (['KeyW', 'KeyA', 'KeyS', 'KeyD', 'ShiftLeft', 'ShiftRight', 'KeyE', 'KeyF', 'Space'].includes(event.code)) {
        event.preventDefault();
      }
      this.keys.add(event.code);
      if (event.code === 'KeyE' && !event.repeat) this.interact();
    });
    window.addEventListener('keyup', event => this.keys.delete(event.code));
    window.addEventListener('blur', () => this.clear());
    document.addEventListener('pointerlockchange', () => {
      if (!document.pointerLockElement) this.clear();
    });

    this.canvas.addEventListener('contextmenu', event => event.preventDefault());
    window.addEventListener('mousemove', event => {
      if (document.pointerLockElement === this.canvas) {
        this.camera.rotate(event.movementX, event.movementY);
      } else {
        this.pointer.x = event.clientX;
        this.pointer.y = event.clientY;
      }
    });
    this.canvas.addEventListener('mousedown', event => {
      if (this.blocked() || event.button !== 0) return;
      const locked = document.pointerLockElement === this.canvas;
      const x = locked ? window.innerWidth / 2 : event.clientX;
      const y = locked ? window.innerHeight / 2 : event.clientY;
      const hit = this.scene.pick(x, y, this.camera.camera);
      if (this.mode !== 'god' && !locked) {
        this.canvas.requestPointerLock?.();
        return;
      }
      if (hit?.kind === 'toilet') this.interact();
      else if (hit?.kind === 'ground') {
        this.network.path(hit.point);
        this.scene.showDestination(hit.point);
      }
    });
    this.canvas.addEventListener('wheel', event => {
      if (this.blocked()) return;
      event.preventDefault();
      if (this.mode === 'god') this.camera.zoom(event.deltaY);
    }, { passive: false });
  }

  setMode(mode) {
    this.mode = mode;
    this.camera.setMode(mode, this.callbacks.localPosition());
    this.callbacks.onMode(mode);
    this.clear();
    if (mode === 'god' && document.pointerLockElement) document.exitPointerLock?.();
  }

  clear() {
    this.keys.clear();
    this.scene.setHighlight(null);
  }

  interact() {
    this.network.toilet();
  }

  updateHover() {
    if (this.blocked()) {
      this.scene.setHighlight(null);
      return;
    }
    const locked = document.pointerLockElement === this.canvas;
    const hit = this.scene.pick(locked ? window.innerWidth / 2 : this.pointer.x,
      locked ? window.innerHeight / 2 : this.pointer.y, this.camera.camera);
    this.scene.setHighlight(hit?.kind || null);
    this.canvas.style.cursor = this.mode === 'god' ? (hit?.kind === 'toilet' ? 'pointer' : 'crosshair') :
      locked ? 'none' : 'default';
  }

  update(dt) {
    this.updateHover();
    if (this.blocked()) return { x: 0, z: 0, sprint: false, piss: false, yaw: this.callbacks.localYaw(), pitch: 0 };
    const boost = this.keys.has('ShiftLeft') || this.keys.has('ShiftRight');
    if (this.mode === 'god') {
      this.camera.pan(this.keys, dt);
      return { x: 0, z: 0, sprint: false, piss: false, yaw: this.callbacks.localYaw(), pitch: 0 };
    }
    const move = this.camera.movement(this.keys);
    const moving = Math.hypot(move.x, move.z) > 0.05;
    return {
      ...move,
      sprint: boost,
      piss: this.mode === 'first' && this.keys.has('KeyF'),
      yaw: this.mode === 'first' || moving ? this.camera.yaw : this.callbacks.localYaw(),
      pitch: this.camera.pitch,
    };
  }
}
