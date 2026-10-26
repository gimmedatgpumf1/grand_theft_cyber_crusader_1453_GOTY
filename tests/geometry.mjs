import assert from 'node:assert/strict';
import { normalizeAppearance } from '../web/js/appearance.js';
import { MORPH_DEFS } from '../web/js/morphs.js';
import { buildHumanGeometry } from '../web/js/human-geometry.js';

function headSize(key, value) {
  const appearance = normalizeAppearance();
  const index = MORPH_DEFS.findIndex(definition => definition.key === key);
  assert.notEqual(index, -1, `Missing morph ${key}`);
  appearance.morphs[index] = value;
  const geometry = buildHumanGeometry(appearance);
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
console.log('Signed morph direction tests passed. Decrease no longer means increase.');
