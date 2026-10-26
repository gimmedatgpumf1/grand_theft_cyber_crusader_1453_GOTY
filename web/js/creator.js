import { PRESETS, normalizeAppearance } from './appearance.js';

const byId = id => document.getElementById(id);
const FIELDS = ['male', 'skin', 'hair', 'hairColor', 'shirt', 'pants', 'beard', 'coat', 'sunglasses'];

export class CharacterCreator {
  constructor(network, callbacks) {
    this.network = network;
    this.callbacks = callbacks;
    this.values = normalizeAppearance();
    document.querySelectorAll('[data-preset]').forEach(button => {
      button.addEventListener('click', () => this.apply(PRESETS[button.dataset.preset]));
    });
    document.querySelectorAll('[data-appearance]').forEach(field => {
      field.addEventListener('change', () => this.apply(this.read()));
    });
    byId('apply-appearance').addEventListener('click', () => this.apply(this.read(), true));
  }

  open(local) {
    this.values = normalizeAppearance(local || this.values);
    for (const key of FIELDS) {
      const field = byId(`creator-${key}`);
      if (field.type === 'checkbox') field.checked = this.values[key];
      else field.value = String(this.values[key]);
    }
    byId('creator').classList.remove('hidden');
  }

  close() {
    byId('creator').classList.add('hidden');
  }

  read() {
    const result = {};
    for (const key of FIELDS) {
      const field = byId(`creator-${key}`);
      if (field.type === 'checkbox') result[key] = field.checked;
      else if (key === 'male') result[key] = field.value === 'male';
      else result[key] = Number(field.value);
    }
    return normalizeAppearance(result);
  }

  apply(values, announce = false) {
    this.values = normalizeAppearance(values);
    for (const key of FIELDS) {
      const field = byId(`creator-${key}`);
      if (field.type === 'checkbox') field.checked = this.values[key];
      else field.value = String(this.values[key]);
    }
    this.network.customize(this.values);
    if (announce) this.callbacks.onApplied?.();
  }
}
