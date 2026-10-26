import { Network, defaultIdentity } from './network.js';
import { GameScene } from './scene.js';
import { CharacterManager } from './characters.js';
import { CameraController } from './camera.js';
import { InputController } from './input.js';
import { HUD } from './hud.js';

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
let latestState = { characters: [], puddles: [], events: [], tick: 0, toiletUser: 0 };
let mode = 'third';
let helpOpen = false;

function toggleHelp(force) {
  helpOpen = typeof force === 'boolean' ? force : !helpOpen;
  hud.help(helpOpen);
  input.clear();
  if (helpOpen && document.pointerLockElement) document.exitPointerLock?.();
}

const input = new InputController(document.getElementById('world'), camera, network, gameScene, {
  onMode(newMode) {
    mode = newMode;
    hud.setMode(mode);
    gameScene.setMode(mode);
    if (mode === 'first') hud.toast('Click the view to capture your mouse. Hold F to piss.');
    if (mode === 'third') hud.toast('Click the view for mouse look. WASD moves; E interacts.');
    if (mode === 'god') hud.toast('Godview: WASD fast-pans; Shift boosts. Clicks move your citizen.');
  },
  onHelp: toggleHelp,
  isHelpOpen: () => helpOpen,
  localPosition: () => characters.localView(network.id)?.group.position || null,
  localYaw: () => characters.localView(network.id)?.data.yaw ?? Math.PI,
});

network.onStatus = status => hud.network(status);
network.onWelcome = message => {
  latestState = { characters: [], puddles: [], events: [], tick: 0, toiletUser: 0 };
  gameScene.buildWorld(message.world);
  gameScene.setMode(mode);
  hud.lastNotice = 0;
  hud.welcome(message, network.identity);
  hud.toast(`Welcome, ${network.identity}. The throne is marked in acid green.`);
};
network.onState = state => {
  latestState = state;
  gameScene.syncPuddles(state.puddles);
  if (network.world) hud.update(state, network.id, network.world);
};

hud.setMode(mode);
document.querySelectorAll('[data-mode]').forEach(button => {
  button.addEventListener('click', () => input.setMode(button.dataset.mode));
});
document.getElementById('help-button').addEventListener('click', () => toggleHelp(true));
document.getElementById('close-help').addEventListener('click', () => toggleHelp(false));
document.getElementById('help').addEventListener('mousedown', event => {
  if (event.target.id === 'help') toggleHelp(false);
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
