import { MORPH_DEFS } from './morphs.js';

export const PALETTES = {
  skin: ['#f0c8a8', '#d9a17d', '#b97c58', '#8a573e', '#5c3b2e', '#e5b894'],
  hairColor: ['#171a1c', '#3b281d', '#75604a', '#b5a075', '#5b6268', '#69362c', '#e5d5aa', '#d2b477', '#a9adb0'],
  shirt: ['#20262b', '#b3453c', '#56765f', '#c4b58d', '#334c6b', '#6b4c70', '#c7ccd1'],
  pants: ['#1c2328', '#2d3338', '#4a453e', '#23384a'],
  eyes: ['#34271f', '#6a4d2c', '#496b82', '#4c6854', '#777d82', '#171b1d'],
  lips: ['#874b45', '#b32b38', '#d34b55', '#9c5260', '#c96a5b'],
  tie: ['#20262b', '#243b60', '#7d2528', '#375a80', '#454c55', '#b2924a'],
};

export const PORTRAIT_KEYS = Object.freeze([null, 'marilyn', 'ibrahim', 'xi', 'putin', 'trump', 'taylor', 'obama']);

export const DEFAULT_APPEARANCE = Object.freeze({
  male: true, skin: 1, portrait: 0, hair: 2, hairColor: 0, shirt: 0, pants: 0,
  beard: 0, coat: false, sunglasses: false, eyeColor: 2, makeup: 0, lipColor: 0,
  suit: false, tie: 0, dress: false, beret: false, earrings: false, necklace: false, fur: false,
  morphs: Object.freeze([]),
});

const face = values => values;
export const PRESETS = {
  jc: { male: true, skin: 1, hair: 2, hairColor: 0, shirt: 0, pants: 0, coat: true, sunglasses: true,
    morphs: face({ age: 25, cheekBones: 20, chinProjection: 15, noseProjection: 15, eyeOpening: -10, browAngle: -10 }) },
  agent: { male: true, skin: 3, hair: 1, hairColor: 0, shirt: 4, pants: 1, beard: 1,
    morphs: face({ african: 80, age: 35, cheekBones: 20, noseWidth: 20, noseFlare: 15, jawWidth: 10 }) },
  classic: { male: false, skin: 0, hair: 3, hairColor: 2, shirt: 2, pants: 3, eyeColor: 3,
    morphs: face({ faceShape: 20, jawWidth: -15, cheekBones: 15, noseWidth: -10, lowerLip: 10 }) },
  marilyn: { male: false, skin: 0, hair: 5, hairColor: 6, shirt: 6, pants: 0, eyeColor: 2,
    makeup: 4, lipColor: 1, dress: true, fur: true, necklace: true,
    creatorYaw: -0.42123766,
    creatorPitch: -0.14454021,
    creatorRoll: -0.07859953,
    portrait: 1,
    morphs: face({ age: 14, headWidth: -61, faceShape: 55, faceFullness: 47, cheekBones: -17, jawWidth: -49, chinHeight: -63, chinProjection: -25, eyeSize: 31, eyeOpening: 43, noseWidth: 36, noseProjection: -34, noseLength: 35, noseTip: -29, nosePoint: 5, mouthWidth: 39, upperLip: -10, lowerLip: -64, smile: 53, cupidsBow: 55, chinCleft: 100, eyeDepth: -23, epicanthus: 4, eyeBags: 13, noseBridge: -12, noseFlare: -40, nostrils: 51, mouthFullness: 38 }) },
  ibrahim: { male: true, skin: 4, hair: 1, hairColor: 0, shirt: 3, pants: 1, eyeColor: 0, beard: 1, beret: true,
    creatorYaw: -0.01952579,
    creatorPitch: -0.12351031,
    creatorRoll: 0.03656563,
    portrait: 2,
    morphs: face({ age: 24, african: 100, headWidth: 45, faceShape: -40, cheekBones: 30, cheekFullness: 16, jawWidth: 34, eyeOpening: -4, eyeDepth: -9, noseWidth: 30, noseProjection: 15, noseFlare: 19, nostrils: 11, lowerLip: -5, neckWidth: 15, shoulders: 20, muscles: 20, faceFullness: -15, chinCleft: -25, eyeSize: -4, eyeSpacing: -6, epicanthus: 4, noseLength: 7, noseTip: 13, noseBridge: -5, nosePoint: 18, mouthFullness: 8, smile: 4 }) },
  xi: { male: true, skin: 5, hair: 2, hairColor: 0, shirt: 0, pants: 0, eyeColor: 0, suit: true, tie: 1,
    creatorYaw: -0.19187904,
    creatorPitch: -0.01719577,
    creatorRoll: 0.03544049,
    portrait: 3,
    morphs: face({ age: 59, asian: 100, headWidth: 45, faceShape: 33, faceFullness: 35, cheekFullness: 13, jawWidth: 46, chinHeight: -34, eyeOpening: -11, epicanthus: 50, browHeight: 44, noseWidth: 60, noseProjection: -59, mouthWidth: -52, smile: 44, neckWidth: 10, headDepth: -5, cheekBones: -4, cheekHeight: 32, chinProjection: 26, chinCleft: -36, eyeSize: -3, browAngle: -20, noseLength: -37, noseTip: -14, noseBridge: -16, noseFlare: 24, nostrils: 48, mouthFullness: -6, lowerLip: -8, cupidsBow: -12, laughLines: 13 }) },
  putin: { male: true, skin: 0, hair: 8, hairColor: 4, shirt: 0, pants: 0, eyeColor: 2, suit: true, tie: 4,
    creatorYaw: 0.06722873,
    creatorPitch: 0.13586467,
    creatorRoll: -0.01020534,
    portrait: 4,
    morphs: face({ age: 72, headWidth: -9, faceShape: -29, faceFullness: -44, forehead: 17, cheekBones: 29, cheekFullness: -32, jawWidth: -56, chinProjection: 9, eyeOpening: -9, eyeDepth: -3, eyeBags: 3, browAngle: -18, noseProjection: 30, noseLength: 44, noseBridge: 39, nosePoint: -15, laughLines: 25, cheekHeight: 14, chinCleft: -56, eyeSize: 17, eyeTilt: 8, epicanthus: -19, browHeight: 14, noseWidth: 44, noseTip: -12, noseFlare: 26, nostrils: 30, upperLip: 15, lowerLip: -26, cupidsBow: -11 }) },
  trump: { male: true, skin: 1, hair: 4, hairColor: 7, shirt: 0, pants: 0, eyeColor: 2, suit: true, tie: 3,
    creatorYaw: 0.19157456,
    creatorPitch: 0.07089581,
    creatorRoll: 0.01636347,
    portrait: 5,
    morphs: face({ age: 59, headWidth: 68, faceFullness: 52, cheekFullness: 77, jawWidth: 54, chinHeight: 33, chinProjection: 89, eyeOpening: -8, noseWidth: 83, noseProjection: 85, noseLength: 45, mouthWidth: 99, upperLip: -21, lowerLip: -62, smile: 93, laughLines: 73, neckWidth: 15, torsoWidth: 15, stomach: 25, faceShape: 30, forehead: 14, cheekHeight: 63, chinCleft: -57, eyeTilt: 46, browAngle: -19, noseTip: -20, noseBridge: -20, noseFlare: 37, nostrils: 67, mouthFullness: -92, lipHeight: 64, cupidsBow: -16, earSize: -54 }) },
  taylor: { male: false, skin: 0, hair: 6, hairColor: 1, shirt: 0, pants: 0, eyeColor: 2,
    makeup: 3, lipColor: 1, earrings: true,
    creatorYaw: 0.02993686,
    creatorPitch: 0.10054902,
    creatorRoll: 0.02333125,
    portrait: 6,
    morphs: face({ headWidth: -26, faceShape: 53, jawWidth: -38, chinProjection: -8, eyeSize: 4, browHeight: 3, noseWidth: -46, noseTip: -15, upperLip: 13, smile: 18, cupidsBow: 39, age: 24, faceFullness: 41, chinHeight: 15, chinCleft: 34, epicanthus: -3, noseProjection: -24, noseLength: 45, noseBridge: 38, noseFlare: 30, nostrils: 25, laughLines: -7 }) },
  obama: { male: true, skin: 3, hair: 1, hairColor: 0, shirt: 0, pants: 0, eyeColor: 0, suit: true, tie: 1,
    creatorYaw: -0.00650220,
    creatorPitch: 0.12357411,
    creatorRoll: 0.01619271,
    portrait: 7,
    morphs: face({ age: 44, african: 75, headWidth: -35, headHeight: 14, faceShape: -23, faceFullness: -47, forehead: 99, cheekBones: -7, jawWidth: -64, chinHeight: -19, chinProjection: -60, eyeOpening: -17, browAngle: -32, noseWidth: 56, noseFlare: 23, mouthWidth: 25, laughLines: -24, earSize: 7, earStick: 8, neckWidth: -5, headDepth: -3, cheekHeight: 52, chinCleft: -95, eyeSize: -4, eyeSpacing: -3, eyeTilt: -43, epicanthus: 45, noseLength: -3, noseTip: 85, noseBridge: -91, nostrils: 8, nosePoint: 27, upperLip: -39, lowerLip: -39, cupidsBow: -26 }) },
};

const LIMITS = { skin: 5, portrait: 7, hair: 12, hairColor: 8, shirt: 6, pants: 3, beard: 2, eyeColor: 5, makeup: 4, lipColor: 4, tie: 5 };
const BOOLEANS = ['male', 'coat', 'sunglasses', 'suit', 'dress', 'beret', 'earrings', 'necklace', 'fur'];
const index = (value, fallback, maximum) => Number.isFinite(Number(value)) ?
  Math.max(0, Math.min(maximum, Math.round(Number(value)))) : fallback;

export function normalizeAppearance(data = {}) {
  const result = { ...DEFAULT_APPEARANCE, morphs: [] };
  for (const [key, maximum] of Object.entries(LIMITS)) result[key] = index(data[key], result[key], maximum);
  for (const key of BOOLEANS) if (typeof data[key] === 'boolean') result[key] = data[key];
  result.morphs = MORPH_DEFS.map((definition, position) => {
    const supplied = Array.isArray(data.morphs) ? data.morphs[position] : data.morphs?.[definition.key];
    const value = Number.isFinite(Number(supplied)) ? Number(supplied) : 0;
    return Math.max(definition.min, Math.min(definition.max, Math.round(value)));
  });
  return result;
}

export function appearanceKey(data) {
  const value = normalizeAppearance(data);
  const fields = Object.keys(DEFAULT_APPEARANCE).filter(key => key !== 'morphs').map(key => value[key]);
  return [...fields, ...value.morphs].join('|');
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
