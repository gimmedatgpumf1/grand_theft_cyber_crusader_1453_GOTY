'use strict';

// Skeleton Studio: browser-only WebGL viewer for the real fitted bone meshes.
const $ = id => document.getElementById(id);
const { TARGET_NAMES, formulaTargets, parseOBJ, fitVertical, mapPoint, makeBaseFit, transformToFit } = window.SkeletonFitGeometry;
const JOINT_NAMES = ['neck', 'shoulderL', 'elbowL', 'wristL', 'shoulderR', 'elbowR', 'wristR', 'hipL', 'kneeL', 'ankleL', 'hipR', 'kneeR', 'ankleR'];
const MAX_BODIES = 4;
const JOINT_MARKERS = [
  ['neck', 'neck', []], ['shoulderL', 'shoulderL', []], ['elbowL', 'elbowL', ['shoulderL']], ['wristL', 'wristL', ['shoulderL', 'elbowL']],
  ['shoulderR', 'shoulderR', []], ['elbowR', 'elbowR', ['shoulderR']], ['wristR', 'wristR', ['shoulderR', 'elbowR']],
  ['hipL', 'hipL', []], ['kneeL', 'kneeL', ['hipL']], ['ankleL', 'ankleL', ['hipL', 'kneeL']],
  ['hipR', 'hipR', []], ['kneeR', 'kneeR', ['hipR']], ['ankleR', 'ankleR', ['hipR', 'kneeR']]
];
const COLORS = {
  head: [0.82, 0.69, 0.48], neck: [0.89, 0.76, 0.56], torso: [0.78, 0.80, 0.77],
  arm: [0.89, 0.77, 0.58], leg: [0.76, 0.72, 0.61], hip: [0.70, 0.78, 0.82], shoulder: [0.76, 0.80, 0.77]
};
const canvas = $('view');
const gl = canvas.getContext('webgl', { antialias: true, alpha: false, powerPreference: 'high-performance' });
let program = null, attribs = null, uniform = null, gridBuffers = null;
let bodies = [], selectedId = null, idCounter = 1, drag = null, sourceCache = {};
const camera = { yaw: 0.16, pitch: 0.10, distance: 4.1, target: [0, 0.86, 0], start: [0.16, 0.10, 4.1, 0, 0.86, 0] };
const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
function rotatePoint(point, pivot, degrees) {
  let x = point[0] - pivot[0], y = point[1] - pivot[1], z = point[2] - pivot[2];
  const rx = degrees[0] * Math.PI / 180, ry = degrees[1] * Math.PI / 180, rz = degrees[2] * Math.PI / 180;
  let c = Math.cos(rx), s = Math.sin(rx); [y, z] = [y * c - z * s, y * s + z * c];
  c = Math.cos(ry); s = Math.sin(ry); [x, z] = [x * c + z * s, -x * s + z * c];
  c = Math.cos(rz); s = Math.sin(rz); [x, y] = [x * c - y * s, x * s + y * c];
  return [x + pivot[0], y + pivot[1], z + pivot[2]];
}
function transformJointChain(point, body, chain) {
  const nodes = [];
  for (const id of chain) {
    let pivot = body.pivots[id].slice();
    for (const prior of nodes) pivot = rotatePoint(pivot, prior.pivot, prior.angles);
    nodes.push({ pivot, angles: body.joints[id] });
  }
  let result = point.slice();
  for (const node of nodes) result = rotatePoint(result, node.pivot, node.angles);
  return result;
}
function boneChain(name) {
  if (['Head', 'Jaw', 'C1'].includes(name) || /^C[2-7]$/.test(name)) return ['neck'];
  const side = name.endsWith('_L') ? 'L' : 'R';
  if (name.startsWith('Scapula_') || name.startsWith('Clavicula_')) return [`shoulder${side}`];
  if (name.startsWith('Humerus_')) return [`shoulder${side}`];
  if (name.startsWith('Radius_')) return [`shoulder${side}`, `elbow${side}`];
  if (name.startsWith('Hand')) return [`shoulder${side}`, `elbow${side}`, `wrist${side}`];
  if (name.startsWith('Femur_')) return [`hip${side}`];
  if (name.startsWith('Patella_') || name.startsWith('Tibia_')) return [`hip${side}`, `knee${side}`];
  if (name.startsWith('Foot') || name.startsWith('Tal_')) return [`hip${side}`, `knee${side}`, `ankle${side}`];
  return [];
}

function bounds(vertices) {
  const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  for (const p of vertices) for (let i = 0; i < 3; i++) { min[i] = Math.min(min[i], p[i]); max[i] = Math.max(max[i], p[i]); }
  return { min, max, center: min.map((v, i) => (v + max[i]) * .5) };
}
function localBoxCorners(box) {
  const points = [];
  for (const x of [box.min[0], box.max[0]]) for (const y of [box.min[1], box.max[1]]) for (const z of [box.min[2], box.max[2]]) points.push([x, y, z]);
  return points;
}
function updateGroundHeight(body) {
  if (!body.meshBounds) return;
  const floor = Math.min(...localBoxCorners(body.meshBounds).map(p => rotatePoint(p, [0, 0, 0], [body.tilt, body.yaw, 0])[1]));
  body.worldY = -floor;
}
function worldBounds(body) {
  const points = localBoxCorners(body.meshBounds).map(p => {
    const q = rotatePoint(p, [0, 0, 0], [body.tilt, body.yaw, 0]);
    return [q[0] + body.worldX, q[1] + body.worldY, q[2] + body.worldZ];
  });
  const min = [0, 1, 2].map(i => Math.min(...points.map(p => p[i])));
  const max = [0, 1, 2].map(i => Math.max(...points.map(p => p[i])));
  return { min, max };
}
function mergedBounds(meshes, predicate) {
  const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  for (const obj of meshes) if (predicate(obj.name)) for (const p of obj.vertices) for (let i = 0; i < 3; i++) { min[i] = Math.min(min[i], p[i]); max[i] = Math.max(max[i], p[i]); }
  if (!Number.isFinite(min[0])) return { min: [0, 0, 0], max: [0, 0, 0], center: [0, 0, 0] };
  return { min, max, center: min.map((v, i) => (v + max[i]) * .5) };
}
function createJointState() { return Object.fromEntries(JOINT_NAMES.map(name => [name, [0, 0, 0]])); }
function buildPivots(body) {
  const fit = body.fit, meta = body.meta, meshes = body.meshes;
  const byName = new Map(meshes.map(obj => [obj.name, obj]));
  const partBounds = new Map(meshes.map(obj => [obj.name, bounds(obj.vertices)]));
  const boundsOf = name => partBounds.get(name) || { min: [0, 0, 0], max: [0, 0, 0], center: [0, 0, 0] };
  const femurL = boundsOf('Femur_L'), femurR = boundsOf('Femur_R');
  const tibiaL = boundsOf('Tibia_L'), tibiaR = boundsOf('Tibia_R');
  const footL = mergedBounds(meshes, name => /^(Foot|Tal)_.*_L$/.test(name));
  const footR = mergedBounds(meshes, name => /^(Foot|Tal)_.*_R$/.test(name));
  const radiusL = boundsOf('Radius_L'), radiusR = boundsOf('Radius_R');
  const arm = meta.arm_points_m;
  const sidePivot = side => {
    const suffix = side === 'L' ? 'left' : 'right';
    const shoulder = mapPoint(arm[`${suffix}_shoulder`], 'girdle', side === 'L' ? 1 : -1, false, fit);
    const elbow = mapPoint(arm[`${suffix}_elbow`], 'arm', side === 'L' ? 1 : -1, false, fit);
    const radius = side === 'L' ? radiusL : radiusR;
    const femur = side === 'L' ? femurL : femurR;
    const tibia = side === 'L' ? tibiaL : tibiaR;
    const foot = side === 'L' ? footL : footR;
    const sideSign = side === 'L' ? 1 : -1;
    const wrist = [sideSign > 0 ? radius.max[0] : radius.min[0], radius.center[1], radius.center[2]];
    const hip = [femur.center[0], fit.mapY(fit.hipSourceY), femur.center[2]];
    const knee = [(femur.center[0] + tibia.center[0]) * .5, (femur.min[1] + tibia.max[1]) * .5, (femur.center[2] + tibia.center[2]) * .5];
    const ankle = [(tibia.center[0] + foot.center[0]) * .5, (tibia.min[1] + foot.max[1]) * .5, (tibia.center[2] + foot.center[2]) * .5];
    return { [`shoulder${side}`]: shoulder, [`elbow${side}`]: elbow, [`wrist${side}`]: wrist, [`hip${side}`]: hip, [`knee${side}`]: knee, [`ankle${side}`]: ankle };
  };
  body.partBounds = partBounds;
  body.pivots = {
    neck: [0, fit.c7, meta.source_landmarks_m.neck_contact_z * fit.heightScale],
    ...sidePivot('L'), ...sidePivot('R')
  };
  body.meshBounds = mergedBounds(meshes, () => true);
  body.namedObjects = byName;
}

function createFitBody(sex, height) {
  const source = awaitSource(sex);
  const fit = fitVertical(source.meta, height, formulaTargets(sex, height));
  const meshes = transformToFit(source.mesh, makeBaseFit(source.meta), fit);
  const body = {
    id: idCounter++, sex, height, meta: source.meta, sourceMesh: source.mesh,
    fit, meshes, joints: createJointState(), worldX: 0, worldY: 0, worldZ: 0, yaw: 0, tilt: 0,
    buffers: null, markerBuffer: null, drawCount: 0, markerCount: 0, partBounds: null, pivots: null, meshBounds: null
  };
  buildPivots(body);
  updateGroundHeight(body);
  uploadBody(body);
  return body;
}
function awaitSource(sex) {
  const value = sourceCache[sex];
  if (!value) throw new Error(`The ${sex} mesh has not finished loading yet.`);
  return value;
}
async function loadSource(sex) {
  if (sourceCache[sex]) return sourceCache[sex];
  const [objResponse, jsonResponse] = await Promise.all([
    fetch(`standard_blue_${sex}.obj`), fetch(`standard_blue_${sex}.json`)
  ]);
  if (!objResponse.ok || !jsonResponse.ok) throw new Error('Could not load the source OBJ and JSON. Open this page from the export folder through a local web server.');
  const [objText, meta] = await Promise.all([objResponse.text(), jsonResponse.json()]);
  const mesh = parseOBJ(objText);
  sourceCache[sex] = { mesh, meta };
  return sourceCache[sex];
}

function packGeometry(body) {
  let vertexCount = 0;
  for (const obj of body.meshes) vertexCount += obj.faces.length * 3;
  const positions = new Float32Array(vertexCount * 3);
  const normals = new Float32Array(vertexCount * 3);
  const colors = new Float32Array(vertexCount * 3);
  let cursor = 0;
  for (const obj of body.meshes) {
    const color = COLORS[obj.category] || [.80, .77, .68];
    const chain = boneChain(obj.name);
    for (const face of obj.faces) {
      const q = face.map(index => transformJointChain(obj.vertices[index], body, chain));
      const a = q[0], b = q[1], c = q[2];
      const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
      const vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
      let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      const length = Math.hypot(nx, ny, nz) || 1;
      nx /= length; ny /= length; nz /= length;
      for (const p of q) {
        positions[cursor] = p[0]; normals[cursor] = nx; colors[cursor++] = color[0];
        positions[cursor] = p[1]; normals[cursor] = ny; colors[cursor++] = color[1];
        positions[cursor] = p[2]; normals[cursor] = nz; colors[cursor++] = color[2];
      }
    }
  }
  return { positions, normals, colors, vertexCount };
}
function jointMarkerPoints(body) {
  const entries = JOINT_MARKERS;
  const positions = new Float32Array(entries.length * 3);
  const normals = new Float32Array(entries.length * 3);
  const colors = new Float32Array(entries.length * 3);
  let n = 0;
  for (const [label, pivotId, chain] of entries) {
    const p = transformJointChain(body.pivots[pivotId], body, chain);
    const selected = label === $('jointSelect').value;
    positions.set(p, n);
    normals.set([0, 1, 0], n);
    colors.set(selected ? [0.88, 0.48, 0.08] : [0.15, 0.45, 0.74], n);
    n += 3;
  }
  return { positions, normals, colors, count: entries.length };
}
function uploadBody(body) {
  const packed = packGeometry(body);
  if (!body.buffers) body.buffers = { position: gl.createBuffer(), normal: gl.createBuffer(), color: gl.createBuffer() };
  const items = [['position', packed.positions], ['normal', packed.normals], ['color', packed.colors]];
  for (const [name, data] of items) {
    gl.bindBuffer(gl.ARRAY_BUFFER, body.buffers[name]);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
  }
  body.drawCount = packed.vertexCount;
  const marker = jointMarkerPoints(body);
  if (!body.markerBuffers) body.markerBuffers = { position: gl.createBuffer(), normal: gl.createBuffer(), color: gl.createBuffer() };
  for (const [name, data] of [['position', marker.positions], ['normal', marker.normals], ['color', marker.colors]]) {
    gl.bindBuffer(gl.ARRAY_BUFFER, body.markerBuffers[name]);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
  }
  body.markerCount = marker.count;
}
function refreshBodyGeometry(body) {
  body.fit = fitVertical(body.meta, body.height, formulaTargets(body.sex, body.height));
  body.meshes = transformToFit(body.sourceMesh, makeBaseFit(body.meta), body.fit);
  buildPivots(body);
  updateGroundHeight(body);
  uploadBody(body);
}

function shader(type, source) {
  const result = gl.createShader(type);
  gl.shaderSource(result, source);
  gl.compileShader(result);
  if (!gl.getShaderParameter(result, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(result));
  return result;
}
function initGL() {
  if (!gl) throw new Error('This browser could not create a WebGL context. Try a browser with hardware acceleration enabled.');
  const vertex = shader(gl.VERTEX_SHADER, `
    attribute vec3 aPosition; attribute vec3 aNormal; attribute vec3 aColor;
    uniform mat4 uMvp; uniform mat4 uModel; uniform vec3 uTint; uniform float uPointSize;
    varying vec3 vNormal; varying vec3 vColor;
    void main(){ gl_Position=uMvp*vec4(aPosition,1.0); gl_PointSize=uPointSize; vNormal=mat3(uModel)*aNormal; vColor=aColor*uTint; }
  `);
  const fragment = shader(gl.FRAGMENT_SHADER, `
    precision mediump float; varying vec3 vNormal; varying vec3 vColor; uniform float uRoundPoints;
    void main(){
      vec3 n=normalize(vNormal); if(!gl_FrontFacing)n=-n;
      vec3 light=normalize(vec3(-0.35,0.72,0.62));
      float diffuse=max(dot(n,light),0.0);
      float shade=0.73+0.27*diffuse;
      if(uRoundPoints>0.5){ vec2 q=gl_PointCoord-vec2(0.5); if(dot(q,q)>0.25) discard; }
      gl_FragColor=vec4(vColor*shade,1.0);
    }
  `);
  program = gl.createProgram();
  gl.attachShader(program, vertex); gl.attachShader(program, fragment); gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
  gl.useProgram(program);
  attribs = { position: gl.getAttribLocation(program, 'aPosition'), normal: gl.getAttribLocation(program, 'aNormal'), color: gl.getAttribLocation(program, 'aColor') };
  uniform = {
    mvp: gl.getUniformLocation(program, 'uMvp'), model: gl.getUniformLocation(program, 'uModel'),
    tint: gl.getUniformLocation(program, 'uTint'), pointSize: gl.getUniformLocation(program, 'uPointSize'), roundPoints: gl.getUniformLocation(program, 'uRoundPoints')
  };
  gl.enable(gl.DEPTH_TEST);
  gl.disable(gl.CULL_FACE);
  gl.clearColor(.965, .976, .986, 1);
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
  createGroundGrid();
}
function createGroundGrid() {
  const positions = [], normals = [], colors = [];
  const extent = 12;
  const addLine = (a, b, color) => {
    positions.push(...a, ...b);
    normals.push(0, 1, 0, 0, 1, 0);
    colors.push(...color, ...color);
  };
  for (let i = -extent; i <= extent; i++) {
    const color = i === 0 ? [.61, .71, .80] : [.84, .87, .89];
    addLine([-extent, -.006, i], [extent, -.006, i], color);
    addLine([i, -.006, -extent], [i, -.006, extent], color);
  }
  gridBuffers = { position: gl.createBuffer(), normal: gl.createBuffer(), color: gl.createBuffer(), count: positions.length / 3 };
  for (const [name, data] of [['position', positions], ['normal', normals], ['color', colors]]) {
    gl.bindBuffer(gl.ARRAY_BUFFER, gridBuffers[name]);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(data), gl.STATIC_DRAW);
  }
}

function identity() { return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; }
function multiply(a, b) {
  const out = new Array(16);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  return out;
}
function translate(x, y, z) { const m = identity(); m[12] = x; m[13] = y; m[14] = z; return m; }
function rotateX(a) { const c = Math.cos(a), s = Math.sin(a); return [1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1]; }
function rotateY(a) { const c = Math.cos(a), s = Math.sin(a); return [c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1]; }
function rotateZ(a) { const c = Math.cos(a), s = Math.sin(a); return [c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; }
function perspective(fov, aspect, near, far) {
  const f = 1 / Math.tan(fov * .5), nf = 1 / (near - far);
  return [f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0];
}
function lookAt(eye, center, up) {
  const normalize = v => { const d = Math.hypot(...v) || 1; return v.map(x => x / d); };
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const z = normalize(eye.map((v, i) => v - center[i]));
  const x = normalize(cross(up, z));
  const y = cross(z, x);
  return [x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
    -x.reduce((s, v, i) => s + v * eye[i], 0), -y.reduce((s, v, i) => s + v * eye[i], 0), -z.reduce((s, v, i) => s + v * eye[i], 0), 1];
}
function bodyMatrix(body) {
  const yaw = body.yaw * Math.PI / 180, tilt = body.tilt * Math.PI / 180;
  return multiply(translate(body.worldX, body.worldY, body.worldZ), multiply(rotateY(yaw), rotateX(tilt)));
}
function bindAttributes(positionBuffer, normalBuffer, colorBuffer) {
  for (const [location, buffer] of [[attribs.position, positionBuffer], [attribs.normal, normalBuffer], [attribs.color, colorBuffer]]) {
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.enableVertexAttribArray(location);
    gl.vertexAttribPointer(location, 3, gl.FLOAT, false, 0, 0);
  }
}
function draw() {
  if (!gl || !program) return;
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  const width = Math.max(1, Math.floor(canvas.clientWidth * ratio));
  const height = Math.max(1, Math.floor(canvas.clientHeight * ratio));
  if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
  gl.viewport(0, 0, width, height);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  const projection = perspective(.67, width / height, .05, 100);
  const cp = Math.cos(camera.pitch), sp = Math.sin(camera.pitch);
  const offset = [camera.distance * cp * Math.sin(camera.yaw), camera.distance * sp, camera.distance * cp * Math.cos(camera.yaw)];
  const eye = camera.target.map((v, i) => v + offset[i]);
  const view = lookAt(eye, camera.target, [0, 1, 0]);
  gl.useProgram(program);
  const gridModel = identity();
  gl.uniformMatrix4fv(uniform.model, false, new Float32Array(gridModel));
  gl.uniformMatrix4fv(uniform.mvp, false, new Float32Array(multiply(projection, view)));
  gl.uniform3f(uniform.tint, 1, 1, 1);
  gl.uniform1f(uniform.pointSize, 1);
  gl.uniform1f(uniform.roundPoints, 0);
  bindAttributes(gridBuffers.position, gridBuffers.normal, gridBuffers.color);
  gl.drawArrays(gl.LINES, 0, gridBuffers.count);
  for (const body of bodies) {
    if (!body.drawCount) continue;
    const model = bodyMatrix(body);
    gl.uniformMatrix4fv(uniform.model, false, new Float32Array(model));
    gl.uniformMatrix4fv(uniform.mvp, false, new Float32Array(multiply(projection, multiply(view, model))));
    const tint = body.id === selectedId ? [1, 1, 1] : [.79, .84, .88];
    gl.uniform3f(uniform.tint, ...tint);
    gl.uniform1f(uniform.pointSize, body.id === selectedId ? 9 : 7);
    gl.uniform1f(uniform.roundPoints, 0);
    bindAttributes(body.buffers.position, body.buffers.normal, body.buffers.color);
    gl.drawArrays(gl.TRIANGLES, 0, body.drawCount);
    if ($('showJoints').checked && body.markerCount) {
      gl.uniform1f(uniform.roundPoints, 1);
      bindAttributes(body.markerBuffers.position, body.markerBuffers.normal, body.markerBuffers.color);
      gl.drawArrays(gl.POINTS, 0, body.markerCount);
    }
  }
}

function projectWorldPoint(point, rect) {
  const aspect = rect.width / Math.max(1, rect.height);
  const projection = perspective(.67, aspect, .05, 100);
  const cp = Math.cos(camera.pitch), sp = Math.sin(camera.pitch);
  const offset = [camera.distance * cp * Math.sin(camera.yaw), camera.distance * sp, camera.distance * cp * Math.cos(camera.yaw)];
  const eye = camera.target.map((value, index) => value + offset[index]);
  const view = lookAt(eye, camera.target, [0, 1, 0]);
  const matrix = multiply(projection, view);
  const x = point[0], y = point[1], z = point[2];
  const clipX = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12];
  const clipY = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13];
  const clipW = matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15];
  if (clipW <= 0) return null;
  return [rect.left + (clipX / clipW + 1) * .5 * rect.width, rect.top + (1 - clipY / clipW) * .5 * rect.height];
}
function selectMarkerAt(clientX, clientY) {
  const rect = canvas.getBoundingClientRect();
  let nearest = null;
  for (const body of bodies) for (const [label, pivotId, chain] of JOINT_MARKERS) {
    const local = transformJointChain(body.pivots[pivotId], body, chain);
    const screen = projectWorldPoint(transformLocalToWorld(body, local), rect);
    if (!screen) continue;
    const distance = Math.hypot(screen[0] - clientX, screen[1] - clientY);
    if (distance <= 16 && (!nearest || distance < nearest.distance)) nearest = { body, label, distance };
  }
  if (!nearest) return false;
  selectBody(nearest.body.id);
  $('jointSelect').value = nearest.label;
  syncJointSliders();
  return true;
}

function disposeBody(body) {
  if (!gl || !body.buffers) return;
  gl.deleteBuffer(body.buffers.position); gl.deleteBuffer(body.buffers.normal); gl.deleteBuffer(body.buffers.color);
  if (body.markerBuffers) {
    gl.deleteBuffer(body.markerBuffers.position); gl.deleteBuffer(body.markerBuffers.normal); gl.deleteBuffer(body.markerBuffers.color);
  }
}
function fitAllBodies() {
  if (!bodies.length) return;
  const boxes = bodies.map(worldBounds);
  const min = [0, 1, 2].map(axis => Math.min(...boxes.map(box => box.min[axis])));
  const max = [0, 1, 2].map(axis => Math.max(...boxes.map(box => box.max[axis])));
  const spans = max.map((value, axis) => value - min[axis]);
  camera.target = min.map((value, axis) => (value + max[axis]) * .5);
  const aspect = Math.max(.5, canvas.clientWidth / Math.max(1, canvas.clientHeight));
  const tan = Math.tan(.67 * .5);
  const projectedWidth = Math.abs(Math.cos(camera.yaw)) * spans[0] + Math.abs(Math.sin(camera.yaw)) * spans[2];
  const projectedHeight = Math.abs(Math.cos(camera.pitch)) * spans[1] + Math.abs(Math.sin(camera.pitch)) * spans[2];
  const horizontal = projectedWidth / (2 * tan * aspect);
  const vertical = projectedHeight / (2 * tan);
  camera.distance = clamp(Math.max(2.4, horizontal, vertical) * 1.2, 1.5, 12);
  $('zoomRange').value = camera.distance;
  updateZoomText();
  draw();
}
function resetCamera() {
  [camera.yaw, camera.pitch, camera.distance, camera.target[0], camera.target[1], camera.target[2]] = camera.start;
  $('zoomRange').value = camera.distance;
  updateZoomText();
  draw();
}
function updateZoomText() { $('zoomValue').textContent = `${camera.distance.toFixed(2)} m`; }
function zoomBy(factor) {
  camera.distance = clamp(camera.distance * factor, 1.5, 12);
  $('zoomRange').value = camera.distance;
  updateZoomText(); draw();
}

function arrangeBodies() {
  bodies.forEach((body, index) => { body.worldX = (index - (bodies.length - 1) * .5) * 2.25; });
  camera.target[0] = bodies.length ? bodies.reduce((sum, b) => sum + b.worldX, 0) / bodies.length : 0;
  renderBodyList(); syncSelectedControls(); draw();
}
function renderBodyList() {
  $('bodyCount').textContent = `${bodies.length} / ${MAX_BODIES}`;
  $('bodyList').innerHTML = '';
  for (const body of bodies) {
    const card = document.createElement('div');
    card.className = `body-card${body.id === selectedId ? ' selected' : ''}`;
    const pick = document.createElement('button');
    pick.className = 'body-pick';
    pick.innerHTML = `<strong>${body.sex === 'male' ? 'Male' : 'Female'} · ${body.height.toFixed(2)} m</strong><small>Body ${bodies.indexOf(body) + 1} · X ${body.worldX.toFixed(2)} m</small>`;
    pick.addEventListener('click', () => selectBody(body.id));
    const remove = document.createElement('button');
    remove.className = 'body-x'; remove.title = 'Remove this body'; remove.textContent = '×';
    remove.addEventListener('click', () => removeBody(body.id));
    card.append(pick, remove); $('bodyList').append(card);
  }
  $('removeBody').disabled = !bodies.length;
  $('downloadObj').disabled = !bodies.length;
  $('downloadJson').disabled = !bodies.length;
  $('spawnBody').disabled = bodies.length >= MAX_BODIES;
  $('reachTargetBody').innerHTML = bodies.map((body, i) => `<option value="${body.id}">${body.sex} ${body.height.toFixed(2)} m · Body ${i + 1}</option>`).join('');
  if (bodies.length && !bodies.some(body => String(body.id) === $('reachTargetBody').value)) $('reachTargetBody').value = String(bodies[0].id);
}
function selectBody(id) {
  selectedId = id;
  renderBodyList(); syncSelectedControls(); draw();
}
function selectedBody() { return bodies.find(body => body.id === selectedId) || null; }
function removeBody(id) {
  const body = bodies.find(item => item.id === id);
  if (!body) return;
  disposeBody(body);
  bodies = bodies.filter(item => item.id !== id);
  if (selectedId === id) selectedId = bodies[0]?.id ?? null;
  renderBodyList(); syncSelectedControls(); updateTargets(); draw();
}
async function addBody(sex, height, arranged = true) {
  if (bodies.length >= MAX_BODIES) return;
  $('headerStatus').textContent = `Loading ${sex} source mesh…`;
  $('fitStatus').textContent = 'Loading and fitting the source bone geometry…';
  try {
    await loadSource(sex);
    const body = createFitBody(sex, clamp(Number(height) || 1.72, .5, 2.5));
    bodies.push(body); selectedId = body.id;
    if (arranged) arrangeBodies();
    else { renderBodyList(); syncSelectedControls(); draw(); }
    updateTargets(); updateFitSummary();
    $('headerStatus').textContent = `${bodies.length} comparison ${bodies.length === 1 ? 'body' : 'bodies'} · ${body.meshes.length} bone meshes`;
  } catch (error) {
    $('headerStatus').textContent = 'Could not load the source mesh';
    $('fitStatus').className = 'status error'; $('fitStatus').textContent = error.message;
  }
}
async function changeBodySex(body, sex) {
  if (!body || body.sex === sex) return;
  $('fitStatus').textContent = 'Loading the alternate source skeleton…';
  try {
    await loadSource(sex);
    const replacement = createFitBody(sex, body.height);
    disposeBody(body);
    Object.assign(body, replacement, { id: body.id, worldX: body.worldX, worldZ: body.worldZ, yaw: body.yaw, tilt: body.tilt });
    updateGroundHeight(body);
    renderBodyList(); syncSelectedControls(); updateTargets(); updateFitSummary(); draw();
  } catch (error) { $('fitStatus').className = 'status error'; $('fitStatus').textContent = error.message; }
}
function syncSelectedControls() {
  const body = selectedBody();
  const disabled = !body;
  for (const id of ['bodyHeight', 'bodyHeightRange', 'bodySex', 'worldX', 'worldZ', 'worldYaw', 'worldTilt', 'jointSelect', 'jointX', 'jointY', 'jointZ', 'posePreset', 'applyPose', 'resetJoint', 'resetPose']) $(id).disabled = disabled;
  if (!body) {
    $('activeBodyLabel').textContent = 'No body selected';
    $('sceneLabel').textContent = 'No skeletons in view';
    $('sceneSub').textContent = 'Spawn a body to start a comparison';
    return;
  }
  $('activeBodyLabel').textContent = `${body.sex} · ${body.height.toFixed(2)} m`;
  $('bodyHeight').value = body.height.toFixed(2); $('bodyHeightRange').value = body.height;
  $('bodySex').value = body.sex;
  $('worldX').value = body.worldX.toFixed(2); $('worldZ').value = body.worldZ.toFixed(2);
  $('worldYaw').value = body.yaw.toFixed(0); $('worldTilt').value = body.tilt.toFixed(0);
  syncJointSliders(); updateSceneLabel();
}
function updateSceneLabel() {
  $('sceneLabel').textContent = `${bodies.length} ${bodies.length === 1 ? 'skeleton' : 'skeletons'} · ${bodies.reduce((n, b) => n + b.meshes.length, 0)} bone meshes`;
  $('sceneSub').textContent = selectedBody() ? `Selected: ${selectedBody().sex} · ${selectedBody().height.toFixed(2)} m · actual source geometry` : 'Spawn a body to start a comparison';
}
function updateFitSummary() {
  const body = selectedBody();
  if (!body) return;
  const status = $('fitStatus');
  status.className = `status ${body.fit.blended ? '' : 'good'}`;
  status.textContent = body.fit.blended ? 'A safe head / neck / pelvis blend was used. See the fitted column for the achieved values.' : 'Formula fit ready. The preview uses proportion-specific transforms, not one uniform body scale.';
  updateSceneLabel();
}
function updateTargets() {
  const body = selectedBody();
  if (!body) { $('targets').innerHTML = ''; return; }
  const fit = body.fit;
  $('targets').innerHTML = `<div class="measurement-grid"><style>.measurement-grid{display:grid;grid-template-columns:1fr auto auto;gap:4px 10px;font-size:11px}.measurement-grid b{text-align:right;font-variant-numeric:tabular-nums}.measurement-grid .muted{color:#8190a0}</style><span class="muted">Measure (m)</span><span class="muted">Target</span><span class="muted">Fitted</span>${TARGET_NAMES.map((name, i) => `<span>${name}</span><b>${fit.requestedArray[i].toFixed(3)}</b><b>${fit.fittedArray[i].toFixed(3)}</b>`).join('')}</div>`;
}
function syncJointSliders() {
  const body = selectedBody();
  if (!body) return;
  const angles = body.joints[$('jointSelect').value];
  for (let i = 0; i < 3; i++) { const axis = 'XYZ'[i]; $(`joint${axis}`).value = angles[i]; $(`joint${axis}Val`).textContent = `${angles[i]}°`; }
  uploadBody(body); draw();
}
function updateJointFromUI() {
  const body = selectedBody(); if (!body) return;
  body.joints[$('jointSelect').value] = ['X', 'Y', 'Z'].map(axis => Number($(`joint${axis}`).value));
  for (const axis of ['X', 'Y', 'Z']) $(`joint${axis}Val`).textContent = `${$(`joint${axis}`).value}°`;
  uploadBody(body); draw();
}
function resetPose(body) { body.joints = createJointState(); }
function applyPose(body, preset) {
  resetPose(body);
  body.tilt = 0;
  const set = (key, values) => { body.joints[key] = values.slice(); };
  if (preset === 'sitting') {
    set('hipL', [-78, 0, 0]); set('hipR', [-78, 0, 0]);
    set('kneeL', [78, 0, 0]); set('kneeR', [78, 0, 0]);
    set('shoulderL', [0, 0, -22]); set('shoulderR', [0, 0, 22]);
  } else if (preset === 'squatting') {
    set('hipL', [-55, 0, -12]); set('hipR', [-55, 0, 12]);
    set('kneeL', [100, 0, 0]); set('kneeR', [100, 0, 0]);
    set('ankleL', [-38, 0, 0]); set('ankleR', [-38, 0, 0]);
    set('shoulderL', [0, 0, -35]); set('shoulderR', [0, 0, 35]);
  } else if (preset === 'kneeling') {
    set('hipL', [-25, 0, -8]); set('hipR', [-25, 0, 8]);
    set('kneeL', [125, 0, 0]); set('kneeR', [125, 0, 0]);
    set('ankleL', [-95, 0, 0]); set('ankleR', [-95, 0, 0]);
  } else if (preset === 'allfours') {
    set('hipL', [-72, 0, -10]); set('hipR', [-72, 0, 10]);
    set('kneeL', [95, 0, 0]); set('kneeR', [95, 0, 0]);
    set('shoulderL', [0, 0, -58]); set('shoulderR', [0, 0, 58]);
    set('elbowL', [0, 0, -18]); set('elbowR', [0, 0, 18]);
  } else if (preset === 'elbowsKnees') {
    set('hipL', [-68, 0, -8]); set('hipR', [-68, 0, 8]);
    set('kneeL', [100, 0, 0]); set('kneeR', [100, 0, 0]);
    set('shoulderL', [0, 0, -62]); set('shoulderR', [0, 0, 62]);
    set('elbowL', [0, 0, -82]); set('elbowR', [0, 0, 82]);
  } else if (preset === 'onBelly') {
    body.tilt = 90;
    set('hipL', [-8, 0, -8]); set('hipR', [-8, 0, 8]);
    set('kneeL', [28, 0, 0]); set('kneeR', [28, 0, 0]);
  } else if (preset === 'onBack') {
    body.tilt = -90;
    set('hipL', [12, 0, -8]); set('hipR', [12, 0, 8]);
    set('kneeL', [-32, 0, 0]); set('kneeR', [-32, 0, 0]);
  } else if (preset === 'armsRaised') {
    set('shoulderL', [0, 0, 145]); set('shoulderR', [0, 0, -145]);
  }
  updateGroundHeight(body);
  $('worldTilt').value = body.tilt.toFixed(0);
  uploadBody(body); syncJointSliders();
}

function transformLocalToWorld(body, p) {
  const q = rotatePoint(p, [0, 0, 0], [body.tilt, body.yaw, 0]);
  return [q[0] + body.worldX, q[1] + body.worldY, q[2] + body.worldZ];
}
function boneCenter(body, predicate) { return mergedBounds(body.meshes, predicate).center; }
function targetLocalPoint(body, target) {
  const pivot = id => body.pivots[id];
  const joint = (name, chain = []) => transformJointChain(pivot(name), body, chain);
  switch (target) {
    case 'head': return transformJointChain(boneCenter(body, name => name === 'Head'), body, ['neck']);
    case 'neck': return pivot('neck');
    case 'chest': return boneCenter(body, name => /^T\d+$|^Rib|^Sternum$/.test(name));
    case 'pelvis': return boneCenter(body, name => /^Pelvis_|^Sacrum$/.test(name));
    case 'shoulderL': return pivot('shoulderL'); case 'shoulderR': return pivot('shoulderR');
    case 'elbowL': return joint('elbowL', ['shoulderL']); case 'elbowR': return joint('elbowR', ['shoulderR']);
    case 'handL': return transformJointChain(boneCenter(body, name => name.startsWith('Hand') && name.endsWith('_L')), body, ['shoulderL', 'elbowL', 'wristL']);
    case 'handR': return transformJointChain(boneCenter(body, name => name.startsWith('Hand') && name.endsWith('_R')), body, ['shoulderR', 'elbowR', 'wristR']);
    case 'hipL': return pivot('hipL'); case 'hipR': return pivot('hipR');
    case 'kneeL': return joint('kneeL', ['hipL']); case 'kneeR': return joint('kneeR', ['hipR']);
    case 'footL': return transformJointChain(boneCenter(body, name => /^(Foot|Tal)_.*_L$/.test(name)), body, ['hipL', 'kneeL', 'ankleL']);
    case 'footR': return transformJointChain(boneCenter(body, name => /^(Foot|Tal)_.*_R$/.test(name)), body, ['hipR', 'kneeR', 'ankleR']);
    default: return [0, 0, 0];
  }
}
function checkReach() {
  const source = selectedBody();
  const target = bodies.find(body => String(body.id) === $('reachTargetBody').value);
  if (!source || !target) { $('reachResult').textContent = 'Spawn a body first.'; return; }
  const limb = $('reachLimb').value;
  const point = transformLocalToWorld(source, targetLocalPoint(source, limb));
  const targetPoint = transformLocalToWorld(target, targetLocalPoint(target, $('reachTarget').value));
  const distance = Math.hypot(point[0] - targetPoint[0], point[1] - targetPoint[1], point[2] - targetPoint[2]);
  const tolerance = Number($('reachTolerance').value);
  const result = $('reachResult');
  result.className = `status ${distance <= tolerance ? 'good' : ''}`;
  result.innerHTML = `<strong>${distance <= tolerance ? 'Within contact tolerance' : 'Not within contact tolerance'}</strong><br>Joint-center distance: ${distance.toFixed(3)} m · tolerance: ${tolerance.toFixed(2)} m`;
}

function serializeOBJ(meshes, body) {
  const output = ['# Fitted Blue skeleton; units m; axes X right, Y up, Z forward.', 'mtllib standard_blue.mtl', 'usemtl standard_blue'];
  let index = 1;
  for (const obj of meshes) {
    const chain = boneChain(obj.name);
    output.push('', `o ${obj.name}`, `g ${obj.name}`);
    for (const v of obj.vertices) { const p = transformJointChain(v, body, chain); output.push(`v ${p[0].toFixed(10)} ${p[1].toFixed(10)} ${p[2].toFixed(10)}`); }
    for (const face of obj.faces) output.push(`f ${face.map(i => i + index).join(' ')}`);
    index += obj.vertices.length;
  }
  return output.join('\n') + '\n';
}
function saveText(name, content, type) {
  const url = URL.createObjectURL(new Blob([content], { type }));
  const anchor = document.createElement('a'); anchor.href = url; anchor.download = name; anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function downloadObj() {
  const body = selectedBody(); if (!body) return;
  saveText(`standard_blue_${body.sex}_${body.height.toFixed(2)}m_pose.obj`, serializeOBJ(body.meshes, body), 'text/plain');
}
function downloadJson() {
  const body = selectedBody(); if (!body) return;
  const jointPoints = Object.fromEntries(Object.entries(body.pivots).map(([key, point]) => [key, point]));
  const data = {
    name: `standard_blue_${body.sex}_${body.height.toFixed(2)}m`, sex: body.sex, license: 'CC BY 4.0', units: 'm',
    axes: { x: 'right', y: 'up', z: 'forward' }, profile_height_m: body.height,
    mesh_objects: body.meshes.length, mesh_vertices: body.sourceMesh.vertexCount,
    mesh_bounds_m: body.meshBounds, target_measurements_m: { names: TARGET_NAMES, requested: body.fit.requestedArray, fitted: body.fit.fittedArray },
    vertical_map_source_y_m: body.fit.sourceY, vertical_map_target_y_m: body.fit.targetY,
    transform_parameters: { height_scale: body.fit.heightScale, head_scale: body.fit.headScale, hip_scale: body.fit.hipScale, shoulder_scale: body.fit.shoulderScale, arm_scale: body.fit.armScale },
    joint_pivots_m: jointPoints, joint_angles_degrees: body.joints,
    world_transform: { x_m: body.worldX, y_m: body.worldY, z_m: body.worldZ, yaw_degrees: body.yaw, tilt_degrees: body.tilt }
  };
  saveText(`${data.name}.json`, JSON.stringify(data, null, 2) + '\n', 'application/json');
}

function setWorld(field, prop, lo, hi) {
  const body = selectedBody(); if (!body) return;
  body[prop] = clamp(Number($(field).value) || 0, lo, hi);
  updateGroundHeight(body);
  renderBodyList(); updateSceneLabel(); draw();
}
function bindUI() {
  $('spawnBody').addEventListener('click', () => addBody($('spawnSex').value, Number($('spawnHeight').value)));
  $('removeBody').addEventListener('click', () => { if (selectedBody()) removeBody(selectedId); });
  $('arrangeBodies').addEventListener('click', arrangeBodies);
  $('bodyHeight').addEventListener('change', () => changeHeight(Number($('bodyHeight').value)));
  $('bodyHeightRange').addEventListener('input', () => { $('bodyHeight').value = Number($('bodyHeightRange').value).toFixed(2); changeHeight(Number($('bodyHeightRange').value)); });
  $('bodySex').addEventListener('change', () => changeBodySex(selectedBody(), $('bodySex').value));
  $('worldX').addEventListener('change', () => setWorld('worldX', 'worldX', -10, 10));
  $('worldZ').addEventListener('change', () => setWorld('worldZ', 'worldZ', -10, 10));
  $('worldYaw').addEventListener('change', () => setWorld('worldYaw', 'yaw', -180, 180));
  $('worldTilt').addEventListener('change', () => setWorld('worldTilt', 'tilt', -90, 90));
  $('jointSelect').addEventListener('change', syncJointSliders);
  for (const axis of ['X', 'Y', 'Z']) $(`joint${axis}`).addEventListener('input', updateJointFromUI);
  $('resetJoint').addEventListener('click', () => { const body = selectedBody(); if (!body) return; body.joints[$('jointSelect').value] = [0, 0, 0]; syncJointSliders(); });
  $('resetPose').addEventListener('click', () => { const body = selectedBody(); if (!body) return; resetPose(body); uploadBody(body); syncJointSliders(); });
  $('applyPose').addEventListener('click', () => { const body = selectedBody(); if (body) applyPose(body, $('posePreset').value); });
  $('showJoints').addEventListener('change', draw);
  $('reachTolerance').addEventListener('input', () => { $('toleranceValue').textContent = `${Number($('reachTolerance').value).toFixed(2)} m`; });
  $('checkReach').addEventListener('click', checkReach);
  $('downloadObj').addEventListener('click', downloadObj);
  $('downloadJson').addEventListener('click', downloadJson);
  $('fitView').addEventListener('click', fitAllBodies);
  $('resetView').addEventListener('click', resetCamera);
  $('zoomIn').addEventListener('click', () => zoomBy(.84));
  $('zoomOut').addEventListener('click', () => zoomBy(1.19));
  $('zoomRange').addEventListener('input', () => { camera.distance = Number($('zoomRange').value); updateZoomText(); draw(); });
  canvas.addEventListener('contextmenu', event => event.preventDefault());
  canvas.addEventListener('pointerdown', event => {
    if (event.button !== 0 && event.button !== 1 && event.button !== 2) return;
    event.preventDefault();
    drag = { x: event.clientX, y: event.clientY, startX: event.clientX, startY: event.clientY, button: event.button, mode: event.shiftKey || event.button === 1 ? 'pan' : 'orbit' };
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener('pointermove', event => {
    if (!drag) return;
    const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
    drag.x = event.clientX; drag.y = event.clientY;
    if (drag.mode === 'orbit') {
      camera.yaw -= dx * .008;
      camera.pitch = clamp(camera.pitch + dy * .006, -1.35, 1.35);
    } else {
      const scale = camera.distance / Math.max(200, canvas.clientHeight);
      camera.target[0] -= dx * scale * Math.cos(camera.yaw);
      camera.target[2] += dx * scale * Math.sin(camera.yaw);
      camera.target[1] += dy * scale;
    }
    draw();
  });
  canvas.addEventListener('pointerup', event => {
    if (drag && drag.button === 0 && Math.hypot(event.clientX - drag.startX, event.clientY - drag.startY) < 4) selectMarkerAt(event.clientX, event.clientY);
    drag = null;
  });
  canvas.addEventListener('pointercancel', () => { drag = null; });
  canvas.addEventListener('wheel', event => {
    event.preventDefault();
    camera.distance = clamp(camera.distance * Math.exp(event.deltaY * .001), 1.5, 12);
    $('zoomRange').value = camera.distance; updateZoomText(); draw();
  }, { passive: false });
  window.addEventListener('resize', draw);
}
async function changeHeight(value) {
  const body = selectedBody(); if (!body) return;
  const height = clamp(Number(value) || .5, .5, 2.5);
  body.height = height;
  $('bodyHeight').value = height.toFixed(2); $('bodyHeightRange').value = height;
  $('activeBodyLabel').textContent = `${body.sex} · ${height.toFixed(2)} m`;
  refreshBodyGeometry(body);
  renderBodyList(); updateTargets(); updateFitSummary(); draw();
}

async function start() {
  bindUI();
  $('headerStatus').textContent = 'Loading original OBJ skeletons…';
  try {
    initGL();
    await Promise.all([loadSource('male'), loadSource('female')]);
    $('headerStatus').textContent = 'Both source skeletons ready';
    await addBody('male', 1.722, false);
    camera.start = [camera.yaw, camera.pitch, camera.distance, camera.target[0], camera.target[1], camera.target[2]];
    $('zoomRange').value = camera.distance; updateZoomText(); draw();
    $('bodyHeightRange').value = 1.722;
    $('sceneLabel').textContent = 'Real bone meshes loaded';
    $('sceneSub').textContent = 'Right-drag or left-drag to orbit; scroll or use zoom controls';
  } catch (error) {
    $('headerStatus').textContent = 'Viewer initialization failed';
    $('fitStatus').className = 'status error'; $('fitStatus').textContent = error.message;
    console.error(error);
  }
}
start();
