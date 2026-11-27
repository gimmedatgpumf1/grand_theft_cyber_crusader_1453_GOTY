'use strict';

// Proportion formulas and source-mesh fitting math, isolated from the viewer UI.
window.SkeletonFitGeometry = (() => {
  const TARGET_NAMES = ['head', 'neck', 'torso', 'arm', 'leg', 'hip', 'shoulder'];
  const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
  const lerp = (a, b, t) => a + (b - a) * t;

function formulaTargets(sex, h) {
  if (sex === 'male') return {
    head: .0561 * h + .12179, neck: .0193 * h + .074136,
    torso: .3536 * h + .044647, arm: .4515 * h + .014121,
    leg: .5553 * h - .065708, hip: .076737 * Math.exp(.75 * h),
    shoulder: .10719 * Math.exp(.77 * h)
  };
  return {
    head: .0568 * h + .11832, neck: .0213 * h + .071832,
    torso: .3446 * h + .045758, arm: .4734 * h - .013187,
    leg: .568 * h - .069588, hip: .067179 * Math.exp(.89 * h),
    shoulder: .1087 * Math.exp(.75 * h)
  };
}
function boneCategory(name) {
  if (name === 'Head' || name === 'Jaw' || name === 'C1') return 'head';
  if (/^C[2-7]$/.test(name)) return 'neck';
  if (/^T\d+$|^L\d+$|^Rib|^Sternum$/.test(name)) return 'torso';
  if (/^Pelvis_|^Sacrum$/.test(name)) return 'hip';
  if (/^Scapula_|^Clavicula_/.test(name)) return 'shoulder';
  if (/^Humerus_|^Radius_|^Hand/.test(name)) return 'arm';
  return 'leg';
}
function parseOBJ(text) {
  const globalVertices = [];
  const objects = [];
  let current = null;
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line[0] === '#') continue;
    const part = line.split(/\s+/);
    if (part[0] === 'o') {
      current = { name: part.slice(1).join(' ') || `bone_${objects.length}`, category: '', start: globalVertices.length, vertices: [], faces: [] };
      current.category = boneCategory(current.name);
      objects.push(current);
    } else if (part[0] === 'v') {
      if (!current) throw new Error('OBJ vertex appeared before a bone object.');
      const p = part.slice(1, 4).map(Number);
      globalVertices.push(p);
      current.vertices.push(p);
    } else if (part[0] === 'f' && current) {
      const ids = part.slice(1).map(token => {
        const n = Number(token.split('/')[0]);
        return n < 0 ? globalVertices.length + n : n - 1;
      });
      for (let i = 1; i + 1 < ids.length; i++) {
        const tri = [ids[0], ids[i], ids[i + 1]].map(index => index - current.start);
        if (tri.every(index => index >= 0 && index < current.vertices.length)) current.faces.push(tri);
      }
    }
  }
  if (!objects.length || !globalVertices.length) throw new Error('The OBJ did not contain any bone geometry.');
  return { objects, vertexCount: globalVertices.length };
}
function interpolate(xs, ys, x) {
  let i = 0;
  while (i + 2 < xs.length && x > xs[i + 1]) i++;
  const span = xs[i + 1] - xs[i];
  return lerp(ys[i], ys[i + 1], span ? (x - xs[i]) / span : 0);
}
const inverseInterpolate = (xs, ys, y) => interpolate(xs, ys, y);

function fitVertical(meta, height, targets) {
  const source = meta.source_landmarks_m;
  const sourceStops = meta.vertical_map_source_y_m;
  const hipSourceY = inverseInterpolate(meta.vertical_map_target_y_m, sourceStops, meta.target_measurements_m.fitted[4]);
  const heightScale = height / (source.head_top_y - source.floor_y);
  const epsilon = 1e-6 * height;
  const calculate = q => {
    const headScale = q.head / (source.head_top_y - source.jaw_bottom_y);
    const hipScale = q.hip / source.hip_width_x;
    const contact = height - (source.head_top_y - source.neck_contact_y) * headScale;
    const join = height - (source.head_top_y - source.cranial_join_y) * headScale;
    const c7 = contact - q.neck;
    const ring = (source.pelvis_top_y - source.pelvis_bottom_y) * hipScale;
    const hipOffset = (hipSourceY - source.pelvis_bottom_y) * hipScale;
    const valid = headScale > 0 && hipScale > 0 && c7 > ring + 2 * epsilon && c7 < join - epsilon && join + epsilon < contact && contact < height - epsilon;
    if (!valid) return null;
    const pelvisBottom = clamp(((c7 - q.torso) + (q.leg - hipOffset)) * .5, epsilon, c7 - ring - epsilon);
    return {
      headScale, hipScale, contact, join, c7, ring, hipOffset,
      sourceY: sourceStops.slice(),
      targetY: [0, pelvisBottom, pelvisBottom + ring, c7, join, contact, height],
      hipSourceY, heightScale
    };
  };
  let fittedTargets = { ...targets };
  let result = calculate(fittedTargets);
  let blended = false;
  if (!result) {
    const baseline = {
      ...targets,
      head: (source.head_top_y - source.jaw_bottom_y) * heightScale,
      neck: (source.neck_contact_y - source.C7_y) * heightScale,
      hip: source.hip_width_x * heightScale
    };
    const mix = t => ({
      ...targets,
      head: lerp(baseline.head, targets.head, t),
      neck: lerp(baseline.neck, targets.neck, t),
      hip: lerp(baseline.hip, targets.hip, t)
    });
    let low = 0;
    let high = 1;
    result = calculate(mix(0));
    if (!result) throw new Error('This height cannot be fitted safely to the source skeleton.');
    for (let i = 0; i < 48; i++) {
      const mid = (low + high) * .5;
      if (calculate(mix(mid))) low = mid;
      else high = mid;
    }
    fittedTargets = mix(low * (1 - 1e-8));
    result = calculate(fittedTargets);
    blended = true;
  }
  const mapY = y => interpolate(result.sourceY, result.targetY, y);
  const armPoints = meta.arm_points_m;
  const shoulderY = (armPoints.left_shoulder[1] + armPoints.right_shoulder[1]) * .5;
  const delta = Math.max(1e-9, (source.head_top_y - source.floor_y) * 1e-7);
  const shoulderYTarget = mapY(shoulderY);
  const shoulderVertical = (mapY(shoulderY + delta) - mapY(shoulderY - delta)) / (2 * delta);
  const shoulderScale = fittedTargets.shoulder / source.shoulder_width_x;
  const fit = {
    height, sourceY: result.sourceY, targetY: result.targetY, heightScale,
    headScale: result.headScale, hipScale: result.hipScale,
    shoulderScale, upperArmScale: shoulderScale, armScale: 1,
    shoulderYTarget, shoulderVertical, c7: result.c7, join: result.join,
    hipSourceY, requested: targets, fitted: { ...fittedTargets }, blended,
    mapY, source, points: armPoints, centerX: source.center_x
  };
  const lengthAt = scale => {
    fit.armScale = scale;
    let mean = 0;
    for (const [side, label] of [[1, 'left'], [-1, 'right']]) {
      const a = mapPoint(armPoints[`${label}_start`], 'arm', side, true, fit);
      const b = mapPoint(armPoints[`${label}_end`], 'arm', side, false, fit);
      mean += Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) * .5;
    }
    return mean;
  };
  let low = 1e-12;
  let high = 2;
  while (lengthAt(high) < fittedTargets.arm && high < 1e100) high *= 2;
  if (lengthAt(low) <= fittedTargets.arm && lengthAt(high) >= fittedTargets.arm) {
    for (let i = 0; i < 50; i++) {
      const mid = (low + high) * .5;
      if (lengthAt(mid) < fittedTargets.arm) low = mid;
      else high = mid;
    }
    fit.armScale = (low + high) * .5;
  } else {
    fit.armScale = high;
    fit.fitted.arm = lengthAt(high);
  }
  fit.fitted.torso = fit.targetY[3] - fit.targetY[1];
  fit.fitted.leg = mapY(hipSourceY);
  fit.fittedArray = TARGET_NAMES.map(name => fit.fitted[name]);
  fit.requestedArray = TARGET_NAMES.map(name => targets[name]);
  return fit;
}
function mapPoint(p, mode, side, humerus, fit) {
  const s = fit.source;
  const points = fit.points;
  if (mode === 'head') {
    return [(p[0] - fit.centerX) * fit.headScale, fit.height - (s.head_top_y - p[1]) * fit.headScale,
      s.neck_contact_z * fit.heightScale + (p[2] - s.neck_contact_z) * fit.headScale];
  }
  if (mode === 'girdle' || mode === 'arm') {
    const shoulderY = (points.left_shoulder[1] + points.right_shoulder[1]) * .5;
    let x = (p[0] - fit.centerX) * fit.shoulderScale;
    if (mode === 'arm') {
      const label = side > 0 ? 'left' : 'right';
      const shoulder = points[`${label}_shoulder`];
      const elbow = points[`${label}_elbow`];
      const start = points[`${label}_start`];
      const boundary = shoulder[0] + side * Math.abs(start[0] - shoulder[0]);
      const upper = u => (boundary - fit.centerX) * fit.shoulderScale + (u - boundary) * fit.upperArmScale;
      if (humerus && side * (p[0] - boundary) > 0) x = upper(p[0]);
      else if (!humerus) x = upper(elbow[0]) + (p[0] - elbow[0]) * fit.armScale;
    }
    return [x, fit.shoulderYTarget + (p[1] - shoulderY) * fit.shoulderVertical,
      s.neck_contact_z * fit.heightScale + (p[2] - s.neck_contact_z) * fit.heightScale];
  }
  const y = p[1];
  let scaleX = fit.heightScale;
  if (y >= s.pelvis_bottom_y && y <= s.pelvis_top_y) scaleX = fit.hipScale;
  else if (y > s.pelvis_top_y && y < s.scapula_bottom_y) scaleX = lerp(fit.hipScale, fit.shoulderScale, (y - s.pelvis_top_y) / (s.scapula_bottom_y - s.pelvis_top_y));
  else if (y >= s.scapula_bottom_y && y <= s.scapula_top_y) scaleX = fit.shoulderScale;
  else if (y > s.scapula_top_y && y < s.cranial_join_y) scaleX = lerp(fit.shoulderScale, fit.headScale, (y - s.scapula_top_y) / (s.cranial_join_y - s.scapula_top_y));
  else if (y >= s.cranial_join_y || y < s.pelvis_bottom_y) scaleX = y >= s.cranial_join_y ? fit.headScale : fit.hipScale;
  let scaleZ = fit.heightScale;
  if (y >= s.pelvis_bottom_y && y <= s.pelvis_top_y || y < s.pelvis_bottom_y) scaleZ = fit.hipScale;
  else if (y > s.pelvis_top_y && y < s.scapula_bottom_y) scaleZ = lerp(fit.hipScale, fit.heightScale, (y - s.pelvis_top_y) / (s.scapula_bottom_y - s.pelvis_top_y));
  else if (y >= s.cranial_join_y) scaleZ = fit.headScale;
  else if (y > s.scapula_top_y) scaleZ = lerp(fit.heightScale, fit.headScale, (y - s.scapula_top_y) / (s.cranial_join_y - s.scapula_top_y));
  return [(p[0] - fit.centerX) * scaleX, fit.mapY(y), s.neck_contact_z * fit.heightScale + (p[2] - s.neck_contact_z) * scaleZ];
}
function makeBaseFit(meta) {
  const p = meta.transform_parameters;
  return {
    height: meta.default_height_m, sourceY: meta.vertical_map_source_y_m, targetY: meta.vertical_map_target_y_m,
    heightScale: p.height_scale, headScale: p.head_scale, hipScale: p.hip_scale,
    shoulderScale: p.shoulder_scale, armScale: p.arm_scale, upperArmScale: p.upper_arm_scale,
    shoulderYTarget: p.shoulder_y_target_m, shoulderVertical: p.shoulder_vertical_scale,
    source: meta.source_landmarks_m, points: meta.arm_points_m, centerX: meta.source_landmarks_m.center_x
  };
}

function scaleXAt(y, fit) {
  const s = fit.source;
  if (y >= s.pelvis_bottom_y && y <= s.pelvis_top_y || y < s.pelvis_bottom_y) return fit.hipScale;
  if (y > s.pelvis_top_y && y < s.scapula_bottom_y) return lerp(fit.hipScale, fit.shoulderScale, (y - s.pelvis_top_y) / (s.scapula_bottom_y - s.pelvis_top_y));
  if (y >= s.scapula_bottom_y && y <= s.scapula_top_y) return fit.shoulderScale;
  if (y > s.scapula_top_y && y < s.cranial_join_y) return lerp(fit.shoulderScale, fit.headScale, (y - s.scapula_top_y) / (s.cranial_join_y - s.scapula_top_y));
  if (y >= s.cranial_join_y) return fit.headScale;
  return fit.heightScale;
}
function scaleZAt(y, fit) {
  const s = fit.source;
  if (y >= s.pelvis_bottom_y && y <= s.pelvis_top_y || y < s.pelvis_bottom_y) return fit.hipScale;
  if (y > s.pelvis_top_y && y < s.scapula_bottom_y) return lerp(fit.hipScale, fit.heightScale, (y - s.pelvis_top_y) / (s.scapula_bottom_y - s.pelvis_top_y));
  if (y >= s.cranial_join_y) return fit.headScale;
  if (y > s.scapula_top_y) return lerp(fit.heightScale, fit.headScale, (y - s.scapula_top_y) / (s.cranial_join_y - s.scapula_top_y));
  return fit.heightScale;
}
function inverseBaseVertex(name, p, base, fit) {
  const source = base.source;
  const points = base.points;
  const side = name.endsWith('_L') ? 1 : -1;
  if (['Head', 'Jaw', 'C1'].includes(name)) {
    const x = base.centerX + p[0] / base.headScale;
    const y = source.head_top_y - (base.height - p[1]) / base.headScale;
    const z = source.neck_contact_z + (p[2] - source.neck_contact_z * base.heightScale) / base.headScale;
    return mapPoint([x, y, z], 'head', 0, false, fit);
  }
  if (name.startsWith('Scapula_') || name.startsWith('Clavicula_')) {
    const shoulderY = (points.left_shoulder[1] + points.right_shoulder[1]) * .5;
    const x = base.centerX + p[0] / base.shoulderScale;
    const y = shoulderY + (p[1] - base.shoulderYTarget) / base.shoulderVertical;
    const z = source.neck_contact_z + (p[2] - source.neck_contact_z * base.heightScale) / base.heightScale;
    return mapPoint([x, y, z], 'girdle', 0, false, fit);
  }
  if (name.startsWith('Humerus_') || name.startsWith('Radius_') || name.startsWith('Hand')) {
    const label = side > 0 ? 'left' : 'right';
    const shoulder = points[`${label}_shoulder`];
    const elbow = points[`${label}_elbow`];
    const start = points[`${label}_start`];
    const boundary = shoulder[0] + side * Math.abs(start[0] - shoulder[0]);
    const upperOld = u => (boundary - base.centerX) * base.shoulderScale + (u - boundary) * base.upperArmScale;
    let x;
    if (name.startsWith('Humerus_')) {
      const seam = (boundary - base.centerX) * base.shoulderScale;
      x = side * (p[0] - seam) > 0 ? boundary + (p[0] - seam) / base.upperArmScale : base.centerX + p[0] / base.shoulderScale;
    } else x = elbow[0] + (p[0] - upperOld(elbow[0])) / base.armScale;
    const shoulderY = (points.left_shoulder[1] + points.right_shoulder[1]) * .5;
    const y = shoulderY + (p[1] - base.shoulderYTarget) / base.shoulderVertical;
    const z = source.neck_contact_z + (p[2] - source.neck_contact_z * base.heightScale) / base.heightScale;
    return mapPoint([x, y, z], 'arm', side, name.startsWith('Humerus_'), fit);
  }
  const sourceY = inverseInterpolate(base.targetY, base.sourceY, p[1]);
  const x = p[0] * (scaleXAt(sourceY, fit) / scaleXAt(sourceY, base));
  const y = interpolate(base.targetY, fit.targetY, p[1]);
  const z = source.neck_contact_z * fit.heightScale + (p[2] - source.neck_contact_z * base.heightScale) * (scaleZAt(sourceY, fit) / scaleZAt(sourceY, base));
  return [x, y, z];
}
function transformToFit(sourceMesh, base, fit) {
  if (Math.abs(fit.height - base.height) < 1e-12) return sourceMesh.objects.map(o => ({ ...o, vertices: o.vertices.map(p => p.slice()) }));
  return sourceMesh.objects.map(o => ({ ...o, vertices: o.vertices.map(p => inverseBaseVertex(o.name, p, base, fit)) }));
}


  return { TARGET_NAMES, formulaTargets, boneCategory, parseOBJ, fitVertical, mapPoint, makeBaseFit, transformToFit };
})();
