// Lazy access to generated MHCLO-fitted community hair packs.
import { hairData as maleData } from './human-hair-male-data.js';
import { hairData as femaleData } from './human-hair-female-data.js';

export function hairData(male = true) {
  return male ? maleData() : femaleData();
}
