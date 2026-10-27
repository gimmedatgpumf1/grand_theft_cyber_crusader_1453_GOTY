import * as THREE from './vendor/three.module.min.js';
import { CameraController } from './camera.js';
import { PRESETS, normalizeAppearance } from './appearance.js';
import { createHuman, disposeHuman, humanAssetsReady } from './human.js';

const WIDTH = 480;
const HEIGHT = 640;
const canvas = document.getElementById('fit');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(1);
renderer.setSize(WIDTH, HEIGHT, false);
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.08;

const scene = new THREE.Scene();
scene.background = new THREE.Color('#111713');
const ambient = new THREE.HemisphereLight('#f4f5ff', '#343b36', 0.8);
const key = new THREE.DirectionalLight('#ffffff', 1.9);
key.position.set(-3, 6, 5);
const fill = new THREE.DirectionalLight('#c9ddf2', 0.55);
fill.position.set(4, 3, 1);
const rim = new THREE.DirectionalLight('#ffffff', 0.9);
rim.position.set(0, 4, -5);
scene.add(ambient, key, fill, rim);

const camera = new THREE.PerspectiveCamera(22, WIDTH / HEIGHT, 0.02, 20);
const controller = new CameraController(camera);
controller.creatorActive = true;
controller.creatorFocus = 'face';
let view = null;

function booleans(fields) {
  const result = { ...fields };
  for (const [name, value] of Object.entries(result)) {
    if (typeof value !== 'string') continue;
    if (['true', '1', 'male', 'on'].includes(value)) result[name] = true;
    if (['false', '0', 'female', 'off'].includes(value)) result[name] = false;
  }
  return result;
}

function apply(name, candidate) {
  const presetName = candidate.preset || name;
  const preset = PRESETS[presetName];
  if (!preset) throw new Error(`Unknown fit preset ${presetName}`);
  const fields = booleans(candidate.fields || {});
  const appearance = normalizeAppearance({ ...preset, ...fields,
    morphs: candidate.morphs || preset.morphs });
  if (typeof candidate.male === 'boolean' && appearance.male !== candidate.male) {
    throw new Error(`${name}: expected ${candidate.male ? 'male' : 'female'} geometry, got ${appearance.male ? 'male' : 'female'}`);
  }
  if (view) disposeHuman(scene, view);
  view = createHuman(appearance);
  scene.add(view.group);
  const headParts = new Set(view.headParts);
  view.group.traverse(object => {
    if (object.isMesh) object.visible = headParts.has(object);
  });
  controller.setCreatorFocus('face');
  controller.setCreatorPose(0, { ...preset, ...fields });
  controller.distance = 1.75;
  controller.update(0, view);
  return { male: appearance.male, headOnly: true };
}

function draw() {
  if (!view) throw new Error('No fit candidate applied');
  controller.update(0, view);
  renderer.render(scene, camera);
}

window.fitHarness = {
  ready: true,
  apply,
  draw,
  assetsReady: () => Boolean(view && humanAssetsReady()),
  size: () => [WIDTH, HEIGHT],
};
