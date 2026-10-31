# Shoulder-to-fingertip arm measurements in the actual T-pose

These are measured from the **actual T-pose used in the room**, not the original bent source pose. Each arm includes the entire hand to the middle finger's distal bone tip. The [full per-side values and coordinates](T_POSE_ARM_MEASUREMENTS.tsv) are reproducible.

| Skeleton | Left | Right | L/R average | Matching-sex height formula | Raw difference | Biological sample mean ± SD |
|---|---:|---:|---:|---:|---:|---:|
| Male, 170.0 cm | 75.04 cm | 75.16 cm | **75.10 cm** | 76.06 cm | −0.96 cm (−1.26%) | 76.49 ± 2.50 cm |
| Female, 170.2 cm | 70.56 cm | 70.05 cm | **70.30 cm** | 75.89 cm | −5.59 cm (−7.36%) | 75.24 ± 2.58 cm |

These are straight-line distances from the **shoulder end of the humerus** to the middle-finger bone tip. The difference is `bone proxy − formula`, divided by the formula for percentages. **It is not a matched clinical deviation**: the reference starts at the acromion on the scapula rather than the humerus; finger tips lack skin; and the biological formula uses a different posture and two surface measurements. The formula is the ANSUR II sex-specific adult height regression for sleeve outseam (acromion–stylion, arm straight down) plus hand length (stylion–middle-finger skin tip). The biological sample statistics are unweighted, separately observed adults 165 cm ≤ height < 175 cm, n=1,706 men and 651 women. They are not SDs of the skeleton differences. See `ARM_PATH_AUDIT.md` for the previous mismatched bone-centre path, which remains withdrawn as a biological comparison.

## Exact measurement

1. `38-export-t-pose.cpp` loads both actual room skeletons and calls `SkeletonRoom::restoreReferencePose()`. It exports `sourceMatrices({},0)` for every labelled bone. It neither reconstructs a pose in Python nor changes the game's skeletons; no scaling or floor/camera translation is applied.
2. Apply each source-structure matrix to the actual game OBJ vertices. Select the **most medial X vertex of the posed humerus**, at its shoulder end; verify it is near the fitted humeral head.
3. On the posed middle finger, take the distal phalanx vertex furthest in the direction from the middle-phalanx centroid toward the distal-phalanx centroid. This is an actual bone-tip vertex, not a surface/skin tip.
4. Measure the straight **3D** distance from humerus end to middle-finger bone tip; then average left and right. The 3D line is not a path through the elbow and does not omit the hand. The script separately records the former scapular-point measurement as a geometric alternative, not a verified acromion.

The room's humeral-head-to-elbow axes are horizontal to numerical precision, and the elbow-to-radius proxy axes have lateral X alignment greater than 0.997; digit axes have alignment above 0.97. The posed arms are straight enough for this direct measurement, although digit pose, bone placement and landmark selection remain geometric rather than clinically validated.

## Humerus end versus joint centre

The humerus-end measurement in the table is distinct from starting at the fitted centre of the humeral head: the latter gives left/right mean distances of 72.49 cm male and 68.04 cm female. The shoulder-end humerus vertex is a geometric bony endpoint, not an identified skin acromion, and must not be presented as directly matching the ANSUR shoulder reference. Left/right endpoint coordinates and values are in the TSV.

The previous shoulder-width audit found the registered scapulae are not verified as positioned in a standard anatomical posture. In this T-pose the selected right male scapular point lies only about 0.31 cm farther outward in X than the humeral-head centre, versus 2.50 cm on the left; the asymmetry is another reason the shoulder landmark needs validation, not proof of an anatomical abnormality.

## Reproduce

With NumPy, SciPy, a C++17 compiler and the repository's vendored Bullet sources:

```sh
python3 tools/skeleton-source/openskeleton/38-measure-t-pose-arm.py
python3 tests/t_pose_arm_measurement.py
python3 tools/skeleton-source/openskeleton/35-seven-dimensions.py
```

The first command builds the vendored Bullet library under the ignored `build/` directory if needed and compiles the small exporter in a temporary directory. The test independently checks both actual T-pose matrices, landmark selection, distances, bilateral averages and the reference formula. Run the measurement again whenever the rig, T-pose or OBJ changes; the seven-dimension generator reads the resulting TSV.
