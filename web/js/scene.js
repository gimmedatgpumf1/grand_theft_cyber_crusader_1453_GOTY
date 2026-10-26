import * as THREE from './vendor/three.module.min.js';

function standard(color, options = {}) {
  return new THREE.MeshStandardMaterial({ color, roughness: 0.82, flatShading: true, ...options });
}

function box(width, height, depth, material) {
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(width, height, depth), material);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  return mesh;
}

function canvasTexture(width, height, draw) {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  draw(canvas.getContext('2d'), width, height);
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  return texture;
}

function sign(text, width, height, foreground = '#d7ff54', background = '#152019') {
  const texture = canvasTexture(512, 256, (context, w, h) => {
    context.fillStyle = background;
    context.fillRect(0, 0, w, h);
    context.strokeStyle = foreground;
    context.lineWidth = 7;
    context.strokeRect(12, 12, w - 24, h - 24);
    context.fillStyle = foreground;
    context.textAlign = 'center';
    context.textBaseline = 'middle';
    context.font = '900 48px monospace';
    const lines = text.split('\n');
    lines.forEach((line, index) => context.fillText(line, w / 2, h / 2 + (index - (lines.length - 1) / 2) * 58));
    for (let i = 0; i < 70; ++i) {
      context.globalAlpha = Math.random() * 0.18;
      context.fillRect(Math.random() * w, Math.random() * h, Math.random() * 28, 3);
    }
  });
  return new THREE.Mesh(new THREE.PlaneGeometry(width, height), new THREE.MeshBasicMaterial({ map: texture }));
}

export class GameScene {
  constructor(canvas) {
    this.canvas = canvas;
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color('#111913');
    this.scene.fog = new THREE.Fog('#111913', 20, 38);
    this.camera = new THREE.PerspectiveCamera(62, 1, 0.02, 70);
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.08;
    this.raycaster = new THREE.Raycaster();
    this.cameraRaycaster = new THREE.Raycaster();
    this.cameraBlockers = [];
    this.pointer = new THREE.Vector2();
    this.clickTargets = [];
    this.toiletMaterials = new Set();
    this.highlightedKind = null;
    this.puddles = new Map();
    this.worldRoot = null;
    this.destinationLife = 0;
    this.addLights();
    this.resize();
    window.addEventListener('resize', () => this.resize());
  }

  addLights() {
    const hemisphere = new THREE.HemisphereLight('#c8e3c1', '#273125', 1.55);
    this.scene.add(hemisphere);
    const sun = new THREE.DirectionalLight('#fff1c7', 2.15);
    sun.position.set(-4, 11, 6);
    sun.castShadow = true;
    sun.shadow.mapSize.set(1024, 1024);
    sun.shadow.camera.left = -12;
    sun.shadow.camera.right = 12;
    sun.shadow.camera.top = 12;
    sun.shadow.camera.bottom = -12;
    sun.shadow.camera.near = 1;
    sun.shadow.camera.far = 30;
    sun.shadow.bias = -0.0007;
    this.scene.add(sun);
    const sickly = new THREE.PointLight('#b7ff6a', 9, 9, 2);
    sickly.position.set(0, 2.8, -4.5);
    this.scene.add(sickly);
  }

  buildWorld(world) {
    this.world = world;
    if (this.worldRoot) this.scene.remove(this.worldRoot);
    this.worldRoot = new THREE.Group();
    this.scene.add(this.worldRoot);
    this.clickTargets = [];
    this.cameraBlockers = [];
    this.toiletMaterials = new Set();
    this.highlightedKind = null;
    for (const puddle of this.puddles.values()) this.scene.remove(puddle);
    this.puddles.clear();
    this.createFloor(world);
    this.ceiling = new THREE.Mesh(new THREE.PlaneGeometry(world.width, world.depth), standard('#1b2720', { side: THREE.FrontSide }));
    this.ceiling.rotation.x = Math.PI / 2;
    this.ceiling.position.y = 3.39;
    this.ceiling.receiveShadow = true;
    this.worldRoot.add(this.ceiling);
    this.cameraBlockers.push(this.ceiling);
    for (const obstacle of world.obstacles) {
      if (obstacle.kind === 'boundary') this.createWall(obstacle);
      else if (obstacle.kind === 'toilet') this.createToilet(obstacle);
      else if (obstacle.kind === 'sink') this.createSink(obstacle);
      else this.createCrate(obstacle);
    }
    this.createDressing();
    this.createInteractionMarkers();
    this.worldRoot.updateMatrixWorld(true);
  }

  createFloor(world) {
    const texture = canvasTexture(512, 512, (context) => {
      const tile = 64;
      for (let y = 0; y < 8; ++y) {
        for (let x = 0; x < 8; ++x) {
          const pale = (x + y) % 2 === 0;
          context.fillStyle = pale ? '#83917a' : '#31473e';
          context.fillRect(x * tile, y * tile, tile, tile);
          context.strokeStyle = '#1a2821';
          context.lineWidth = 4;
          context.strokeRect(x * tile, y * tile, tile, tile);
          context.fillStyle = pale ? '#4d5a47' : '#1e332a';
          for (let i = 0; i < 6; ++i) {
            context.globalAlpha = 0.12 + Math.random() * 0.16;
            context.beginPath();
            context.arc(x * tile + Math.random() * tile, y * tile + Math.random() * tile, 2 + Math.random() * 9, 0, Math.PI * 2);
            context.fill();
          }
          context.globalAlpha = 1;
        }
      }
    });
    texture.wrapS = texture.wrapT = THREE.RepeatWrapping;
    texture.repeat.set(world.width / 8, world.depth / 8);
    texture.anisotropy = Math.min(4, this.renderer.capabilities.getMaxAnisotropy());
    const floor = new THREE.Mesh(new THREE.PlaneGeometry(world.width, world.depth),
      new THREE.MeshStandardMaterial({ map: texture, roughness: 0.93 }));
    floor.rotation.x = -Math.PI / 2;
    floor.receiveShadow = true;
    floor.userData.kind = 'ground';
    this.worldRoot.add(floor);
    this.clickTargets.push(floor);
    const outside = new THREE.Mesh(new THREE.PlaneGeometry(80, 80), standard('#0b110d'));
    outside.rotation.x = -Math.PI / 2;
    outside.position.y = -0.04;
    outside.receiveShadow = true;
    this.worldRoot.add(outside);
  }

  createWall(obstacle) {
    const tall = obstacle.height > 1;
    const material = standard(tall ? '#53665a' : '#293a31');
    const wall = box(obstacle.hx * 2, obstacle.height, obstacle.hz * 2, material);
    wall.position.set(obstacle.x, obstacle.height / 2, obstacle.z);
    this.worldRoot.add(wall);
    this.cameraBlockers.push(wall);
    if (tall) {
      const trim = box(obstacle.hx * 2 + 0.04, 0.16, obstacle.hz * 2 + 0.04, standard('#1c2d25'));
      trim.position.set(obstacle.x, 1.05, obstacle.z);
      this.worldRoot.add(trim);
    }
  }

  createToilet(obstacle) {
    const toilet = new THREE.Group();
    toilet.position.set(obstacle.x, 0, obstacle.z);
    const porcelain = standard('#d7ddc9', { roughness: 0.35 });
    const dark = standard('#17251e');
    const pedestal = new THREE.Mesh(new THREE.CylinderGeometry(0.28, 0.38, 0.5, 8), porcelain);
    pedestal.position.y = 0.25;
    pedestal.castShadow = true;
    toilet.add(pedestal);
    const bowl = new THREE.Mesh(new THREE.CylinderGeometry(0.47, 0.32, 0.38, 10), porcelain);
    bowl.position.set(0, 0.51, 0.08);
    bowl.scale.z = 1.17;
    bowl.castShadow = true;
    toilet.add(bowl);
    const seat = new THREE.Mesh(new THREE.TorusGeometry(0.37, 0.085, 6, 14), porcelain);
    seat.rotation.x = Math.PI / 2;
    seat.scale.y = 1.18;
    seat.position.set(0, 0.73, 0.09);
    seat.castShadow = true;
    toilet.add(seat);
    const water = new THREE.Mesh(new THREE.CircleGeometry(0.29, 12), standard('#344c3c', { roughness: 0.1 }));
    water.rotation.x = -Math.PI / 2;
    water.position.set(0, 0.695, 0.09);
    toilet.add(water);
    const tank = box(0.88, 0.78, 0.38, porcelain);
    tank.position.set(0, 1.03, -0.47);
    toilet.add(tank);
    const tankTop = box(0.96, 0.09, 0.44, porcelain);
    tankTop.position.set(0, 1.45, -0.47);
    toilet.add(tankTop);
    const handle = box(0.17, 0.07, 0.08, standard('#a2ad8f', { metalness: 0.65 }));
    handle.position.set(0.48, 1.18, -0.33);
    toilet.add(handle);
    const pipe = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.07, 0.7, 7), dark);
    pipe.position.set(0, 0.42, -0.68);
    toilet.add(pipe);
    toilet.traverse(child => {
      child.userData.kind = 'toilet';
      if (!child.isMesh) return;
      for (const material of Array.isArray(child.material) ? child.material : [child.material]) {
        if (!material.emissive || this.toiletMaterials.has(material)) continue;
        material.userData.baseEmissive = material.emissive.getHex();
        material.userData.baseEmissiveIntensity = material.emissiveIntensity;
        this.toiletMaterials.add(material);
      }
    });
    this.worldRoot.add(toilet);
    this.clickTargets.push(toilet);
  }

  createSink(obstacle) {
    const group = new THREE.Group();
    group.position.set(obstacle.x, 0, obstacle.z);
    const cabinet = box(1.35, 0.72, 0.75, standard('#354b40'));
    cabinet.position.y = 0.36;
    group.add(cabinet);
    this.cameraBlockers.push(cabinet);
    const basin = new THREE.Mesh(new THREE.CylinderGeometry(0.61, 0.48, 0.24, 8), standard('#bdc9b5'));
    basin.scale.z = 0.72;
    basin.position.y = 0.85;
    basin.castShadow = true;
    group.add(basin);
    const faucet = new THREE.Mesh(new THREE.TorusGeometry(0.18, 0.035, 5, 8, Math.PI), standard('#85998b', { metalness: 0.7 }));
    faucet.position.set(0, 1.05, -0.2);
    group.add(faucet);
    this.worldRoot.add(group);
    const mirrorFrame = box(1.84, 1.28, 0.05, standard('#1b2b23'));
    mirrorFrame.position.set(obstacle.x, 2.0, -6.84);
    this.worldRoot.add(mirrorFrame);
    const mirror = new THREE.Mesh(new THREE.PlaneGeometry(1.7, 1.15), new THREE.MeshBasicMaterial({ color: '#263d33' }));
    mirror.position.set(obstacle.x, 2.0, -6.80);
    this.worldRoot.add(mirror);
  }

  createCrate(obstacle) {
    const crate = box(obstacle.hx * 2, obstacle.height, obstacle.hz * 2, standard('#5e5135'));
    crate.position.set(obstacle.x, obstacle.height / 2, obstacle.z);
    this.worldRoot.add(crate);
    this.cameraBlockers.push(crate);
    const edges = new THREE.LineSegments(new THREE.EdgesGeometry(crate.geometry), new THREE.LineBasicMaterial({ color: '#211f16' }));
    edges.position.copy(crate.position);
    this.worldRoot.add(edges);
  }

  createDressing() {
    const graffiti = sign('FLUSH THE\nFLESH', 2.4, 1.15, '#d7ff54', '#2c3c31');
    graffiti.position.set(-3.8, 2.05, -6.82);
    graffiti.rotation.z = -0.06;
    this.worldRoot.add(graffiti);
    const warning = sign('NO GODS\nONLY CLOGS', 2.15, 0.95, '#e9a45f', '#26332b');
    warning.position.set(3.0, 2.25, -6.82);
    warning.rotation.z = 0.04;
    this.worldRoot.add(warning);
    const neon = sign('PORCELAIN.EXE', 2.5, 0.5, '#d7ff54', '#101a13');
    neon.position.set(0, 2.35, -6.81);
    this.worldRoot.add(neon);

    const pipeMaterial = standard('#677568', { metalness: 0.5 });
    for (const x of [-7.7, 7.65]) {
      const pipe = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.11, 3.1, 7), pipeMaterial);
      pipe.position.set(x, 1.55, -6.65);
      pipe.castShadow = true;
      this.worldRoot.add(pipe);
    }
    const plungerStick = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.035, 1.0, 6), standard('#9b7748'));
    plungerStick.position.set(-1.1, 0.65, -5.35);
    plungerStick.rotation.z = 0.15;
    this.worldRoot.add(plungerStick);
    const plunger = new THREE.Mesh(new THREE.ConeGeometry(0.22, 0.3, 8, 1, true), standard('#6c382c'));
    plunger.position.set(-1.18, 0.15, -5.35);
    this.worldRoot.add(plunger);
    const paper = new THREE.Mesh(new THREE.CylinderGeometry(0.16, 0.16, 0.24, 10), standard('#d9decb'));
    paper.rotation.z = Math.PI / 2;
    paper.position.set(1.25, 1.0, -6.55);
    this.worldRoot.add(paper);
    for (const x of [-3.5, 3.5]) {
      const fixture = box(2.3, 0.09, 0.28, standard('#c9efaf', { emissive: '#7da74c', emissiveIntensity: 1.2 }));
      fixture.position.set(x, 3.05, -1.7);
      this.worldRoot.add(fixture);
    }
  }

  createInteractionMarkers() {
    const ringMaterial = new THREE.MeshBasicMaterial({ color: '#d7ff54', transparent: true, opacity: 0.22, side: THREE.DoubleSide, depthWrite: false });
    this.interactionRing = new THREE.Mesh(new THREE.RingGeometry(this.world.interactionRange - 0.035, this.world.interactionRange, 48), ringMaterial);
    this.interactionRing.rotation.x = -Math.PI / 2;
    this.interactionRing.position.set(this.world.toilet.frontX, 0.025, this.world.toilet.frontZ);
    this.worldRoot.add(this.interactionRing);
    this.questMarker = new THREE.Mesh(new THREE.OctahedronGeometry(0.16, 0), new THREE.MeshBasicMaterial({ color: '#d7ff54' }));
    this.questMarker.position.set(this.world.toilet.x, 2.05, this.world.toilet.z);
    this.worldRoot.add(this.questMarker);
    this.destination = new THREE.Mesh(new THREE.RingGeometry(0.19, 0.25, 16),
      new THREE.MeshBasicMaterial({ color: '#d7ff54', transparent: true, opacity: 0, side: THREE.DoubleSide, depthWrite: false }));
    this.destination.rotation.x = -Math.PI / 2;
    this.destination.position.y = 0.035;
    this.worldRoot.add(this.destination);
  }

  syncPuddles(puddles) {
    const alive = new Set();
    for (const data of puddles) {
      alive.add(data.id);
      let mesh = this.puddles.get(data.id);
      if (!mesh) {
        mesh = new THREE.Mesh(new THREE.CircleGeometry(1, 13), new THREE.MeshStandardMaterial({
          color: '#b5a636', roughness: 0.22, transparent: true, opacity: 0.72, depthWrite: false,
        }));
        mesh.rotation.x = -Math.PI / 2;
        mesh.rotation.z = data.id * 1.71;
        mesh.position.y = 0.018;
        this.scene.add(mesh);
        this.puddles.set(data.id, mesh);
      }
      mesh.position.x = data.x;
      mesh.position.z = data.z;
      mesh.scale.set(data.size, data.size * 0.72, 1);
    }
    for (const [id, mesh] of this.puddles) {
      if (!alive.has(id)) {
        this.scene.remove(mesh);
        this.puddles.delete(id);
      }
    }
  }

  resolveCamera(target, desired) {
    const direction = desired.clone().sub(target);
    const distance = direction.length();
    if (distance < 0.01 || this.cameraBlockers.length === 0) return desired;
    direction.multiplyScalar(1 / distance);
    this.cameraRaycaster.near = 0.05;
    this.cameraRaycaster.far = distance;
    this.cameraRaycaster.set(target, direction);
    const hit = this.cameraRaycaster.intersectObjects(this.cameraBlockers, false)[0];
    if (!hit) return desired;
    const safeDistance = Math.max(0.35, hit.distance - 0.24);
    return target.clone().addScaledVector(direction, safeDistance);
  }

  setMode(mode) {
    if (this.ceiling) this.ceiling.visible = mode !== 'god';
  }

  showDestination(point) {
    if (!this.destination) return;
    this.destination.position.set(point.x, 0.035, point.z);
    this.destinationLife = 1;
  }

  setHighlight(kind) {
    const activeKind = kind || null;
    if (this.highlightedKind === activeKind) return;
    this.highlightedKind = activeKind;
    const active = activeKind === 'toilet';
    for (const material of this.toiletMaterials) {
      material.emissive.setHex(active ? 0x526b24 : material.userData.baseEmissive);
      material.emissiveIntensity = active ? 0.72 : material.userData.baseEmissiveIntensity;
    }
    if (this.questMarker) this.questMarker.material.color.set(active ? '#ffffff' : '#d7ff54');
  }

  pick(clientX, clientY, camera) {
    const bounds = this.canvas.getBoundingClientRect();
    this.pointer.set(((clientX - bounds.left) / bounds.width) * 2 - 1,
      -((clientY - bounds.top) / bounds.height) * 2 + 1);
    this.raycaster.setFromCamera(this.pointer, camera);
    const hits = this.raycaster.intersectObjects(this.clickTargets, true);
    if (!hits.length) return null;
    const hit = hits[0];
    let object = hit.object;
    while (object && !object.userData.kind) object = object.parent;
    if (object?.userData.kind === 'toilet') return { kind: 'toilet' };
    return { kind: 'ground', point: { x: hit.point.x, z: hit.point.z } };
  }

  update(time, dt) {
    if (this.questMarker) {
      this.questMarker.rotation.y = time * 1.7;
      this.questMarker.position.y = 2.04 + Math.sin(time * 2.8) * 0.1;
      this.questMarker.scale.setScalar(this.highlightedKind === 'toilet' ? 1.45 : 1);
    }
    if (this.interactionRing) {
      this.interactionRing.material.opacity = this.highlightedKind === 'toilet' ? 0.72 :
        0.15 + Math.sin(time * 2) * 0.06;
    }
    if (this.destinationLife > 0) {
      this.destinationLife = Math.max(0, this.destinationLife - dt * 0.8);
      this.destination.material.opacity = this.destinationLife;
      const scale = 1 + (1 - this.destinationLife) * 1.8;
      this.destination.scale.setScalar(scale);
    }
  }

  resize() {
    const width = this.canvas.clientWidth || window.innerWidth;
    const height = this.canvas.clientHeight || window.innerHeight;
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(width, height, false);
  }

  render() {
    this.renderer.render(this.scene, this.camera);
  }
}
