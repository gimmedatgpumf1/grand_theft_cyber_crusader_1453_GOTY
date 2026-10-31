# Continuous child–adult transition

`BodyProportionCurves::blendedNhanesDimensionCm(sex, dimension, heightCm)` supplies a **continuous surface-landmark estimate** across the overlap of the separately fitted NHANES III child and adult groups. The eligible dimensions are sitting height, height-minus-sitting, upper arm, upper leg, shoulder breadth and pelvis breadth. They use the same underlying NHANES III anatomical definitions on both sides; no measurements from differently defined datasets are silently mixed.

For each sex, the same height-dependent blend is applied to every eligible dimension:

```
t = (height - low) / (high - low)
w = 6t^5 - 15t^4 + 10t^3
value = child(height) + w * (adult(height) - child(height))
```

Below `low`, use the child curve; above `high`, use the adult curve. `low/high` are 160/175 cm for males and 145/160 cm for females, chosen inside the documented overlapping support for **all** eligible dimensions. At either endpoint, the weight is exactly 0 or 1, and its first and second derivatives are zero. Thus the blend joins the respective source curves without a jump in value or slope; PCHIP curves are continuously differentiable, though their second derivatives need not be continuous at their own knots. `height_minus_sitting` remains exactly `height - sitting_height`, even inside the transition.

`blendedNhanesScale` divides the target dimension by the same continuous dimension at a specified reference height. For a fixed, supported reference height, that multiplier is continuous and equals 1 at the reference. This is a mathematical surface-landmark multiplier, **not a calibrated bone, joint, mesh or collider multiplier**. Same-sex people of one height can be children or adults; a height-only changeover is a deliberate character-design assumption, not an age classifier or clinical inference. The endpoints are chosen for safe overlap, not as biological growth thresholds. Predictions also depend on the original cohort and its unweighted model fitting; smoothness does not establish accuracy.

For both sexes and six dimensions the test sweeps the full 15-cm blend at 0.001 cm (0.01 mm) height increments, checks monotonicity in these data, tests adjacent floating-point values across each endpoint, tests matching slopes, and checks exact sitting complement. Largest measured change per step was **0.00762 mm**; greatest difference between numerical left/right endpoint slopes (using 0.0001 cm) was **0.00000114 cm/cm**. These are numerical checks alongside the continuity guaranteed by the construction, not a promise that dimensions remain constant when height changes.

The curves still return no value beyond their actual support, including 50 cm and 250 cm. `NewbornLimbProfile` remains a separate newborn reference and is **not blended into these curves**: the source definitions and evidence do not establish a complete continuous infant-to-child fit. Hand, foot, forearm, head and other parts also do not yet have comparable child/adult NHANES pairs. The skeleton renderer, joint anchors, collisions and ragdoll have **not** been wired to any height-scale profile. Do not interpret the existence of this transition as a live height input or claim that all parts are covered.
