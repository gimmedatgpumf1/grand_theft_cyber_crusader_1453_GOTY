import * as THREE from './vendor/three.module.min.js';
import { PORTRAIT_FACES } from './portrait-face-data.js';

// User-supplied portrait references projected in the fitted bind pose. Numeric keys
// match PORTRAIT_KEYS; no runtime downloads or external services are involved.
export const PORTRAIT_PROJECTORS = Object.freeze({
  1: Object.freeze({
    texture: 'assets/human/portrait-marilyn-source.webp', width: 750, height: 1077,
    scale: 189.511484071, translate: [287.692173589, 349.047365823],
    matrix: [8.11420388386, 1.21667226657, -3.49685071556, -0.639089238689, 8.75750084605, 1.56406677493, 3.64689200226, -1.17236876759, 8.05445784623],
    offset: [-1.60703400949, -14.9089720686, 0.962912526657],
  }),
  2: Object.freeze({
    texture: 'assets/human/portrait-ibrahim-source.webp', width: 750, height: 945,
    scale: 185.973883283, translate: [368.260009984, 339.435575555],
    matrix: [9.70349142516, -0.328990735161, -0.231788594551, 0.354972451086, 9.63226303219, 1.18878514965, 0.189619132355, -1.19623604244, 9.63601424915],
    offset: [0.586596350492, -16.4227148995, 0.725552862915],
  }),
  3: Object.freeze({
    texture: 'assets/human/portrait-xi-source.webp', width: 750, height: 999,
    scale: 214.204951184, translate: [347.037313584, 314.769523453],
    matrix: [9.52761154604, -0.312242803161, -1.85656385675, 0.337804689043, 9.7054282881, 0.101274039346, 1.85208339833, -0.16392946916, 9.53218867361],
    offset: [0.776534684181, -16.4001884989, -1.00344306766],
  }),
  4: Object.freeze({
    texture: 'assets/human/portrait-putin-source.webp', width: 750, height: 1032,
    scale: 240.664442349, translate: [394.202841777, 330.75714876],
    matrix: [9.68938971105, 0.186561641642, 0.632952190656, -0.0988869897766, 9.62093185918, -1.32196755588, -0.652422474353, 1.31246775557, 9.60059789805],
    offset: [-0.399998200344, -16.0663767728, -3.5053709844],
  }),
  5: Object.freeze({
    texture: 'assets/human/portrait-trump-source.webp', width: 1137, height: 1440,
    scale: 240.133817711, translate: [571.938956034, 495.463518011],
    matrix: [9.53288521204, -0.0275411291977, 1.855545126, 0.156004958214, 9.68828307129, -0.65767794396, -1.84918054322, 0.675366057159, 9.51021128587],
    offset: [-0.202708710857, -16.2693095716, -2.41755292546],
  }),
  6: Object.freeze({
    texture: 'assets/human/portrait-taylor-source.webp', width: 705, height: 705,
    scale: 237.184186026, translate: [372.369407924, 291.228961673],
    matrix: [8.91257730663, -0.180230820866, 0.286432548102, 0.20797928649, 8.87216198941, -0.888846021522, -0.26696698259, 0.894885179671, 8.86997576916],
    offset: [0.267002412691, -14.7950074001, -2.6122160567],
  }),
  7: Object.freeze({
    texture: 'assets/human/portrait-obama-source.webp', width: 960, height: 1290,
    scale: 305.594286481, translate: [497.965246307, 448.571365465],
    matrix: [9.71035484678, -0.163837476177, -0.0432749978985, 0.157250728399, 9.63638553949, -1.19793682517, 0.0631478618894, 1.1970537624, 9.63757133991],
    offset: [0.282434260345, -16.1091266213, -3.31547240893],
  })
});

const textures = new Map();
export function portraitTexture(portrait) {
  if (textures.has(portrait)) return textures.get(portrait);
  const config = PORTRAIT_PROJECTORS[portrait];
  if (!config) return null;
  const texture = new THREE.TextureLoader().load(config.texture);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = 4;
  textures.set(portrait, texture);
  return texture;
}

// Replace UVs on a bind-pose geometry. `offset` converts local accessory vertices
// to the same visual-space coordinates as the body; head geometry passes no offset.
export function projectPortraitUvs(geometry, portrait, offset = null) {
  const config = PORTRAIT_PROJECTORS[portrait];
  if (!config) return false;
  const position = geometry.attributes.position;
  const uv = new Float32Array(position.count * 2);
  const m = config.matrix;
  for (let i = 0; i < position.count; ++i) {
    const x = position.getX(i) + (offset?.[0] || 0);
    const y = position.getY(i) + (offset?.[1] || 0);
    const z = position.getZ(i) + (offset?.[2] || 0);
    const qx = m[0] * x + m[1] * y + m[2] * z + config.offset[0];
    const qy = m[3] * x + m[4] * y + m[5] * z + config.offset[1];
    uv[i * 2] = (config.translate[0] + config.scale * qx) / config.width;
    uv[i * 2 + 1] = 1 - (config.translate[1] - config.scale * qy) / config.height;
  }
  geometry.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
  return true;
}

export function portraitFaceGeometry(portrait) {
  const data = PORTRAIT_FACES[portrait];
  if (!data) return null;
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(data.positions, 3));
  geometry.setAttribute('uv', new THREE.Float32BufferAttribute(data.uvs, 2));
  geometry.setIndex(data.indices);
  geometry.computeVertexNormals();
  return geometry;
}

function insideHull(u, v, hull) {
  let inside = false;
  for (let i = 0, j = hull.length / 2 - 1; i < hull.length / 2; j = i++) {
    const x = hull[i * 2], y = hull[i * 2 + 1];
    const px = hull[j * 2], py = hull[j * 2 + 1];
    if ((y > v) !== (py > v) && u < (px - x) * (v - y) / (py - y) + x) inside = !inside;
  }
  return inside;
}

// Remove only the front-facing generic facial surface. The skull, ears, neck,
// back of the head, and eye sockets remain, and the landmark patch fills the hole.
export function filterPortraitFace(geometry, portrait) {
  const config = PORTRAIT_PROJECTORS[portrait];
  const face = PORTRAIT_FACES[portrait];
  if (!config || !face || !geometry.index) return;
  const source = geometry.index.array;
  const position = geometry.attributes.position;
  const indices = [];
  const m = config.matrix;
  const point = index => {
    const x = position.getX(index), y = position.getY(index), z = position.getZ(index);
    return [m[0] * x + m[1] * y + m[2] * z + config.offset[0],
      m[3] * x + m[4] * y + m[5] * z + config.offset[1],
      m[6] * x + m[7] * y + m[8] * z + config.offset[2]];
  };
  for (let i = 0; i < source.length; i += 3) {
    const a = point(source[i]), b = point(source[i + 1]), c = point(source[i + 2]);
    const ab = b.map((value, axis) => value - a[axis]);
    const ac = c.map((value, axis) => value - a[axis]);
    const nz = ab[0] * ac[1] - ab[1] * ac[0];
    const qx = (a[0] + b[0] + c[0]) / 3, qy = (a[1] + b[1] + c[1]) / 3;
    const u = (config.translate[0] + config.scale * qx) / config.width;
    const v = 1 - (config.translate[1] - config.scale * qy) / config.height;
    if (nz > 0 && insideHull(u, v, face.hull)) continue;
    indices.push(source[i], source[i + 1], source[i + 2]);
  }
  geometry.setIndex(indices);
}
