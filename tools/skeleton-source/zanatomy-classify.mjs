// Shared selection rules for the Z-Anatomy importer and its tests.
// Muscle markings use .i/.j or .el/.er/.ol/.or and .e1l/.o2r suffixes.
export const NOT_BONE = /\.(i|j|[eo]\d*[lr])$/;
export const NOT_ANATOMY = /^(Cross Section|Reference|Take a picture)/i;

// Ordered rules: the first one that matches decides where a bone is filed.
// Every rule is written against the Z-Anatomy vocabulary, which is a closed
// list of 160 names; anything unmatched is reported rather than guessed at.
const RULES = [
  [/tooth|incisor|canine|premolar|molar/i, () => 'teeth'],
  [/^mandible$/i, () => 'mandible'],
  [/^(frontal|parietal|occipital|temporal|sphenoid|ethmoid|inferior nasal concha|lacrimal|zygomatic|palatine) bone$|^(sinus of|anterior cells of ethmoid|middle cells of ethmoid|posterior cells of ethmoid)/i, () => 'skull'],
  // These source names do not follow the "... bone" convention.
  [/^(nasal bone|maxilla|vomer)$/i, () => 'skull'],
  [/^(malleus|incus|stapes)$/i, () => 'skull'],
  [/^(atlas \(c1\)|axis \(c2\)|vertebra c\d)$/i, () => 'spine_cervical'],
  [/^vertebra t\d+$/i, () => 'spine_thoracic'],
  [/^vertebra l\d$/i, () => 'spine_lumbar'],
  [/^(sacrum|coccyx)$/i, () => 'pelvis'],
  [/^(hip bone)$/i, () => 'pelvis'],
  [/costal cartilage|rib$/i, side => `ribs_${side}`],
  [/^(body of sternum|manubrium of sternum|xiphoid process)$/i, () => 'sternum'],
  [/^(clavicle)$/i, side => `clavicle_${side}`],
  [/^(scapula)$/i, side => `scapula_${side}`],
  [/^(humerus)$/i, side => `humerus_${side}`],
  [/^(radius|ulna)$/i, side => `forearm_${side}`],
  [/^(femur)$/i, side => `femur_${side}`],
  [/^(patella)$/i, side => `patella_${side}`],
  [/^(tibia|fibula)$/i, side => `lower_leg_${side}`],
  [/metacarpal|phalanx of .* finger of hand|^(capitate|hamate|lunate|pisiform|scaphoid|trapezium|trapezoid|triquetrum) bone$/i, side => `hand_${side}`],
  [/metatarsal|phalanx of .* finger of foot|sesamoid bones of foot|^(calcaneus|talus|navicular bone|cuboid bone|intermediate cuneiform bone|lateral cuneiform bone|medial cuneiform bone)$/i, side => `foot_${side}`],
  // Keep the hyoid bone; the separate nasal and laryngeal cartilages are not bone.
  [/^hyoid bone$/i, () => 'spine_cervical'],
];

export function classify(rawName) {
  const side = /\.l$/.test(rawName) ? 'l' : /\.r$/.test(rawName) ? 'r' : null;
  const base = rawName.replace(/\.(l|r)$/, '');
  for (const [pattern, resolve] of RULES) {
    if (pattern.test(base)) {
      const part = resolve(side);
      if (!part.endsWith('_null')) return { part, side, base };
    }
  }
  return { part: null, side, base };
}
