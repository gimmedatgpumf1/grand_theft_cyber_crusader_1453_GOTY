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
      age: 18, african: 100, headWidth: 39, headHeight: -6, faceShape: -56,
      faceFullness: -26, forehead: -6, cheekBones: 17, cheekFullness: 11, cheekHeight: 13,
      jawWidth: 30, chinHeight: -3, chinProjection: -1, chinCleft: -24, eyeSize: -10,
      eyeOpening: -2, eyeSpacing: -1, eyeDepth: -4, eyeTilt: 1, epicanthus: 4,
      browHeight: 2, browAngle: -6, noseWidth: 30, noseProjection: 20, noseLength: -6,
      noseTip: 21, noseBridge: -5, noseFlare: 24, nostrils: 6, nosePoint: 27,
      mouthWidth: -6, mouthFullness: 13, upperLip: -4, lowerLip: -6, lipHeight: -6,
      smile: 3, cupidsBow: -3, earSize: -3, earStick: 2, neckWidth: 15,
      shoulders: 20, muscles: 20, cheekInner: 6, chinBones: -6, jawDrop: -4,
      prognathism: 32, earDepth: -17, earHeight: -3, earRotation: -6, browDepth: 9,
      eyeHeight2: 2, eyeHeight3: 29, outerEyeCorner: 3, eyeBagHeight: 2, eyeBagDepth: 4,
      eyeVertical: -6, eyeSocketDepth: -6, foreheadTemple: 18, foreheadNubian: -6, headOval: 18,
      headSquare: 6, headTriangle: 13, headAge: -6, cupidsBowWidth: -4, dimples: 2,
      upperLipHeight: 2, lowerLipHeight: -6, upperLipWidth: -6, lowerLipWidth: -6, philtrum: 8,
      mouthVertical: 29, upperLipSides: 3, lowerLipSides: -6, upperLipMiddle: 5, lowerLipMiddle: -6,
      noseBase: 6, noseCurve: 21, noseCompression: -6, septumAngle: -3, noseVolume: 23,
      noseMidWidth: 12, noseLowerWidth: -1, noseVertical: 6, noseShiftDepth: 1, ibrahimSculpt: 100
    }) },
  xi: { male: true, skin: 5, hair: 7, hairColor: 0, shirt: 0, pants: 0, eyeColor: 0, suit: true, tie: 1,
    creatorYaw: -0.18801298,
    creatorPitch: -0.00671100,
    creatorRoll: 0.03471682,
    morphs: face({
      age: 55, asian: 100, headWidth: 42, headHeight: 4, faceShape: 40,
      faceFullness: 19, forehead: 5, cheekBones: -5, cheekFullness: 16, cheekHeight: 45,
      jawWidth: 8, chinHeight: -13, chinProjection: 29, chinCleft: -30, eyeSize: -6,
      eyeOpening: -9, eyeSpacing: -6, eyeDepth: -6, epicanthus: 43, browHeight: 21,
      browAngle: -21, noseWidth: 49, noseProjection: -43, noseLength: -30, noseBridge: -11,
      noseFlare: 13, nostrils: 40, nosePoint: -1, mouthWidth: -22, mouthFullness: -5,
      upperLip: -1, lowerLip: -4, lipHeight: -6, smile: 39, cupidsBow: -9,
      laughLines: 21, earSize: -1, neckWidth: 10, cheekInner: -2, chinBones: 1,
      jawDrop: -3, prognathism: -6, chinTriangle: 4, earDepth: -5, earHeight: 15,
      earRotation: 9, browDepth: -6, eyeHeight2: -11, eyeHeight3: 14, outerEyeCorner: 6,
      eyeBagHeight: -5, eyeBagDepth: 6, eyeVertical: 6, eyeSocketDepth: 32, foreheadTemple: 26,
      foreheadSlope: -2, foreheadNubian: 6, headOval: 6, headDiamond: 6, headSquare: 6,
      headTriangle: -53, headAge: -37, cupidsBowWidth: 2, dimples: 8, upperLipHeight: -2,
      lowerLipHeight: -6, upperLipWidth: 6, lowerLipWidth: 11, philtrum: -21, mouthVertical: 27,
      upperLipSides: 5, lowerLipSides: -5, upperLipMiddle: -2, lowerLipMiddle: -10, noseBase: -3,
      noseCurve: -24, noseCompression: -29, noseGreek: 11, nostrilAngle: -7, septumAngle: -11,
      noseVolume: 3, noseMidWidth: 17, noseLowerWidth: 34, noseVertical: 2, noseShiftDepth: -26,
      xiSculpt: 75
    }) },
  putin: { male: true, skin: 0, hair: 8, hairColor: 8, shirt: 0, pants: 0, eyeColor: 2, suit: true, tie: 4,
    creatorYaw: 0.05406029,
    creatorPitch: 0.13800410,
    creatorRoll: -0.01123987,
    morphs: face({
      age: 66, headWidth: -1, headHeight: 24, headDepth: -6, faceShape: -81,
      faceFullness: -14, forehead: -12, cheekBones: 23, cheekFullness: -36, cheekHeight: 34,
      jawWidth: -65, chinHeight: -6, chinProjection: -3, chinCleft: -60, eyeSize: 10,
      eyeOpening: -16, eyeDepth: 2, epicanthus: -18, eyeBags: -20, browHeight: 11,
      browAngle: -3, noseWidth: 30, noseProjection: 16, noseLength: 39, noseTip: 5,
      noseBridge: 31, noseFlare: 17, nostrils: 25, nosePoint: -21, mouthWidth: 6,
      mouthFullness: 2, upperLip: 26, lowerLip: -30, lipHeight: -6, smile: 6,
      cupidsBow: -37, laughLines: -4, earSize: 6, earStick: -4, cheekInner: -8,
      chinBones: -1, jawDrop: 2, prognathism: 57, earDepth: -18, earHeight: -12,
      earRotation: -12, earRound: 34, eyeHeight2: 9, eyeHeight3: 26, outerEyeCorner: 1,
      eyeBagHeight: 5, eyeBagDepth: 2, eyeVertical: 24, eyeSocketDepth: 31, foreheadTemple: 56,
      foreheadSlope: 24, foreheadNubian: -28, headOval: 46, headDiamond: 6, headSquare: 9,
      headTriangle: 43, headAge: 29, dimples: 20, upperLipHeight: -6, lowerLipHeight: -5,
      upperLipWidth: 6, lowerLipWidth: 2, philtrum: -3, mouthVertical: -2, lowerLipSides: -2,
      upperLipMiddle: -5, lowerLipMiddle: -2, noseBase: 6, noseCurve: -93, noseCompression: 28,
      noseGreek: 29, nostrilAngle: -5, septumAngle: -6, noseVolume: 34, noseRootWidth: 22,
      noseMidWidth: 28, noseLowerWidth: 18, noseVertical: 3, noseShiftDepth: 25, putinSculpt: 75
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
      age: 39, african: 75, headWidth: -10, headHeight: 4, headDepth: -5,
      faceShape: -22, faceFullness: -19, forehead: 33, cheekBones: 11, cheekFullness: -12,
      cheekHeight: 50, jawWidth: -21, chinHeight: -5, chinProjection: -27, chinCleft: -19,
      eyeSize: -6, eyeOpening: 7, eyeSpacing: 3, eyeDepth: 1, eyeTilt: 1,
      epicanthus: 8, browHeight: 11, noseWidth: 35, noseProjection: 9, noseLength: -1,
      noseTip: 27, noseBridge: -16, noseFlare: 21, nostrils: 12, nosePoint: 16,
      mouthWidth: 24, mouthFullness: 1, upperLip: -9, lowerLip: -6, lipHeight: -6,
      smile: 39, cupidsBow: -22, laughLines: 20, earSize: 25, earStick: 25,
      neckWidth: -5, cheekInner: -5, chinBones: 4, jawDrop: 2, prognathism: -6,
      earDepth: 4, earHeight: 4, earRotation: 6, earRound: 5, browDepth: -6,
      eyeHeight2: -6, eyeHeight3: -6, outerEyeCorner: 6, eyeBagHeight: -6, eyeBagDepth: 6,
      eyeVertical: 1, eyeSocketDepth: -6, foreheadTemple: 2, foreheadSlope: -5, foreheadNubian: 3,
      headOval: 20, headDiamond: 6, headTriangle: 6, headAge: -6, cupidsBowWidth: -1,
      upperLipHeight: 1, lowerLipHeight: -6, upperLipWidth: 3, lowerLipWidth: -6, philtrum: 1,
      mouthVertical: 6, upperLipSides: 3, lowerLipSides: -6, upperLipMiddle: 2, lowerLipMiddle: -6,
      noseBase: 6, noseCurve: 16, noseCompression: -6, nostrilAngle: 5, septumAngle: -2,
      noseVolume: 6, noseLowerWidth: 4, noseVertical: 6, noseShiftDepth: -6, obamaSculpt: 100
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
