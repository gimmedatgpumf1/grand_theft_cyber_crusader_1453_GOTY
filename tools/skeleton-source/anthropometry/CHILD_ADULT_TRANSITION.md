# Height estimates without fixed cutoffs

`BodyProportionCurves::blendedNhanesDimensionCm(sex, dimension, heightCm)` accepts positive finite heights without an imposed anthropometric range for six NHANES III surface measurements: sitting height, height-minus-sitting, upper arm, upper leg, shoulder breadth and pelvis breadth. The matching NHANES child and adult models blend over 160–175 cm for males, 145–160 cm for females, using the quintic smootherstep `w = 6t^5 - 15t^4 + 10t^3`. Both value and slope match at the ends. `height_minus_sitting` is always exactly height minus sitting height. Positive and finite input checks are retained to prevent meaningless/non-finite arithmetic; this is not a clinical boundary.

At 50 cm, illustrative neonatal anchors are smoothly connected to the child curve at 100 cm via cubic Hermite interpolation matching both values and endpoint slopes. These anchors are not measurements of one individual, not a conditional fit at exactly 50 cm, and do not identify age from height:

| Measurement | Reference used to construct 50 cm anchor |
|---|---|
| Upper arm | Chellappan 39-week subgroup median individual limb/length ratios: male 19.00%, female 19.08%. [Study](https://indianpediatrics.net/epub012024/OA-00580.pdf), see `NEWBORN_SEGMENT_RESEARCH.md`. |
| Upper leg | Abdel-Rahman pooled neonatal upper-leg mean 8.3 cm divided by pooled length mean 50.6 cm, an approximation, not a paired ratio mean. [Study](https://pmc.ncbi.nlm.nih.gov/articles/PMC5751918/). |
| Shoulder | Pooled mean biacromial 12.4 cm and mean length 50.0 cm in 161 neonates; ratio of group means, not individual mean ratio. [Study](https://pmc.ncbi.nlm.nih.gov/articles/PMC9743354/). |
| Pelvis | Pooled mean bi-iliac 7.74 cm and length 48.75 cm; ratio of group means. Population and endpoints may differ from NHANES bicristal breadth. [Study](https://link.springer.com/article/10.1007/BF02896728). |
| Sitting height | **Proxy only:** Aziz term-neonate crown–rump/crown–heel sex-specific group means, male 34.2/50.9, female 33.7/50.4. Crown–rump with flexed hips is not the same measurement as sitting height. [Study](https://www.theprofesional.com/index.php/tpmj/article/download/5238/3566/12766). |

Below 50 cm the anchor ratios continue proportionally. This is a design assumption, **not** a fetal/neonatal growth model. From 50–100 cm the bridge is a smooth interpolation between disparate samples; it is not validated on held-out infants. The neonatal reference cohort and child survey differ in population, age and measurement position. Short adult proportions cannot be inferred from stature alone. The same goes for the adult/child transition; height is not an age classifier.

For every source model, including adult models, `dimensionCm` and `selectedDimensionCm` now evaluate beyond observed knots. They use a slope-matched continuation of the dimension/height fraction in log-height space. This avoids the unbounded raw quadratic/log extrapolations previously found to produce negative lengths at 50 cm. The continuation agrees in value and slope at each end, keeps ordinary numerical results between zero and height, and passes through sparse interior gaps. It is a **design extrapolation**, not a medically fitted or validated formula for very short or tall people. Likewise, `NewbornLimbProfile` permits any positive length but its newborn ratios are not evidence for a 2 m adult. At extreme floating-point magnitudes calculation can still fail because finite precision is limited.

Example 2 m outputs (cm, approximate and **extrapolated**) from the blended NHANES estimator:

| Dimension | Male | Female |
|---|---:|---:|
| Sitting height | 100.50 | 98.85 |
| Height minus sitting | 99.50 | 101.15 |
| Upper arm surface | 42.93 | 42.08 |
| Upper leg surface | 49.99 | 49.93 |
| Shoulder breadth | 44.56 | 42.30 |
| Pelvis surface breadth | 31.47 | 30.89 |

`blendedNhanesScale` divides by the corresponding estimate at a fixed reference height. It produces continuous surface multipliers, not calibrated bone, mesh, joint or collider scales. Tests cover 50–100 and the child/adult joins, adjacent floating-point values at joins, every 52 selected model at heights 20, 50, 100, 200, 250, and 500 cm, and the exact sitting complement. Passing those checks does not prove anatomical accuracy.

**Not integrated:** the live skeleton renderer, joint anchors, collisions and ragdoll do not use these estimates; other body parts are not all covered by this blended profile. There is still no working one-height automatic skeleton scaler.
