// Lazy access to the generated MakeHuman CC0 male and female packs.
import { humanData as maleData } from './human-male-data.js';
import { humanData as femaleData } from './human-female-data.js';

export function humanData(male = true) {
  return male ? maleData() : femaleData();
}
