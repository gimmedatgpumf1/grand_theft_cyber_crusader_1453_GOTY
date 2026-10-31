# Seven dimensions: both skeletons, both sex formulas, and biological scatter

All values are centimetres. This is an exploratory audit, not a live skeleton-scaling implementation or a diagnosis. The sources' recorded cadaver lengths, 170.0 and 170.2 cm, are used as stature inputs, not verified living standing heights. Measurements below are **skeletal proxies**, whereas the formulas generally use surface landmarks; source pose, anatomical landmark and skin differences mean raw differences **are not matched biological deviations**. Left/right neck, arm and leg values are averaged. The complete unrounded audit, both formula sexes, group means, SDs and sample sizes are in `SEVEN_DIMENSION_COMPARISON.tsv`.

Each formula cell is `prediction (proxy minus prediction, % of prediction)`.

## Male skeleton, N197925, 170.0 cm

| Dimension | Skeletal proxy | Male formula | Female formula |
|---|---:|---:|---:|
| Head height | 21.19 | 22.96 (−7.67%) | 21.97 (−3.53%) |
| Neck length | 9.44 | 10.36 (−8.87%) | 11.10 (−14.91%) |
| Torso length | 65.42 | 65.30 (+0.17%) | 63.64 (+2.79%) |
| Full arm length | 72.67 | 76.06 (−4.46%) | 75.79 (−4.13%) |
| Full leg length | 87.14 | 86.67 (+0.55%) | 88.81 (−1.88%) |
| Hip width | 27.36 | 28.43 (−3.76%) | 29.41 (−6.96%) |
| Shoulder width | 31.38 | 40.09 (−21.73%) | 38.15 (−17.75%) |

## Female skeleton, N186416, 170.2 cm

| Dimension | Skeletal proxy | Male formula | Female formula |
|---|---:|---:|---:|
| Head height | 20.73 | 22.97 (−9.77%) | 21.98 (−5.71%) |
| Neck length | 11.41 | 10.38 (+9.99%) | 11.11 (+2.73%) |
| Torso length | 64.93 | 65.37 (−0.67%) | 63.71 (+1.92%) |
| Full arm length | 68.30 | 76.15 (−10.31%) | 75.89 (−10.00%) |
| Full leg length | 79.99 | 86.79 (−7.83%) | 88.92 (−10.05%) |
| Hip width | 29.21 | 28.50 (+2.51%) | 29.42 (−0.71%) |
| Shoulder width | 33.14 | 40.16 (−17.47%) | 38.18 (−13.20%) |

## Biological standard deviations

These are **sample standard deviations of observed values**, not regression coefficient errors and not z-scores for the skeletons. For ANSUR II and NHANES III we selected adults with measured stature **165 cm ≤ h < 175 cm**, near both subjects; the numbers are unweighted and reflect sample variation over that height band, not an SD at exactly 170 cm. Survey sampling weights/strata are not used. For head height, the separate clinical young-adult Yeo group has no available stature distribution and uses all heights; it is **not the sample used to fit the unverified height formula**.

| Dimension | Male sample mean ± SD | Female sample mean ± SD | Source; sample size male / female |
|---|---:|---:|---|
| Head height | 22.30 ± 2.70 | 21.20 ± 2.50 | [Yeo et al.](https://pubmed.ncbi.nlm.nih.gov/34292252/), Korean ages 20–24, all heights; 260 / 261 |
| Neck length | 10.41 ± 1.19 | 10.99 ± 1.20 | ANSUR II derived C7–tragion vertical; 1,706 / 651 |
| Torso length | 65.59 ± 2.49 | 63.23 ± 2.42 | ANSUR II cervicale height − crotch height; 1,706 / 651 |
| Full arm length | 76.49 ± 2.50 | 75.24 ± 2.58 | ANSUR II sleeve outseam + hand length; 1,706 / 651 |
| Full leg length | 87.19 ± 3.04 | 88.07 ± 2.86 | ANSUR II trochanterion to floor; 1,706 / 651 |
| Hip width | 29.10 ± 2.73 | 29.51 ± 3.62 | NHANES III bi-iliac surface breadth; 3,551 / 2,115 |
| Shoulder width | 40.45 ± 2.12 | 37.94 ± 2.13 | NHANES III biacromial surface breadth; 3,557 / 2,118 |

The width predictions are from the child/adult blended NHANES model at exactly each recorded height; the sample means above are from adults across a height interval. Therefore the predictions need not equal the sample means. ANSUR is a U.S. military sample, NHANES III is an unweighted U.S. survey subset, and Yeo is a different Korean sample. SDs must not be applied to the bone-versus-surface differences as though endpoints match.

## What the measurements actually mean

* **Head:** highest skull vertex minus lowest jaw vertex in the source pose. Prediction uses an **unverified third-party Korean height/head dataset**, not the Yeo clinical group and not a verified vertex–menton protocol. The Yeo SD above is a separate young-adult head-height reference, not SD of that formula.
* **Neck:** vertical posterior C7 to the midpoint between the two closest C1–skull surface points on each side, averaging sides. The reference instead ends at the surface ear tragion; C1–skull contact is not tragion. Estimated contacts and coordinates are in `SKELETON_NECK_SKULL_CONTACT.tsv`; see `WHOLE_BODY_HEIGHT_COMPARISON.md`.
* **Torso:** vertical posterior C7 to lowest pelvis mesh vertex; reference is standing surface cervicale to crotch. Different endpoints.
* **Arm:** mean of left/right geometric chains from fitted humeral-head centre to elbow, distal radius and middle-finger bone tip. The reference is acromion–stylion plus hand length; it assumes an extended arm, not the registered bent source pose.
* **Leg:** mean of left/right fitted hip-to-knee-to-fibula ankle chain plus vertical heel-bone distance. The reference is skin greater-trochanter landmark to floor, not hip joint to heel bone.
* **Hip:** transverse X extent of the two pelvis meshes, versus surface bi-iliac breadth. The extrema are not verified iliac-crest landmarks. Hip joint centre spacing, a distinct measurement, is 16.68 cm male and 17.32 cm female.
* **Shoulder:** transverse X separation of each upper-quarter scapula's lateral-most point, a geometric acromion-area proxy. The anatomical acromion itself and source pose are not validated. The reference is surface biacromial breadth. This substantial landmark/pose mismatch could contribute to the large apparent difference and cannot establish that the skeleton is abnormal. Shoulder joint-centre spacing, a different measurement, is 28.53 cm male and 30.68 cm female.

Full methods, formula provenance and extrapolation caveats: `WHOLE_BODY_HEIGHT_COMPARISON.md`, `GIRDLE_WIDTH_FORMULAS.md`, and `SKELETON_GIRDLE_WIDTHS.md`. Arm and leg formulas are research approximations; no joint, collision or physics scaling is implemented.

## Reproduce

From the repository root, with NumPy, SciPy and a C++17 compiler:

```sh
python3 tools/skeleton-source/openskeleton/35-seven-dimensions.py
python3 tests/seven_dimension_comparison.py
```
