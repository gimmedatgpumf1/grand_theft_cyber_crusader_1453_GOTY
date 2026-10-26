import { Network, defaultIdentity } from './network.js';
import { GameScene } from './scene.js';
import { CharacterManager } from './characters.js';
import { CameraController } from './camera.js';
import { InputController } from './input.js';
import { HUD } from './hud.js';
import { CharacterCreator } from './creator.js';

const hud = new HUD();
let gameScene;
try {
  gameScene = new GameScene(document.getElementById('world'));
} catch (error) {
  hud.fatal(`WebGL could not start: ${error.message}. Try enabling hardware acceleration or updating your browser.`);
  throw error;
}

const identity = defaultIdentity();
const network = new Network(identity);
const characters = new CharacterManager(gameScene.scene);
const camera = new CameraController(gameScene.camera,
  (target, desired) => gameScene.resolveCamera(target, desired));
const creator = new CharacterCreator(network, {
  onApplied: () => hud.toast('Citizen rebuilt. The server has witnessed your new meat.'),
});
let latestState = { characters: [], puddles: [], events: [], tick: 0, toiletUser: 0 };
let mode = 'third';
let overlay = null;
let creatorCamera = null;

function closeOverlay() {
  const restore = overlay === 'creator' ? creatorCamera : null;
  overlay = null;
  creatorCamera = null;
  hud.help(false);
  hud.cheats(false);
  hud.wheel(false);
  creator.close();
  document.body.classList.remove('creator-open');
  input.clear();
  if (restore) {
    input.setMode(restore.mode, false);
    camera.yaw = restore.yaw;
    camera.pitch = restore.pitch;
    camera.distance = restore.distance;
    camera.godFocus.copy(restore.godFocus);
    camera.initialized = false;
  }
}

function showOverlay(name) {
  if (overlay === name) return;
  closeOverlay();
  const local = latestState.characters.find(character => character.id === network.id);
  if ((name === 'creator' || name === 'wheel') && !local) return;
  overlay = name;
  input.clear();
  if (document.pointerLockElement) document.exitPointerLock?.();
  if (name === 'help') hud.help(true);
  if (name === 'cheats') hud.cheats(true);
  if (name === 'wheel') hud.wheel(true, local);
  if (name === 'creator') {
    creatorCamera = {
      mode, yaw: camera.yaw, pitch: camera.pitch, distance: camera.distance,
      godFocus: camera.godFocus.clone(),
    };
    input.setMode('third', false);
    camera.yaw = local.yaw + Math.PI;
    camera.pitch = 0.08;
    camera.distance = 3.25;
    camera.initialized = false;
    creator.open(local);
    document.body.classList.add('creator-open');
  }
}

function toggleOverlay(name) {
  if (overlay === name) closeOverlay();
  else showOverlay(name);
}

function interact() {
  const local = latestState.characters.find(character => character.id === network.id);
  if (!local || !network.world) return;
  if (local.toiletAction !== 'none') {
    network.toilet('stand');
    return;
  }
  const distance = Math.hypot(local.x - network.world.toilet.frontX, local.z - network.world.toilet.frontZ);
  if (distance > network.world.interactionRange) {
    hud.toast(`Out of range: ${distance.toFixed(1)} m / ${network.world.interactionRange.toFixed(1)} m. Walk closer; E never auto-walks.`);
    return;
  }
  if (latestState.toiletUser !== 0 && latestState.toiletUser !== local.id) {
    hud.toast('Throne occupied. Await your terrible inheritance.');
    return;
  }
  showOverlay('wheel');
}

const input = new InputController(document.getElementById('world'), camera, network, gameScene, {
  onMode(newMode, announce = true) {
    mode = newMode;
    hud.setMode(mode);
    gameScene.setMode(mode);
    if (!announce) return;
    if (mode === 'first') hud.toast('Click the view to capture your mouse. Hold F to piss.');
    if (mode === 'third') hud.toast('Click the view for mouse look. WASD moves; E opens the toilet wheel.');
    if (mode === 'god') hud.toast('Godview: WASD fast-pans; Shift boosts. Clicks move your citizen.');
  },
  onHelp: () => toggleOverlay('help'),
  onCheat: () => toggleOverlay('cheats'),
  onCreator: () => toggleOverlay('creator'),
  onEscape: () => overlay ? closeOverlay() : document.exitPointerLock?.(),
  onInteract: interact,
  isHelpOpen: () => overlay === 'help',
  isInputBlocked: () => overlay !== null,
  localPosition: () => characters.localView(network.id)?.group.position || null,
  localYaw: () => characters.localView(network.id)?.data.yaw ?? Math.PI,
});

network.onStatus = status => hud.network(status);
network.onWelcome = message => {
  closeOverlay();
  latestState = { characters: [], puddles: [], events: [], tick: 0, toiletUser: 0 };
  gameScene.buildWorld(message.world);
  gameScene.setMode(mode);
  hud.lastNotice = 0;
  hud.welcome(message, network.identity);
  hud.toast(`Welcome, ${network.identity}. Press C to fabricate your citizen.`);
};
network.onState = state => {
  latestState = state;
  gameScene.syncPuddles(state.puddles);
  if (network.world) hud.update(state, network.id, network.world);
};

hud.setMode(mode);
document.querySelectorAll('[data-mode]').forEach(button => {
  button.addEventListener('click', () => {
    if (overlay === 'creator') closeOverlay();
    input.setMode(button.dataset.mode);
  });
});
document.getElementById('help-button').addEventListener('click', () => showOverlay('help'));
document.getElementById('close-help').addEventListener('click', closeOverlay);
document.getElementById('help').addEventListener('mousedown', event => {
  if (event.target.id === 'help') closeOverlay();
});
document.getElementById('cheat-button').addEventListener('click', () => showOverlay('cheats'));
document.getElementById('close-cheats').addEventListener('click', closeOverlay);
document.getElementById('cheats').addEventListener('mousedown', event => {
  if (event.target.id === 'cheats') closeOverlay();
});
document.getElementById('creator-button').addEventListener('click', () => toggleOverlay('creator'));
document.getElementById('close-creator').addEventListener('click', closeOverlay);
document.getElementById('close-wheel').addEventListener('click', closeOverlay);
document.querySelectorAll('[data-toilet-action]').forEach(button => {
  button.addEventListener('click', () => {
    const action = button.dataset.toiletAction;
    closeOverlay();
    network.toilet(action);
  });
});
document.querySelectorAll('.cheat-buttons button').forEach(button => {
  button.addEventListener('click', () => network.cheat(button.dataset.need, Number(button.dataset.delta)));
});
document.getElementById('apply-name').addEventListener('click', () => {
  const field = document.getElementById('player-name');
  const name = field.value.trim().slice(0, 18) || 'Unnamed Mistake';
  field.value = name;
  network.rename(name);
  hud.identity(name);
  hud.toast('Citizen record updated. Your sins remain unchanged.');
});
document.getElementById('player-name').addEventListener('keydown', event => {
  if (event.key === 'Enter') document.getElementById('apply-name').click();
});
document.addEventListener('visibilitychange', () => {
  if (document.hidden) input.clear();
});

let previous = performance.now();
let inputAccumulator = 0;
function frame(now) {
  const dt = Math.min(0.05, Math.max(0.001, (now - previous) / 1000));
  previous = now;
  const time = now / 1000;
  characters.sync(latestState, network.id, mode, gameScene.camera, time, dt);
  const controls = input.update(dt);
  hud.target(gameScene.highlightedKind);
  camera.update(dt, characters.localView(network.id));
  gameScene.update(time, dt);
  inputAccumulator += dt;
  if (network.id && inputAccumulator >= 0.05) {
    network.input(controls);
    inputAccumulator = 0;
  }
  gameScene.render();
  requestAnimationFrame(frame);
}

network.connect();
requestAnimationFrame(frame);
