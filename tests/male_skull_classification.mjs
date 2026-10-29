// Z-Anatomy's unpaired names differ from the usual "... bone" convention.
import assert from 'node:assert/strict';
import { classify } from '../tools/skeleton-source/zanatomy-classify.mjs';
for (const name of ['Maxilla.l', 'Maxilla.r', 'Nasal bone.l', 'Nasal bone.r', 'Vomer']) {
  assert.equal(classify(name).part, 'skull', name);
}
for (const name of ['Frontal bone', 'Parietal bone.l', 'Occipital bone', 'Temporal bone.r']) {
  assert.equal(classify(name).part, 'skull', name);
}
for (const name of ['Nasal septal cartilage', 'Lateral process of nasal septal cartilage.l',
  'Major alar cartilage.l', 'Major alar cartilage.r', 'Thyroid cartilage', 'Cricoid cartilage',
  'Arytenoid cartilage.l', 'Arytenoid cartilage.r', 'Corniculate cartilage.l', 'Corniculate cartilage.r']) {
  assert.equal(classify(name).part, null, name);
}
assert.equal(classify('Hyoid bone').part, 'spine_cervical');
assert.equal(classify('Femur.l').part, 'femur_l');
console.log('male skull source classification: PASS');
