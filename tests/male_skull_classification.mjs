// Z-Anatomy's unpaired names differ from the usual "... bone" convention.
import assert from 'node:assert/strict';
import { classify } from '../tools/skeleton-source/zanatomy-classify.mjs';
for (const name of ['Maxilla.l', 'Maxilla.r', 'Nasal bone.l', 'Nasal bone.r', 'Vomer']) {
  assert.equal(classify(name).part, 'skull', name);
}
for (const name of ['Frontal bone', 'Parietal bone.l', 'Occipital bone', 'Temporal bone.r']) {
  assert.equal(classify(name).part, 'skull', name);
}
assert.equal(classify('Femur.l').part, 'femur_l');
console.log('male skull source classification: PASS');
