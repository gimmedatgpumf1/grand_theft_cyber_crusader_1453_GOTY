import { DEFAULT_APPEARANCE, PRESETS, normalizeAppearance } from './appearance.js';
import { MORPH_DEFS } from './morphs.js';

const byId = id => document.getElementById(id);
const FIELDS = ['male', 'skin', 'hair', 'hairColor', 'shirt', 'pants', 'beard', 'coat', 'sunglasses',
  'eyeColor', 'makeup', 'lipColor', 'suit', 'tie', 'dress', 'beret', 'earrings', 'necklace', 'fur'];

export class CharacterCreator {
  constructor(network, callbacks) {
    this.network = network;
    this.callbacks = callbacks;
    this.values = normalizeAppearance();
    this.pending = null;
    this.buildMorphControls();
    document.querySelectorAll('[data-preset]').forEach(button => {
      button.addEventListener('click', () => {
        this.apply(PRESETS[button.dataset.preset]);
        this.markPreset(button.dataset.preset);
        this.callbacks.onPreset?.(PRESETS[button.dataset.preset]);
      });
    });
    document.querySelectorAll('[data-appearance]').forEach(field => {
      field.addEventListener('change', () => {
        this.markPreset(null);
        this.apply(this.read());
      });
    });
    document.querySelectorAll('[data-creator-tab]').forEach(button => {
      button.addEventListener('click', () => this.tab(button.dataset.creatorTab));
    });
    document.querySelectorAll('[data-creator-focus]').forEach(button => {
      button.addEventListener('click', () => {
        document.querySelectorAll('[data-creator-focus]').forEach(item => item.classList.toggle('active', item === button));
        this.callbacks.onFocus?.(button.dataset.creatorFocus);
      });
    });
    document.querySelectorAll('[data-creator-zoom]').forEach(button => {
      button.addEventListener('click', () => this.callbacks.onZoom?.(Number(button.dataset.creatorZoom)));
    });
    byId('randomize-appearance').addEventListener('click', () => this.randomize());
    byId('reset-appearance').addEventListener('click', () => {
      this.markPreset(null);
      this.apply(DEFAULT_APPEARANCE);
    });
    byId('apply-appearance').addEventListener('click', () => this.apply(this.read(), true));
  }

  buildMorphControls() {
    for (const definition of MORPH_DEFS) {
      const wrapper = document.createElement('label');
      wrapper.className = 'morph-control';
      wrapper.htmlFor = `morph-${definition.key}`;
      const heading = document.createElement('span');
      const title = document.createElement('b');
      title.textContent = definition.label;
      const output = document.createElement('output');
      output.id = `morph-${definition.key}-value`;
      output.textContent = '0';
      heading.append(title, output);
      const input = document.createElement('input');
      input.type = 'range';
      input.id = `morph-${definition.key}`;
      input.min = definition.min;
      input.max = definition.max;
      input.step = 1;
      input.value = 0;
      input.dataset.morph = definition.key;
      input.addEventListener('input', () => {
        output.value = input.value;
        output.textContent = input.value;
        this.markPreset(null);
        this.schedule();
      });
      input.addEventListener('change', () => {
        clearTimeout(this.pending);
        this.apply(this.read());
      });
      wrapper.append(heading, input);
      byId(`morph-${definition.tab}`).append(wrapper);
    }
  }

  markPreset(key) {
    document.querySelectorAll('[data-preset]').forEach(button => {
      button.classList.toggle('active', button.dataset.preset === key);
    });
  }

  tab(key) {
    document.querySelectorAll('[data-creator-tab]').forEach(button => {
      button.classList.toggle('active', button.dataset.creatorTab === key);
    });
    document.querySelectorAll('[data-creator-page]').forEach(page => {
      page.classList.toggle('hidden', page.dataset.creatorPage !== key);
    });
  }

  schedule() {
    clearTimeout(this.pending);
    this.pending = setTimeout(() => this.apply(this.read(), false, false), 55);
  }

  open(local) {
    clearTimeout(this.pending);
    this.values = normalizeAppearance(local || this.values);
    this.syncFields();
    byId('creator').classList.remove('hidden');
  }

  close() {
    clearTimeout(this.pending);
    byId('creator').classList.add('hidden');
  }

  syncFields() {
    for (const key of FIELDS) {
      const field = byId(`creator-${key}`);
      if (field.type === 'checkbox') field.checked = this.values[key];
      else field.value = String(this.values[key]);
    }
    MORPH_DEFS.forEach((definition, index) => {
      const value = this.values.morphs[index];
      byId(`morph-${definition.key}`).value = value;
      byId(`morph-${definition.key}-value`).textContent = value;
    });
  }

  read() {
    const result = { morphs: MORPH_DEFS.map(definition => Number(byId(`morph-${definition.key}`).value)) };
    for (const key of FIELDS) {
      const field = byId(`creator-${key}`);
      if (field.type === 'checkbox') result[key] = field.checked;
      else if (key === 'male') result[key] = field.value === 'male';
      else result[key] = Number(field.value);
    }
    return normalizeAppearance(result);
  }

  apply(values, announce = false, syncControls = true) {
    clearTimeout(this.pending);
    this.values = normalizeAppearance(values);
    if (syncControls) this.syncFields();
    this.network.customize(this.values);
    if (announce) this.callbacks.onApplied?.();
  }

  randomize() {
    const values = normalizeAppearance();
    const random = maximum => Math.floor(Math.random() * (maximum + 1));
    const male = Math.random() < 0.5;
    Object.assign(values, {
      male, skin: random(5), hair: random(15), hairColor: random(8), shirt: random(6), pants: random(3),
      eyeColor: random(5), makeup: male ? 0 : random(4), lipColor: random(4), beard: male ? random(2) : 0,
      coat: Math.random() < 0.15, suit: Math.random() < 0.30, tie: random(5),
      earrings: Math.random() < 0.18, necklace: Math.random() < 0.15,
    });
    values.morphs = MORPH_DEFS.map((definition, index) => {
      if (index === 0) return random(65);
      if (index === 1 || index === 2) return Math.random() < 0.25 ? random(100) : 0;
      return Math.round((Math.random() * 2 - 1) * 45);
    });
    this.markPreset(null);
    this.apply(values);
  }
}
