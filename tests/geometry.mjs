import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { normalizeAppearance } from '../web/js/appearance.js';
import { MORPH_DEFS } from '../web/js/morphs.js';
import { buildHumanGeometry } from '../web/js/human-geometry.js';

const sourceMorphs = JSON.parse(readFileSync(new URL('../tools/human-morphs.json', import.meta.url), 'utf8'));
assert.ok(MORPH_DEFS.every(definition => !definition.key.endsWith('Sculpt')) &&
  sourceMorphs.every(definition => ![...definition.positive, ...definition.negative]
    .some(path => path.startsWith('custom/'))),
'Every control must be a reusable anatomical morph, not a person-specific sculpt');
console.log('[test] All face presets use the universal anatomical morph basis.');

function geometryFor(key, value) {
  const appearance = normalizeAppearance();
  const index = MORPH_DEFS.findIndex(definition => definition.key === key);
  assert.notEqual(index, -1, `Missing morph ${key}`);
  appearance.morphs[index] = value;
  return buildHumanGeometry(appearance);
}

function headSize(key, value) {
  const geometry = geometryFor(key, value);
  return {
    width: geometry.headBounds.max.x - geometry.headBounds.min.x,
    height: geometry.headBounds.max.y - geometry.headBounds.min.y,
  };
}

for (const [key, axis] of [['headWidth', 'width'], ['headHeight', 'height']]) {
  const decrease = headSize(key, -100)[axis];
  const neutral = headSize(key, 0)[axis];
  const increase = headSize(key, 100)[axis];
  assert.ok(decrease < neutral, `${key} -100 must decrease ${axis}: ${decrease} >= ${neutral}`);
  assert.ok(increase > neutral, `${key} +100 must increase ${axis}: ${increase} <= ${neutral}`);
  console.log(`[test] ${key}: ${decrease.toFixed(6)} < ${neutral.toFixed(6)} < ${increase.toFixed(6)}`);
}
function hairSignature(value) {
  const positions = geometryFor('headWidth', value).hair.attributes.position.array;
  let signature = 0;
  for (let index = 0; index < positions.length; ++index) signature += positions[index] * (index % 29 + 1);
  return signature;
}
assert.notEqual(hairSignature(-100), hairSignature(100), 'Hair must not double-negate its decrease target');

const eyeDecrease = geometryFor('eyeSpacing', -100).eyes.L[0];
const eyeNeutral = geometryFor('eyeSpacing', 0).eyes.L[0];
const eyeIncrease = geometryFor('eyeSpacing', 100).eyes.L[0];
assert.ok((eyeDecrease < eyeNeutral && eyeIncrease > eyeNeutral) ||
  (eyeDecrease > eyeNeutral && eyeIncrease < eyeNeutral), 'Eye spacing targets must move in opposite directions');
console.log('[test] Hair and eye morphs use signed target deltas exactly once.');
console.log('Signed morph direction tests passed. Decrease no longer means increase.');
