# Shoulder and hip-area widths of the existing skeletons

The limb-only comparison did not include shoulder or pelvic breadth. This separate audit records **both**, along with hip joint spacing, from the geometry and fitted joint centres the game actually loads. The values below are in the original registered source pose, before the test room applies its reference pose or ragdoll motion.

| Measurement (cm) | Male N197925 | Female N186416 |
|---|---:|---:|
| Shoulder joint centres, transverse X spacing | 28.53 | 30.68 |
| Shoulder joint centres, 3D point-to-point | 28.81 | 30.76 |
| Hip joint centres, transverse X spacing | 16.68 | 17.32 |
| Hip joint centres, 3D point-to-point | 16.70 | 17.35 |
| Pelvis bone mesh transverse X extent | 27.36 | 29.21 |

The joint values come from `skeleton-{male,female}.shoulders` (fitted humeral head centres) and `.hips` (fitted femoral head centres), which `SkeletonRoom::load` loads. The fits are automated geometric estimates and **not clinically validated**. Transverse means absolute left/right coordinate difference along the game's X axis; point-to-point includes the differences in Y and Z. The width of a posed or moving skeleton can differ as the shoulder girdles or pelvis rotate. The pelvis bone extent is the difference between the largest and smallest X coordinates of the two pelvis meshes; it is not a farthest-pair diagonal, but its extrema are **not verified iliac-crest landmarks**.

## How these relate to the height curves

The current blended NHANES surface evaluator gives the following *population curve estimates* at the recorded source cadaver lengths (male 170.0 cm, female 170.2 cm):

| Curve surface measurement (cm) | Male | Female |
|---|---:|---:|
| Shoulder breadth, biacromial | 40.09 | 38.18 |
| Pelvic breadth, bi-iliac | 28.43 | 29.42 |

**Do not subtract these values from the joint-centre widths to judge how well the skeletons fit the curves.** Humeral centres are inside the shoulder joints, not the acromion skin landmarks. Femoral centres likewise are inside the hip joints, not pelvic skin landmarks. The pelvis's X extent is a bone-mesh proxy and does not reproduce the skin-and-soft-tissue, pose, or anatomical endpoints of the bi-iliac measurement. The similar female pelvis numbers are not proof of a fit. Surface landmark measurements are not available from these isolated bone meshes. The source's recorded cadaver lengths are not necessarily living or posed in-game heights.

The width curves are sex-specific and height-driven, but **are not applied to either skeleton**; the two recorded meshes are not altered. Bone-centre locations and mesh widths alone cannot establish if either subject is average or abnormal. Correct same-method validation requires matching acromial/iliac landmarks from the original body CT or verified anatomical landmarks, skin thickness and a standard pose.

## Reproduce

Run `python3 tools/skeleton-source/openskeleton/30-measure-girdle-widths.py` from the repository root (standard library only). It reads the game OBJ and the game's `.shoulders`/`.hips` files and produces `SKELETON_GIRDLE_WIDTHS.tsv`. The NHANES surface outputs above are `BodyProportionCurves::blendedNhanesDimensionCm` with the knot and selected-model tables, at the cadaver length. See `CHILD_ADULT_TRANSITION.md` for the curve's modelling assumptions and `../BONE_PROPORTION_REPORT.md` for the source subjects and fit limitations.
