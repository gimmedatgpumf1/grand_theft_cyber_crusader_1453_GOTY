# Height, shoulder breadth and hip/pelvic width: formulas and limitations

The earlier width audit omitted the formulas even though we already had height-driven **surface** breadth evaluators. It should have stated them. Those estimates have not been applied to the live skeleton. We also now have a published CT-based **hip joint-centre** formula, but its input is clinical leg length, not height. No corresponding height-only glenohumeral-centre formula has been established here.

## 1. Existing height-to-surface-breadth equations

For stature `h` and output in **cm**, the selected adult NHANES III models, inside their observed knot intervals, are:

| Sex | Surface biacromial shoulder breadth | Surface bi-iliac pelvic breadth |
|---|---|---|
| Male | `37.2262637429 + 25.4583990806 ln(h / 150)` | `27.1207250566 (h / 150)^0.517196132636` |
| Female | `35.2319379928 (h / 150)^0.636437330159` | `28.2990833872 (h / 150)^0.306647681838` |

These are fits computed from paired NHANES III data, not equations published by NHANES. They are unweighted research estimates, not diagnostic norms. Training-only grouped cross-validation chose models from five families; their near-tied selection does not establish the functional form as biological truth. The untouched adult holdout MAE (cm) is 1.70/2.13 for male shoulder/pelvis and 1.54/2.76 for female shoulder/pelvis. See `BODY_PART_MODEL_SELECTION.tsv` and `BODY_PART_MODEL_CV.tsv` for sampling, counts and alternatives. Held-out MAE describes errors for these survey surface landmarks, not bone-centre widths or all populations.

The adult male formula's first/last retained knot is approximately 153.08–191.71 cm for both dimensions; the female shoulder knots are 137.95–176.65 cm and pelvis 137.93–176.65 cm. Outside each interval the code **does not** use the raw log/power formula: it extends the dimension/stature fraction smoothly in log-height. This is a plausible bounded design extrapolation, not medical validation. In particular, these adult equations must not be applied directly to infants.

The actual height-only surface evaluator is `BodyProportionCurves::blendedNhanesDimensionCm(sex, dimension, h)` in `native/client/body_proportion_curves.h`. Load `BODY_PART_CURVE_KNOTS.tsv` with `load` and `BODY_PART_SELECTED_MODELS.tsv` with `loadSelectedModels`; pass `"male"` or `"female"` and `"shoulder_breadth"` or `"pelvis_surface_breadth"`. Check the returned `optional`. Exact reproducible procedure:

1. At `h <= 50`, use `h * (12.4/50.0)` for shoulder or `h * (7.74/48.75)` for pelvis, for either sex. These are ratios of **different neonatal cohort means**, not sex-specific fitted curves or a fetal model.
2. For 50–100 cm, use the code's cubic Hermite bridge matching the neonatal value and slope at 50 and the selected child model's value and slope at 100. This bridge is a modelling assumption, not a fitted infant dataset.
3. From 100 cm up to the sex-specific lower transition height, use the child PCHIP fit and its knots in `BODY_PART_CURVE_KNOTS.tsv`; it is not a single straight-line formula. Between 160–175 cm male or 145–160 cm female, evaluate both child and adult models and blend `child*(1-w)+adult*w`, where `t=(h-low)/(high-low)` and `w=6t^5-15t^4+10t^3`. Above that range use the selected adult model, with the bounded extrapolation described above. Height is not a reliable age classifier; the transition is a design choice.
4. The evaluator accepts positive finite input without height cutoffs, but extrapolated numbers are not supported evidence for extreme heights. Numerical overflow may still return no value. See `CHILD_ADULT_TRANSITION.md` and `BODY_PART_CURVES.md` for all assumptions.

| Height cm | Sex formula | Biacromial cm | Bi-iliac cm | Caveat |
|---:|---|---:|---:|---|
| 50 | both | 12.40 | 7.94 | mixed neonatal anchors |
| 170.0 | male | 40.09 | 28.43 | male child/adult blend |
| 170.0 | female | 38.15 | 29.41 | adult model |
| 170.2 | male | 40.16 | 28.50 | male child/adult blend |
| 170.2 | female | 38.18 | 29.42 | adult model |
| 200 | male | 44.56 | 31.47 | extrapolation |
| 200 | female | 42.30 | 30.89 | extrapolation |

The 170/170.2 cm rows include the requested cross-sex application. Male source cadaver length is 170.0 cm; female is 170.2 cm. Since the male estimate is a blend, substituting 170 cm directly into the adult-only table yields a different answer. Bone-centre widths cannot be directly compared to these surface predictions. The original measured joint-centre spacings and bone mesh extents are in `SKELETON_GIRDLE_WIDTHS.md`.

## 2. An independent, approximate surface cross-check

[Ozaslan et al., Turkish adult sample](https://scispace.com/pdf/stature-estimation-from-bi-acromial-and-bi-iliocristal-268zugd067.pdf), n=216 men and 121 women aged 20–52 and 20–49, published surface breadth means, SDs and correlations, and regressions for **stature from breadth**. We do not invert those regressions. We reconstruct approximate ordinary-least-squares breadth-on-stature slopes from their rounded summaries: `slope = r * SD_breadth / SD_stature`; `breadth = mean_breadth + slope*(h - mean_stature)`. In cm:

| Sex | Shoulder (biacromial) | Pelvis (bi-iliocristal) |
|---|---|---|
| Male | `38.606 + 0.142531*(h-172.436)` | `28.892 + 0.080062*(h-172.436)` |
| Female | `34.926 + 0.088837*(h-161.863)` | `27.331 + 0.084258*(h-161.863)` |

These are **our summary-derived approximations**, not published forward regression coefficients; rounded correlations/SDs prevent exact recovery and study tables have internal inconsistencies. The paper's sample stature ranges were 154.6–197.5 cm for men and 146.2–184.8 cm for women. At 170.0 cm male estimates are 38.26/28.70 cm; at 170.2 cm female estimates are 35.67/28.03 cm. They differ from the NHANES-based estimates above; sources, populations, protocols and fitted models differ. Height alone has weak predictive power in that sample (reported breadth/stature correlations 0.42/0.21 male, 0.26/0.19 female). These are not skeletal measurements, validation of the models on the two game subjects, or estimates for children.

## 3. Published skeletal hip-centre formula using clinical leg length

[Hara et al. (2016), CT study](https://pmc.ncbi.nlm.nih.gov/articles/PMC5121588/), 157 cadavers in groups aged 5–11, 16–19 and 25–40, used the femoral head centre and pelvic frame whose origin is the midpoint of the anterior superior iliac spines (ASIS). They recommend the following shared-sex model; adding sex did not meaningfully improve it. `L` is **clinical leg length in mm**, ASIS to medial malleolus through the femoral medial epicondyle. All components below are mm from the ASIS midpoint; posterior and inferior are negative, and lateral is positive toward the side in question:

```
anterior = 11 - 0.063 * L
lateral  =  8 + 0.086 * L
superior = -9 - 0.078 * L
```

Under the explicit mirror-frame assumption, the transverse hip-centre spacing derived from the two sides is `16 + 0.086*(L_left + L_right)` mm; for `L_left=L_right=800 mm`, this is **153.6 mm (15.36 cm)**. This is an illustrative input, not a measurement of either game skeleton. If the lateral coordinate frame does not match game X, or other centre coordinates differ, this is not the 3D distance. The per-centre lateral leave-one-out MAE was 4.4 mm, and left-side MAE 4.6 mm; those are **not** an error bound for the summed bilateral spacing. This model is not sex-specific or based on stature, and its sample doesn't cover newborns, 12–15-year-olds or older adults. Height-only application would additionally require a separately validated height-to-clinical-leg-length model with matching endpoints; `height-minus-sitting`, femur+tibia, or skeletal height cannot be substituted without evidence.

`native/client/hip_joint_reference.h` implements these formulas, including the explicit bilateral lateral derivation, with finite/positive arithmetic checks. `tests/hip_joint_reference.cpp` verifies the published coefficients and units. It is a research evaluator and is **not wired into** rig construction, mesh scaling or physics. The two source skeletons lack verified ASIS and medial-malleolus landmarks/clinical leg lengths, so a same-input comparison cannot yet be made against their measured 16.68/17.32 cm X hip-centre separations. Automated bone-centre fits are also not clinically validated.

A functional movement-based study of 48 adults reported stature explains only 22% of functional hip-centre-distance variance, and adding sex did not improve its predictive ability ([Kramer & Sylvester 2023](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0284450)). Its supporting workbook exists, but the available extracted rendering rounds stature to whole metres and pelvic width to zero; it is unsuitable for a regression without the original precision. Functional rotation centres are not automatically equivalent to CT head centres.

## 4. Remaining gap

A sex-specific, height-driven **humeral joint-centre** distance is still not established. Acromial breadth is not that distance; shoulder posture moves the girdle. Neither these surface models nor the Hara formula currently auto-scale the game skeleton, joint positions, colliders or physics. Reliable subject-specific scaling requires matched landmarks and a validated bridge from the height input to the relevant skeletal endpoints. No normality diagnosis follows from this report.
