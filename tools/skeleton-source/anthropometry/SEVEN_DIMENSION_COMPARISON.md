# Seven dimensions: matched-sex formulas and biological scatter

All values are centimetres. This is an exploratory audit, not a live skeleton-scaling implementation or a diagnosis. The sources' recorded cadaver lengths, 170.0 and 170.2 cm, are used as stature inputs, not verified living standing heights. Measurements below are **skeletal proxies**, whereas the formulas generally use surface landmarks; source pose, anatomical landmark and skin differences mean raw differences **are not matched biological deviations**. Left/right neck, arm and leg values are averaged. The complete unrounded audit, both formula sexes, group means, SDs and sample sizes are in `SEVEN_DIMENSION_COMPARISON.tsv`.

## Matched-sex comparison

Each biological mean ± SD is an observed sample statistic, **not** the formula prediction. Formula inputs are 170.0 cm for the male and 170.2 cm for the female skeleton. Difference is skeletal proxy minus matching-sex prediction, divided by that prediction. Arm measurements run from the shoulder end of the humerus to the middle-finger bone tip in the actual T-pose; these endpoints do not match the biological reference. See `T_POSE_ARM_MEASUREMENTS.md`. Shoulder width below is the requested T-pose scapula X extent, not validated acromion breadth; see `TPOSE_SCAPULA_EXTREMA.md`. The separately requested fingertip-span-minus-arms calculation measures an internal humerus-end gap instead; see `TPOSE_FINGERTIP_SPAN_GAP.md`.

| Dimension | Male biological mean ± SD | Male formula | Male skeleton | Difference | Female biological mean ± SD | Female formula | Female skeleton | Difference |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Head height | 22.30 ± 2.70 | 22.96 | 21.19 | -7.67% | 21.20 ± 2.50 | 21.98 | 20.73 | -5.71% |
| Neck length | 10.41 ± 1.19 | 10.36 | 9.44 | -8.87% | 10.99 ± 1.20 | 11.11 | 11.41 | +2.73% |
| Torso length | 65.59 ± 2.49 | 65.30 | 65.42 | +0.17% | 63.23 ± 2.42 | 63.71 | 64.93 | +1.92% |
| Full arm length | 76.49 ± 2.50 | 76.06 | 75.10 | -1.26% | 75.24 ± 2.58 | 75.89 | 70.30 | -7.36% |
| Full leg length | 87.19 ± 3.04 | 86.67 | 87.14 | +0.55% | 88.07 ± 2.86 | 88.92 | 79.99 | -10.05% |
| Hip width | 29.10 ± 2.73 | 28.43 | 27.36 | -3.76% | 29.51 ± 3.62 | 29.42 | 29.21 | -0.71% |
| Shoulder width (scapula X extent) | 40.45 ± 2.12 | 40.09 | 31.40 | -21.68% | 37.94 ± 2.13 | 38.18 | 33.22 | -12.99% |

## Biological standard deviations

These are **sample standard deviations of observed values**, not regression coefficient errors and not z-scores for the skeletons. For ANSUR II and NHANES III we selected adults with measured stature **165 cm ≤ h < 175 cm**, near both subjects; the numbers are unweighted and reflect sample variation over that height band, not an SD at exactly 170 cm. Survey sampling weights/strata are not used. For head height, the separate clinical young-adult Yeo group has no available stature distribution and uses all heights; it is **not the sample used to fit the unverified height formula**.

Sources and sample counts (male / female):

* **Head:** [Yeo et al.](https://pubmed.ncbi.nlm.nih.gov/34292252/), Korean adults 20–24, all heights; 260 / 261.
* **Neck, torso, arm, leg:** ANSUR II, 1,706 / 651. Measures respectively are derived C7–tragion vertical, cervicale minus crotch height, sleeve outseam plus hand length, and trochanterion to floor.
* **Hip:** NHANES III bi-iliac breadth, 3,551 / 2,115.
* **Shoulder:** NHANES III biacromial breadth, 3,557 / 2,118.

The width predictions are from the child/adult blended NHANES model at exactly each recorded height; the sample means above are from adults across a height interval. Therefore the predictions need not equal the sample means. ANSUR is a U.S. military sample, NHANES III is an unweighted U.S. survey subset, and Yeo is a different Korean sample. SDs must not be applied to the bone-versus-surface differences as though endpoints match.

## What the measurements actually mean

* **Head:** highest skull vertex minus lowest jaw vertex in the source pose. Prediction uses an **unverified third-party Korean height/head dataset**, not the Yeo clinical group and not a verified vertex–menton protocol. The Yeo SD above is a separate young-adult head-height reference, not SD of that formula.
* **Neck:** vertical posterior C7 to the midpoint between the two closest C1–skull surface points on each side, averaging sides. The reference instead ends at the surface ear tragion; C1–skull contact is not tragion. Estimated contacts and coordinates are in `SKELETON_NECK_SKULL_CONTACT.tsv`; see `WHOLE_BODY_HEIGHT_COMPARISON.md`.
* **Torso:** vertical posterior C7 to lowest pelvis mesh vertex; reference is standing surface cervicale to crotch. Different endpoints.
* **Arm:** mean of straight-line left/right distances in the actual room T-pose, from the medial shoulder end of the humerus to the end of the middle-finger bone, including the whole hand. The ANSUR reference is acromion–stylion plus skin hand length in a straight-arm, arms-down posture. The acromion is a point on the scapula, not the humerus; the skin fingertip also differs from bone. These remain **unmatched raw comparisons**, not clinical deviations; see `T_POSE_ARM_MEASUREMENTS.md`. The old source-pose path and differences remain withdrawn; see `ARM_PATH_AUDIT.md`.
* **Leg:** mean of left/right fitted hip-to-knee-to-fibula ankle chain plus vertical heel-bone distance. The reference is skin greater-trochanter landmark to floor, not hip joint to heel bone.
* **Hip:** transverse X extent of the two pelvis meshes, versus surface bi-iliac breadth. The extrema are not verified iliac-crest landmarks. Hip joint centre spacing, a distinct measurement, is 16.68 cm male and 17.32 cm female.
* **Shoulder:** the requested width is the leftmost-to-rightmost **scapula X extent in the actual T-pose**. These directly measured extremes are not verified acromial landmarks. NHANES measures the acromial processes while seated, so the raw arithmetic differences are **not clinical deviations**. See `TPOSE_SCAPULA_EXTREMA.md` for point coordinates and 3D distances. Previous values from the original registered pose are superseded. Shoulder joint-centre spacing is a different measurement.

Full methods, formula provenance and extrapolation caveats: `WHOLE_BODY_HEIGHT_COMPARISON.md`, `GIRDLE_WIDTH_FORMULAS.md`, and `SKELETON_GIRDLE_WIDTHS.md`. Arm and leg formulas are research approximations; no joint, collision or physics scaling is implemented.

## Reproduce

From the repository root, with NumPy, SciPy and a C++17 compiler:

```sh
python3 tools/skeleton-source/openskeleton/38-measure-t-pose-arm.py
python3 tools/skeleton-source/openskeleton/40-measure-scapula-span.py
python3 tools/skeleton-source/openskeleton/35-seven-dimensions.py
python3 tests/seven_dimension_comparison.py
```
