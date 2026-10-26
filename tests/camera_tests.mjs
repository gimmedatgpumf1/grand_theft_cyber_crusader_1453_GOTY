import assert from 'node:assert/strict';
import * as THREE from '../web/js/vendor/three.module.min.js';
import { CameraController } from '../web/js/camera.js';

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

console.log('Camera direction, mouse look, and godview speed tests passed.');
