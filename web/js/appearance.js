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
    morphs: face({
      age: 30, african: 100, headWidth: 35, headHeight: 10, headDepth: -6,
      faceShape: -40, faceFullness: -16, forehead: -6, cheekBones: 24, cheekFullness: 17,
      cheekHeight: 1, jawWidth: 33, chinHeight: 6, chinProjection: -5, chinCleft: -23,
      eyeSize: -18, eyeOpening: -2, eyeSpacing: -1, eyeDepth: -10, epicanthus: 4,
      browHeight: 2, browAngle: 2, noseWidth: 33, noseProjection: 19, noseLength: 1,
      noseTip: 12, noseBridge: -5, noseFlare: 23, nostrils: 16, nosePoint: 26,
      mouthWidth: -6, mouthFullness: 15, upperLip: -13, lowerLip: -5, lipHeight: -6,
      smile: -6, cupidsBow: -1, laughLines: -1, earSize: -1, earStick: 3,
      neckWidth: 15, shoulders: 20, muscles: 20, cheekInner: 3, chinBones: -4,
      jawDrop: -5, prognathism: 35, earDepth: -15, earHeight: -6, earRotation: -6,
      browDepth: 9, eyeHeight2: -8, eyeHeight3: 18, eyeBagHeight: 1, upperEyelid: -10,
      eyeVertical: 3, eyeSocketDepth: -6, foreheadTemple: 19, foreheadSlope: -6, foreheadNubian: -6,
      headOval: 16, headSquare: 3, headTriangle: 13, headAge: 6, cupidsBowWidth: -3,
      dimples: 3, upperLipHeight: -6, lowerLipHeight: -6, upperLipWidth: -6, lowerLipWidth: -6,
      philtrum: 15, mouthVertical: 17, upperLipSides: -6, lowerLipSides: -6, upperLipMiddle: -6,
      lowerLipMiddle: -6, noseBase: -6, noseCurve: 15, noseCompression: 3, nostrilAngle: -1,
      septumAngle: 3, noseVolume: 16, noseMidWidth: 12, noseLowerWidth: 3, noseVertical: -6,
      noseShiftDepth: -6
    }) },
  xi: { male: true, skin: 5, hair: 7, hairColor: 0, shirt: 0, pants: 0, eyeColor: 0, suit: true, tie: 1,
    creatorYaw: -0.18801298,
    creatorPitch: -0.00671100,
    creatorRoll: 0.03471682,
    morphs: face({ age: 59, asian: 100, headWidth: 33, faceShape: 28, faceFullness: 13, cheekFullness: 10, jawWidth: 18, chinHeight: -18, eyeOpening: -11, epicanthus: 49, browHeight: 15, noseWidth: 47, noseProjection: -49, mouthWidth: -28, smile: 33, neckWidth: 10, headDepth: -5, cheekBones: -11, cheekHeight: 36, chinProjection: 35, chinCleft: -35, browAngle: -22, noseLength: -32, noseTip: -3, noseBridge: -14, noseFlare: 10, nostrils: 34, mouthFullness: -11, lowerLip: -4, cupidsBow: -11, laughLines: 16, jawDrop: -4, chinTriangle: 5, earHeight: 15, earRotation: 3, eyeHeight2: -8, eyeHeight3: 20, eyeSocketDepth: 38, foreheadTemple: 31, foreheadSlope: -8, headOval: 3, headTriangle: -59, headAge: -43, dimples: 9, lowerLipWidth: 5, philtrum: -27, mouthVertical: 21, lowerLipMiddle: -5, noseCurve: -30, noseCompression: -33, noseGreek: 13, nostrilAngle: -11, septumAngle: -14, noseMidWidth: 20, noseLowerWidth: 28, noseShiftDepth: -20 }) },
  putin: { male: true, skin: 0, hair: 8, hairColor: 8, shirt: 0, pants: 0, eyeColor: 2, suit: true, tie: 4,
    creatorYaw: 0.05406029,
    creatorPitch: 0.13800410,
    creatorRoll: -0.01123987,
    morphs: face({
      age: 71, headWidth: 11, headHeight: 16, faceShape: -72, faceFullness: -25,
      forehead: -22, cheekBones: 21, cheekFullness: -42, cheekHeight: 33, jawWidth: -65,
      chinHeight: -4, chinProjection: 2, chinCleft: -60, eyeSize: -5, eyeOpening: -16,
      eyeSpacing: -5, eyeDepth: -4, epicanthus: -18, eyeBags: -20, browHeight: 1,
      browAngle: 3, noseWidth: 35, noseProjection: 16, noseLength: 50, noseTip: 5,
      noseBridge: 38, noseFlare: 22, nostrils: 24, nosePoint: -18, mouthWidth: 6,
      mouthFullness: 2, upperLip: 28, lowerLip: -30, lipHeight: -6, smile: -6,
      cupidsBow: -37, laughLines: -4, earSize: -6, earStick: -5, cheekInner: -6,
      chinBones: -3, jawDrop: -1, prognathism: 60, earDepth: -20, earHeight: -16,
      earRotation: -21, earRound: 23, browDepth: 10, eyeHeight2: 9, eyeHeight3: 26,
      outerEyeCorner: 3, eyeBagHeight: 5, eyeBagDepth: -2, eyeVertical: 26, eyeSocketDepth: 27,
      foreheadTemple: 56, foreheadSlope: 35, foreheadNubian: -29, headOval: 35, headDiamond: 4,
      headSquare: 7, headTriangle: 44, headAge: 36, cupidsBowWidth: -2, dimples: 20,
      upperLipHeight: -6, lowerLipHeight: 4, upperLipWidth: -1, lowerLipWidth: -6, philtrum: 1,
      mouthVertical: -6, upperLipSides: -6, upperLipMiddle: -6, lowerLipMiddle: 6, noseBase: -3,
      noseCurve: -93, noseCompression: 29, noseGreek: 29, nostrilAngle: 2, septumAngle: 6,
      noseVolume: 29, noseRootWidth: 22, noseMidWidth: 28, noseLowerWidth: 20, noseVertical: -9,
      noseShiftDepth: 27
    }) },
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
    morphs: face({
      age: 18, headWidth: -16, faceShape: 18, faceFullness: -18, forehead: -6,
      cheekBones: 22, cheekFullness: 3, cheekHeight: 6, jawWidth: -44, chinHeight: 1,
      chinProjection: -6, chinCleft: -6, eyeSize: -6, eyeOpening: 4, eyeSpacing: 6,
      eyeDepth: 3, eyeTilt: -18, epicanthus: 2, browHeight: 11, browAngle: 11,
      noseWidth: -38, noseProjection: -15, noseLength: 13, noseTip: -21, noseBridge: 7,
      noseFlare: -15, nosePoint: -2, mouthWidth: 2, mouthFullness: -2, lowerLip: 9,
      lipHeight: -1, smile: 22, cupidsBow: 25, laughLines: -7, earSize: 3,
      earStick: -5, cheekInner: -2, chinBones: -6, jawDrop: -2, prognathism: -6,
      chinTriangle: 1, earLobe: 1, earFlap: -2, earHeight: 2, earRound: 2,
      browDepth: -6, eyeHeight2: 16, eyeHeight3: 6, outerEyeCorner: -6, eyeBagHeight: 2,
      eyeBagDepth: 1, eyelidAngle: 10, eyeVertical: -6, eyeSocketDepth: -6, foreheadTemple: 6,
      foreheadSlope: 2, foreheadNubian: -6, headDiamond: 6, headSquare: 4, headTriangle: 1,
      headAge: -6, dimples: -1, lowerLipHeight: -6, upperLipWidth: 6, lowerLipWidth: 8,
      mouthVertical: 6, upperLipSides: 1, lowerLipSides: 2, lowerLipMiddle: -6, nostrilAngle: 2,
      septumAngle: 1, noseLowerWidth: -2, noseVertical: 4, noseShiftDepth: -1, taylorSculpt: 100
    }) },
  obama: { male: true, skin: 3, hair: 1, hairColor: 0, shirt: 0, pants: 0, eyeColor: 0, suit: true, tie: 1,
    creatorYaw: -0.00650220,
    creatorPitch: 0.12357411,
    creatorRoll: 0.01619271,
    morphs: face({
      age: 44, african: 75, headWidth: -5, headHeight: 15, faceShape: -18,
      faceFullness: -25, forehead: 45, cheekFullness: -5, cheekHeight: 56, jawWidth: -30,
      chinHeight: 6, chinProjection: -9, chinCleft: -18, eyeSize: 4, eyeOpening: -3,
      eyeSpacing: 3, eyeDepth: -2, epicanthus: 8, browHeight: 1, browAngle: -2,
      noseWidth: 27, noseProjection: 9, noseLength: 3, noseTip: 22, noseBridge: -16,
      noseFlare: 20, nostrils: 12, nosePoint: 10, mouthWidth: 26, mouthFullness: 2,
      upperLip: -9, lowerLip: -6, lipHeight: -6, smile: 31, cupidsBow: -21,
      laughLines: 18, earSize: 25, earStick: 25, neckWidth: -5, cheekInner: -3,
      chinBones: 1, prognathism: -6, earHeight: 1, earRotation: 4, browDepth: -3,
      eyeHeight2: 4, eyeHeight3: -6, outerEyeCorner: -6, eyeBagHeight: -5, eyelidFold: -10,
      eyeVertical: 5, eyeSocketDepth: -14, foreheadTemple: 1, foreheadSlope: -5, foreheadNubian: 6,
      headOval: 17, headDiamond: 1, headTriangle: 4, headAge: 6, cupidsBowWidth: -2,
      upperLipHeight: -3, lowerLipHeight: -6, upperLipWidth: 6, lowerLipWidth: -4, philtrum: 1,
      mouthVertical: -1, upperLipSides: -3, lowerLipSides: -5, upperLipMiddle: -3, lowerLipMiddle: -6,
      noseBase: 1, noseCurve: 9, noseCompression: -3, septumAngle: -1, noseVolume: 2,
      noseLowerWidth: 4, noseVertical: 4, noseShiftDepth: -6
    }) },
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
