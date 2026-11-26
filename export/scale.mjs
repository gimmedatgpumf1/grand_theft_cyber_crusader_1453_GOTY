// Blue-skeleton target formulas. Height in metres; every result in metres.
// These are the male/female formulas expressed in one unit throughout.
export function comparisonTargetsM(sex, heightM) {
  if (!Number.isFinite(heightM) || heightM <= 0) throw new RangeError('heightM must be positive');
  const h = heightM;
  if (sex === 'male') return {
    head: 0.0561*h + 0.12179,
    neck: 0.0193*h + 0.074136,
    torso: 0.3536*h + 0.044647,
    arm: 0.4515*h + 0.014121,
    leg: 0.5553*h - 0.065708,
    hip: 0.076737*Math.exp(0.75*h),
    shoulder: 0.10719*Math.exp(0.77*h),
  };
  if (sex === 'female') return {
    head: 0.0568*h + 0.11832,
    neck: 0.0213*h + 0.071832,
    torso: 0.3446*h + 0.045758,
    arm: 0.4734*h - 0.013187,
    leg: 0.568*h - 0.069588,
    hip: 0.067179*Math.exp(0.89*h),
    shoulder: 0.1087*Math.exp(0.75*h),
  };
  throw new RangeError('sex must be male or female');
}
