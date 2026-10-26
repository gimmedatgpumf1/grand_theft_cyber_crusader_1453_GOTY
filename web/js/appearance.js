export const PALETTES = {
  skin: ['#f0c8a8', '#d9a17d', '#b97c58', '#8a573e', '#5c3b2e', '#e5b894'],
  hairColor: ['#171a1c', '#3b281d', '#75604a', '#b5a075', '#5b6268', '#69362c'],
  shirt: ['#20262b', '#b3453c', '#56765f', '#c4b58d', '#334c6b', '#6b4c70', '#c7ccd1'],
  pants: ['#1c2328', '#2d3338', '#4a453e', '#23384a'],
};

export const DEFAULT_APPEARANCE = Object.freeze({
  male: true, skin: 1, hair: 2, hairColor: 0, shirt: 0, pants: 0,
  beard: 0, coat: false, sunglasses: false,
});

export const PRESETS = {
  jc: { male: true, skin: 1, hair: 2, hairColor: 0, shirt: 0, pants: 0,
    beard: 2, coat: true, sunglasses: true },
  agent: { male: true, skin: 3, hair: 1, hairColor: 0, shirt: 4, pants: 1,
    beard: 1, coat: false, sunglasses: false },
  classic: { male: false, skin: 0, hair: 3, hairColor: 2, shirt: 2, pants: 3,
    beard: 0, coat: false, sunglasses: false },
};

const LIMITS = { skin: 5, hair: 4, hairColor: 5, shirt: 6, pants: 3, beard: 2 };
const index = (value, fallback, maximum) => Number.isFinite(Number(value)) ?
  Math.max(0, Math.min(maximum, Math.round(Number(value)))) : fallback;

export function normalizeAppearance(data = {}) {
  const result = { ...DEFAULT_APPEARANCE };
  for (const [key, maximum] of Object.entries(LIMITS)) {
    result[key] = index(data[key], result[key], maximum);
  }
  for (const key of ['male', 'coat', 'sunglasses']) {
    if (typeof data[key] === 'boolean') result[key] = data[key];
  }
  return result;
}

export function appearanceKey(data) {
  const value = normalizeAppearance(data);
  return Object.keys(DEFAULT_APPEARANCE).map(key => value[key]).join('|');
}

export function savedAppearance() {
  try {
    const saved = JSON.parse(localStorage.getItem('gtc-appearance') || 'null');
    return saved ? normalizeAppearance(saved) : null;
  } catch { return null; }
}

export function saveAppearance(appearance) {
  try { localStorage.setItem('gtc-appearance', JSON.stringify(normalizeAppearance(appearance))); }
  catch { /* Private browsing can deny storage. */ }
}
