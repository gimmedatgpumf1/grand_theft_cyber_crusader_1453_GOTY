import assert from 'node:assert/strict';
import * as THREE from '../web/js/vendor/three.module.min.js';
import { CameraController } from '../web/js/camera.js';
import { InputController } from '../web/js/input.js';

const nearZero = value => Math.abs(value) < 1e-8;
const controller = new CameraController(new THREE.PerspectiveCamera());

controller.yaw = Math.PI;
let move = controller.movement(new Set(['KeyD']));
assert.ok(move.x > 0.99 && nearZero(move.z), 'D must move right while facing -Z');
move = controller.movement(new Set(['KeyA']));
assert.ok(move.x < -0.99 && nearZero(move.z), 'A must move left while facing -Z');
move = controller.movement(new Set(['KeyW']));
assert.ok(move.z < -0.99 && nearZero(move.x), 'W must move forward while facing -Z');

controller.yaw = 0;
move = controller.movement(new Set(['KeyD']));
assert.ok(move.x < -0.99 && nearZero(move.z), 'D must move right while facing +Z');
move = controller.movement(new Set(['KeyW']));
assert.ok(move.z > 0.99 && nearZero(move.x), 'W must move forward while facing +Z');

controller.mode = 'first';
controller.yaw = Math.PI;
controller.pitch = 0;
controller.rotate(100, -100);
assert.ok(controller.yaw < Math.PI, 'Mouse-right must turn right');
assert.ok(Math.sin(controller.yaw) > 0, 'Mouse-right from -Z turns toward +X');
assert.ok(controller.pitch > 0, 'Mouse-up must look up in first person');

controller.mode = 'third';
controller.pitch = 0.34;
controller.rotate(0, 100);
assert.ok(controller.pitch > 0.34, 'Mouse-down must pitch a TPS camera downward');

controller.godFocus.set(0, 0, 0);
controller.godDistance = 16;
controller.pan(new Set(['KeyW']), 0.1);
assert.ok(controller.godFocus.length() > 1.6, 'Godview should use the faster base pan speed');
const normalDistance = controller.godFocus.length();
controller.godFocus.set(0, 0, 0);
controller.pan(new Set(['KeyW', 'ShiftLeft']), 0.1);
assert.ok(controller.godFocus.length() > normalDistance * 1.5, 'Shift must boost godview panning');

globalThis.window = { addEventListener() {}, innerWidth: 1280, innerHeight: 800 };
globalThis.document = { addEventListener() {}, pointerLockElement: null };
const canvas = { addEventListener() {}, style: {} };
const scene = { pick: () => null, setHighlight() {} };
const inputCamera = new CameraController(new THREE.PerspectiveCamera());
const input = new InputController(canvas, inputCamera, {}, scene, {
  isHelpOpen: () => false,
  isInputBlocked: () => false,
  localPosition: () => new THREE.Vector3(),
  localYaw: () => Math.PI,
  onMode() {},
});
input.mode = inputCamera.mode = 'third';
inputCamera.yaw = Math.PI;
input.keys.add('KeyD');
let controls = input.update(0.05);
assert.ok(controls.x > 0.99 && nearZero(controls.z), 'Input must send world-space D as protocol x/z');
input.keys.clear();
input.keys.add('KeyW');
controls = input.update(0.05);
assert.ok(controls.z < -0.99 && nearZero(controls.x), 'Input must send world-space W as protocol x/z');
input.setMode('god');
controls = input.update(0.05);
assert.ok(nearZero(controls.x) && nearZero(controls.z), 'Godview pans without moving the citizen');

console.log('Camera, input protocol, mouse look, and godview speed tests passed.');
