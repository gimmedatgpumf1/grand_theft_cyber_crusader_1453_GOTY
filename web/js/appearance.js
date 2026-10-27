import { MORPH_DEFS } from './morphs.js';

export const PALETTES = {
  skin: ['#f0c8a8', '#d9a17d', '#b97c58', '#8a573e', '#714a38', '#e5b894'],
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
    morphs: face({ age: 20, headWidth: -17, faceShape: 37, faceFullness: 30, cheekBones: -17, jawWidth: -35, chinHeight: -25, chinProjection: -15, eyeSize: 31, eyeOpening: -18, noseWidth: -45, noseProjection: -45, noseLength: 0, noseTip: -35, nosePoint: 30, mouthWidth: 25, upperLip: 30, lowerLip: 35, smile: 35, cupidsBow: 55, chinCleft: 20, eyeDepth: -23, epicanthus: 4, eyeBags: 13, noseFlare: -35, nostrils: -10, mouthFullness: 38, cheekFullness: 25, browHeight: 20, browAngle: 15, lipHeight: 15, cupidsBowWidth: 15, upperLipHeight: 10, lowerLipHeight: 10, philtrum: 10, noseCurve: 10 }) },
  ibrahim: { male: true, skin: 4, hair: 1, hairColor: 0, shirt: 3, pants: 1, eyeColor: 0, beard: 1, beret: true,
    creatorYaw: -0.02750107,
    creatorPitch: -0.10650637,
    creatorRoll: 0.03747819,
    morphs: face({
      age: 7, african: 100, headWidth: 35, headHeight: -26, headDepth: 4,
      faceShape: 3, faceFullness: -10, forehead: -25, cheekBones: 2, cheekFullness: 12,
      cheekHeight: -42, jawWidth: 36, chinHeight: 26, chinProjection: -13, eyeSize: -44,
      eyeSpacing: 27, eyeDepth: 17, browHeight: 21, browAngle: -18, noseWidth: 54,
      noseProjection: 63, noseLength: 21, noseTip: 4, noseBridge: -3, noseFlare: 2,
      nostrils: -4, nosePoint: 16, mouthWidth: -35, mouthFullness: 8, lowerLip: -6,
      lipHeight: -21, smile: 5, earSize: -2, earStick: 1, neckWidth: 15,
      shoulders: 20, muscles: 20, chinBones: -17, jawDrop: -9, prognathism: -21,
      earDepth: -3, earHeight: -2, browDepth: 21, eyeHeight3: 16, outerEyeCorner: 3,
      eyeBagHeight: 7, eyeBagDepth: 10, eyeVertical: -48, eyeSocketDepth: -15, foreheadTemple: -6,
      foreheadSlope: -13, foreheadNubian: -21, headOval: 1, headDiamond: 6, headSquare: 21,
      headTriangle: -20, headAge: -21, cupidsBowWidth: -9, upperLipHeight: -21, lowerLipHeight: -21,
      upperLipWidth: -11, lowerLipWidth: -21, philtrum: 10, mouthVertical: 10, upperLipSides: -18,
      lowerLipSides: -21, upperLipMiddle: -14, lowerLipMiddle: -21, noseBase: 5, noseCurve: 6,
      noseCompression: -13, nostrilAngle: -11, noseVolume: 18, noseMidWidth: 1, noseLowerWidth: -6,
      noseVertical: -2, noseShiftDepth: -19

    }) },
  xi: { male: true, skin: 5, hair: 7, hairColor: 0, shirt: 0, pants: 0, eyeColor: 0, suit: true, tie: 1,
    creatorYaw: -0.18801298,
    creatorPitch: -0.00671100,
    creatorRoll: 0.03471682,
    morphs: face({
      age: 53, asian: 100, headWidth: 55, headHeight: 1, headDepth: 37,
      faceShape: 3, faceFullness: -14, forehead: -5, cheekBones: 7, cheekFullness: 18,
      cheekHeight: 4, jawWidth: 33, chinHeight: 21, chinProjection: 8, eyeSize: -2,
      eyeSpacing: -72, eyeDepth: -3, browHeight: 3, browAngle: 1, noseWidth: 96,
      noseProjection: 87, noseLength: 38, noseTip: 1, noseBridge: -2, noseFlare: 2,
      nostrils: 2, nosePoint: 1, mouthWidth: 82, mouthFullness: 3, lowerLip: -2,
      lipHeight: -3, smile: 3, laughLines: 2, earSize: -3, earStick: -1,
      neckWidth: 10, cheekInner: -2, chinBones: -6, prognathism: -3, earDepth: -3,
      earHeight: 1, earRotation: 3, browDepth: -3, eyeHeight2: -3, eyeHeight3: -3,
      outerEyeCorner: 3, eyeBagHeight: -3, eyeBagDepth: 3, eyeVertical: -66, eyeSocketDepth: -1,
      foreheadTemple: -20, foreheadSlope: 1, foreheadNubian: -1, headOval: 3, headDiamond: 3,
      headSquare: 10, headTriangle: 3, headAge: 1, upperLipHeight: -3, lowerLipHeight: 1,
      upperLipWidth: -1, philtrum: 3, mouthVertical: 11, upperLipSides: -3, lowerLipSides: -1,
      upperLipMiddle: -3, lowerLipMiddle: 3, noseBase: 1, noseCurve: 2, noseCompression: -3,
      nostrilAngle: 2, septumAngle: 2, noseVolume: 3, noseLowerWidth: 3, noseVertical: -44,
      noseShiftDepth: -3

    }) },
  putin: { male: true, skin: 0, hair: 8, hairColor: 8, shirt: 0, pants: 0, eyeColor: 2, suit: true, tie: 4,
    creatorYaw: 0.05406029,
    creatorPitch: 0.13800410,
    creatorRoll: -0.01123987,
    morphs: face({
      age: 45, headWidth: -6, headHeight: 35, headDepth: -22, faceShape: -82,
      faceFullness: -5, forehead: -25, cheekBones: 15, cheekFullness: -16, cheekHeight: 35,
      jawWidth: -44, chinHeight: -27, chinProjection: -24, chinCleft: -39, eyeSize: -2,
      eyeOpening: -8, eyeSpacing: 12, eyeDepth: 2, epicanthus: -9, eyeBags: -10,
      browHeight: 32, noseWidth: 24, noseProjection: -1, noseLength: 19, noseTip: 11,
      noseBridge: 10, noseFlare: 18, nostrils: 20, nosePoint: -15, mouthWidth: 26,
      mouthFullness: 4, upperLip: 13, lowerLip: -15, lipHeight: -27, smile: 22,
      cupidsBow: -18, laughLines: -2, earSize: 8, earStick: -18, cheekInner: -9,
      chinBones: 3, jawDrop: -2, prognathism: 36, earDepth: 3, earHeight: 3,
      earRotation: 9, earRound: 34, browDepth: -21, eyeHeight2: 1, eyeHeight3: 29,
      outerEyeCorner: 7, eyeBagHeight: -1, eyeBagDepth: 12, eyeVertical: 34, eyeSocketDepth: 21,
      foreheadTemple: 36, foreheadSlope: 18, foreheadNubian: -40, headOval: 55, headDiamond: 23,
      headSquare: 30, headTriangle: 30, headAge: 8, cupidsBowWidth: -1, dimples: 10,
      upperLipHeight: -23, lowerLipHeight: -24, upperLipWidth: 16, lowerLipWidth: -9, philtrum: -8,
      mouthVertical: 9, upperLipSides: -3, lowerLipSides: -7, upperLipMiddle: -6, lowerLipMiddle: 1,
      noseBase: 11, noseCurve: -72, noseCompression: 7, noseGreek: 15, nostrilAngle: -17,
      septumAngle: -11, noseVolume: 47, noseRootWidth: 11, noseMidWidth: 16, noseLowerWidth: 20,
      noseVertical: 22, noseShiftDepth: 29

    }) },
  trump: { male: true, skin: 1, hair: 4, hairColor: 7, shirt: 0, pants: 0, eyeColor: 2, suit: true, tie: 3,
    creatorYaw: 0.17867560,
    creatorPitch: 0.07796664,
    creatorRoll: 0.01485240,
    morphs: face({
      age: 58, headWidth: 60, faceShape: 38, faceFullness: 52, forehead: -41,
      cheekBones: 19, cheekFullness: 78, cheekHeight: 64, jawWidth: 58, chinHeight: 20,
      chinProjection: 87, chinCleft: -62, browAngle: -14, noseWidth: 69, noseProjection: 75,
      noseLength: 38, noseBridge: -16, noseFlare: 29, nostrils: 47, nosePoint: -10,
      mouthWidth: 70, mouthFullness: -84, upperLip: -13, lowerLip: -49, lipHeight: 20,
      smile: 70, cupidsBow: -25, laughLines: 75, earSize: -5, neckWidth: 15,
      torsoWidth: 15, stomach: 25, prognathism: -40, chinTriangle: 25, earDepth: 6,
      earHeight: 52, earRotation: 53, earRound: 9, outerEyeCorner: 4, eyeVertical: 20,
      foreheadTemple: 35, foreheadSlope: -5, foreheadNubian: 43, headDiamond: 6, headTriangle: 19,
      headAge: 25, dimples: 16, lowerLipHeight: 26, upperLipWidth: 42, lowerLipWidth: 85,
      philtrum: 8, mouthVertical: 3, upperLipSides: -5, lowerLipSides: -38, lowerLipMiddle: 36,
      noseCurve: -43, noseCompression: -34, noseGreek: -18, septumAngle: 31, noseRootWidth: 31,
      noseMidWidth: 30, noseLowerWidth: 36, noseShiftDepth: 40
    }) },
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
      septumAngle: 1, noseLowerWidth: -2, noseVertical: 4, noseShiftDepth: -1
    }) },
  obama: { male: true, skin: 3, hair: 1, hairColor: 0, shirt: 0, pants: 0, eyeColor: 0, suit: true, tie: 1,
    creatorYaw: -0.00650220,
    creatorPitch: 0.12357411,
    creatorRoll: 0.01619271,
    morphs: face({
      age: 18, african: 75, headWidth: -21, headHeight: -14, headDepth: -22,
      faceShape: -22, faceFullness: -10, forehead: 41, cheekBones: 7, cheekFullness: 6,
      cheekHeight: 59, jawWidth: -14, chinHeight: 7, chinProjection: -12, chinCleft: -18,
      eyeSize: -21, eyeOpening: 4, eyeSpacing: 24, eyeDepth: 10, eyeTilt: 1,
      epicanthus: 4, browHeight: 32, browAngle: 18, noseWidth: 30, noseProjection: -12,
      noseLength: -22, noseTip: 26, noseBridge: -8, noseFlare: 38, nostrils: 20,
      nosePoint: 35, mouthWidth: 37, mouthFullness: 1, upperLip: -3, lowerLip: -7,
      lipHeight: -27, smile: 26, cupidsBow: -10, laughLines: 14, earSize: 45,
      earStick: 13, neckWidth: -5, cheekInner: -9, chinBones: 20, jawDrop: 6,
      prognathism: -27, earDepth: 21, earHeight: 16, earRotation: 27, earRound: 22,
      browDepth: -18, eyeHeight2: -22, eyeHeight3: -26, outerEyeCorner: 17, eyeBagHeight: -17,
      eyeBagDepth: 14, eyeVertical: 20, eyeSocketDepth: -11, foreheadTemple: 1, foreheadSlope: -19,
      foreheadNubian: 23, headOval: 31, headDiamond: 7, headTriangle: 7, headAge: -12,
      cupidsBowWidth: -4, upperLipHeight: -13, lowerLipHeight: -27, upperLipWidth: 13, lowerLipWidth: -17,
      philtrum: -3, mouthVertical: 27, upperLipSides: -5, lowerLipSides: -18, upperLipMiddle: -7,
      lowerLipMiddle: -3, noseBase: 25, noseCurve: 27, noseCompression: -26, nostrilAngle: 8,
      septumAngle: -10, noseVolume: 26, noseLowerWidth: 5, noseVertical: 27, noseShiftDepth: -27

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
