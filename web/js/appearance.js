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

export const DEFAULT_APPEARANCE = Object.freeze({
  male: true, skin: 1, hair: 2, hairColor: 0, shirt: 0, pants: 0,
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
  marilyn: { male: false, skin: 0, hair: 13, hairColor: 6, shirt: 6, pants: 0, eyeColor: 2,
    makeup: 4, lipColor: 1, dress: true, fur: true, necklace: true,
    creatorYaw: -0.42123766,
    creatorPitch: -0.14454021,
    creatorRoll: -0.07859953,
    morphs: face({ age: 20, headWidth: -17, faceShape: 37, faceFullness: 30, cheekBones: -17, jawWidth: -35, chinHeight: -25, chinProjection: -15, eyeSize: 31, eyeOpening: -18, noseWidth: -45, noseProjection: -45, noseLength: 0, noseTip: -35, nosePoint: 30, mouthWidth: 25, upperLip: 30, lowerLip: 35, smile: 35, cupidsBow: 55, chinCleft: 20, eyeDepth: -23, epicanthus: 4, eyeBags: 13, noseFlare: -35, nostrils: -10, mouthFullness: 38, cheekFullness: 25, browHeight: 20, browAngle: 15, lipHeight: 15, cupidsBowWidth: 15, upperLipHeight: 10, lowerLipHeight: 10, philtrum: 10, noseCurve: 10, marilynSculpt: 100 }) },
  ibrahim: { male: true, skin: 4, hair: 1, hairColor: 0, shirt: 3, pants: 1, eyeColor: 0, beard: 1, beret: true,
    creatorYaw: -0.02750107,
    creatorPitch: -0.10650637,
    creatorRoll: 0.03747819,
    morphs: face({ age: 24, african: 100, headWidth: 42, faceShape: -46, cheekBones: 29, cheekFullness: 15, jawWidth: 36, eyeOpening: -3, eyeDepth: -10, noseWidth: 31, noseProjection: 21, noseFlare: 19, nostrils: 10, upperLip: -4, lowerLip: -5, neckWidth: 15, shoulders: 20, muscles: 20, faceFullness: -13, chinCleft: -28, eyeSize: -4, eyeSpacing: -7, epicanthus: 5, browHeight: -4, noseTip: 14, noseBridge: -6, nosePoint: 24, mouthFullness: 15, cupidsBow: -3, cheekInner: 5, chinBones: -6, prognathism: 38, earDepth: -21, browDepth: 3, eyeHeight2: 8, eyeHeight3: 25, foreheadTemple: 21, headOval: 21, headTriangle: 19, dimples: 3, philtrum: 10, mouthVertical: 23, noseCurve: 21, noseVolume: 17, noseMidWidth: 14, ibrahimSculpt: 100 }) },
  xi: { male: true, skin: 5, hair: 7, hairColor: 0, shirt: 0, pants: 0, eyeColor: 0, suit: true, tie: 1,
    creatorYaw: -0.18801298,
    creatorPitch: -0.00671100,
    creatorRoll: 0.03471682,
    morphs: face({ age: 59, asian: 100, headWidth: 33, faceShape: 28, faceFullness: 13, cheekFullness: 10, jawWidth: 18, chinHeight: -18, eyeOpening: -11, epicanthus: 49, browHeight: 15, noseWidth: 47, noseProjection: -49, mouthWidth: -28, smile: 33, neckWidth: 10, headDepth: -5, cheekBones: -11, cheekHeight: 36, chinProjection: 35, chinCleft: -35, browAngle: -22, noseLength: -32, noseTip: -3, noseBridge: -14, noseFlare: 10, nostrils: 34, mouthFullness: -11, lowerLip: -4, cupidsBow: -11, laughLines: 16, jawDrop: -4, chinTriangle: 5, earHeight: 15, earRotation: 3, eyeHeight2: -8, eyeHeight3: 20, eyeSocketDepth: 38, foreheadTemple: 31, foreheadSlope: -8, headOval: 3, headTriangle: -59, headAge: -43, dimples: 9, lowerLipWidth: 5, philtrum: -27, mouthVertical: 21, lowerLipMiddle: -5, noseCurve: -30, noseCompression: -33, noseGreek: 13, nostrilAngle: -11, septumAngle: -14, noseMidWidth: 20, noseLowerWidth: 28, noseShiftDepth: -20 }) },
  putin: { male: true, skin: 0, hair: 8, hairColor: 8, shirt: 0, pants: 0, eyeColor: 2, suit: true, tie: 4,
    creatorYaw: 0.05406029,
    creatorPitch: 0.13800410,
    creatorRoll: -0.01123987,
    morphs: face({ age: 72, headWidth: 7, faceShape: -78, faceFullness: -27, forehead: -16, cheekBones: 27, cheekFullness: -41, jawWidth: -71, eyeOpening: -20, eyeBags: -24, browAngle: -8, noseProjection: 22, noseLength: 45, noseBridge: 37, nosePoint: -22, laughLines: -5, headHeight: 10, cheekHeight: 35, chinCleft: -66, epicanthus: -22, browHeight: 5, noseWidth: 35, noseTip: 3, noseFlare: 17, nostrils: 26, upperLip: 32, lowerLip: -36, cupidsBow: -43, cheekInner: -8, chinBones: -3, prognathism: 55, earDepth: -24, earHeight: -18, earRotation: -18, earRound: 28, browDepth: 6, eyeHeight2: 15, eyeHeight3: 20, eyeBagHeight: 8, eyeVertical: 20, eyeSocketDepth: 33, foreheadTemple: 62, foreheadSlope: 30, foreheadNubian: -23, headOval: 41, headSquare: 3, headTriangle: 47, headAge: 35, dimples: 25, noseCurve: -99, noseCompression: 34, noseGreek: 35, noseVolume: 28, noseRootWidth: 27, noseMidWidth: 33, noseLowerWidth: 19, noseVertical: -3, noseShiftDepth: 27, putinSculpt: 100 }) },
  trump: { male: true, skin: 1, hair: 4, hairColor: 7, shirt: 0, pants: 0, eyeColor: 2, suit: true, tie: 3,
    creatorYaw: 0.17867560,
    creatorPitch: 0.07796664,
    creatorRoll: 0.01485240,
    morphs: face({ age: 58, headWidth: 68, faceFullness: 44, cheekFullness: 78, jawWidth: 58, chinHeight: 20, chinProjection: 87, noseWidth: 69, noseProjection: 75, noseLength: 38, mouthWidth: 70, upperLip: -13, lowerLip: -49, smile: 70, laughLines: 75, neckWidth: 15, torsoWidth: 15, stomach: 25, faceShape: 30, forehead: -41, cheekBones: 19, cheekHeight: 64, chinCleft: -62, browAngle: -14, noseBridge: -16, noseFlare: 29, nostrils: 47, nosePoint: -10, mouthFullness: -84, lipHeight: 20, cupidsBow: -25, earSize: -5, prognathism: -40, chinTriangle: 25, earDepth: 6, earHeight: 52, earRotation: 53, earRound: 9, outerEyeCorner: 4, eyeVertical: 20, foreheadTemple: 35, foreheadSlope: -5, foreheadNubian: 43, headDiamond: 6, headTriangle: 19, headAge: 25, dimples: 16, lowerLipHeight: 26, upperLipWidth: 42, lowerLipWidth: 85, philtrum: 8, mouthVertical: 3, upperLipSides: -5, lowerLipSides: -38, lowerLipMiddle: 36, noseCurve: -43, noseCompression: -34, noseGreek: -18, septumAngle: 31, noseRootWidth: 31, noseMidWidth: 30, noseLowerWidth: 36, noseShiftDepth: 40, trumpSculpt: 100 }) },
  taylor: { male: false, skin: 0, hair: 6, hairColor: 2, shirt: 0, pants: 0, eyeColor: 2,
    makeup: 3, lipColor: 3, earrings: true,
    creatorYaw: 0.02993686,
    creatorPitch: 0.10054902,
    creatorRoll: 0.02333125,
    morphs: face({ headWidth: -20, faceShape: 13, jawWidth: -35, chinProjection: 0, eyeSize: 0, browHeight: 5, noseWidth: -40, noseProjection: -20, noseTip: -25, upperLip: 10, smile: 20, cupidsBow: 30, age: 24, faceFullness: -8, chinHeight: 0, chinCleft: 0, epicanthus: -3, noseLength: 15, noseBridge: 10, noseFlare: -15, nostrils: 0, laughLines: -7, eyeOpening: 0, eyeTilt: -12, browAngle: 5, mouthWidth: 5, lowerLip: 15, lipHeight: 5, upperLipWidth: 5, lowerLipWidth: 10, cheekBones: 23 }) },
  obama: { male: true, skin: 3, hair: 1, hairColor: 0, shirt: 0, pants: 0, eyeColor: 0, suit: true, tie: 1,
    creatorYaw: -0.00650220,
    creatorPitch: 0.12357411,
    creatorRoll: 0.01619271,
    morphs: face({ age: 45, african: 75, headWidth: -12, headHeight: 10, faceShape: -20, faceFullness: -25, forehead: 45, cheekBones: 10, jawWidth: -35, chinProjection: -20, eyeOpening: 8, browAngle: -5, noseWidth: 25, noseFlare: 15, mouthWidth: 20, laughLines: 20, earSize: 25, earStick: 30, neckWidth: -5, cheekHeight: 52, chinCleft: -20, eyeSpacing: -3, eyeTilt: 0, epicanthus: 10, noseLength: 5, noseTip: 25, noseBridge: -20, nostrils: 10, nosePoint: 10, upperLip: -10, cupidsBow: -26, cheekFullness: -18, eyeDepth: -5, browHeight: 5, noseProjection: 15, smile: 33, noseCurve: 10, headOval: 15, obamaSculpt: 100 }) },
};

const LIMITS = { skin: 5, hair: 15, hairColor: 8, shirt: 6, pants: 3, beard: 2, eyeColor: 5, makeup: 4, lipColor: 4, tie: 5 };
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
