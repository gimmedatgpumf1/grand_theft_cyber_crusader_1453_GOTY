# Why the shoulders appear too narrow

## The source really is hugging-like

The registered source/rest assembly has both forearms pointing inward across the torso, with the middle-finger bone-tip proxy on the opposite side of the sternum's centroid in all four arms. The radius-end wrist proxy is 10–17 cm forward of the shoulder centre. Geometric upper-arm-to-forearm deflections are approximately 97°/132° male and 121°/100° female, left/right. These fitted joints and tips are approximations, but they directly establish a folded, hugging-like mesh pose. They **do not prove the person was hugging an object**: [OpenSkeleton](https://zenodo.org/records/22833546) states that upper-extremity structures were repositioned and individual structures registered, so the assembly is not documented as the original living or CT posture.

The actual room T-pose is different: its humeri and forearms point sideways, with shaft-proxy deflections about 2–4°. The old T-pose code moves the arms but does **not** properly reconstruct the sternoclavicular/acromioclavicular/scapulothoracic pose to accompany them. So straight arms are not evidence of a correctly straightened shoulder girdle. In a [study of 30 healthy volunteers](https://pmc.ncbi.nlm.nih.gov/articles/PMC2759875/), moving the arm from rest to 90° in the scapular plane involved average scapula-relative-to-clavicle changes of 14.6° upward rotation, 6.7° posterior tilt and 4.3° internal rotation. That confirms the girdle participates; it is not a validated correction to apply directly to our unstandardized source pose or to coronal-plane elevation. `HUGGING_POSE_AUDIT.tsv` records all four sides in both actual poses, including direction and tip position; see `42-audit-hugging-pose.py`.

## The width discrepancy, and what is known

In the actual room T-pose, the full leftmost-to-rightmost bony scapula X extent is **31.36 cm male** and **33.21 cm female**. The matching-sex NHANES height-based biacromial predictions are **40.09 cm** and **38.18 cm**, respectively. The arithmetic gaps are **8.73 cm** and **4.97 cm**, not 10 cm for each figure. NHANES measures skin over the acromial borders with a caliper, in a seated, arms-down posture; these bone extrema are not clinically identified acromia and the skeletal pose is different. This is a serious discrepancy in the current model/measurement comparison, but the gaps are not established clinical deviations or evidence of a condition. Soft tissue alone has not been shown to explain them.

The selected extreme scapula vertices are 3.50 cm apart in depth in the male, but measuring the 3D chord instead of X changes the span only from **31.36 to 31.61 cm**. This particular projection difference explains **0.25 cm**, not 8.73 cm. The corresponding female difference is about **0.03 cm**. Full-resolution registered versus simplified meshes differ in the old upper-scapula proxy by only about 0.1 cm; simplification is not a several-centimetre explanation. See `TPOSE_SCAPULA_EXTREMA.md` and `SHOULDER_POSE_AUDIT.md`.

An earlier **discarded experiment** rotated whole clavicle/scapula chains toward a 20° projected endpoint-slope target, preserving each bone's size and re-aligning the arms. It increased the actual T-pose scapula X extent from 31.36 to **35.24 cm male** and from 33.21 to **36.85 cm female**. That suggests joint pose can contribute several centimetres; it does **not** demonstrate that 20° is medically correct, identify the real acromia, establish the true living breadth, or explain the entire mismatch. The trial produced unacceptable ragdoll joint separation and hand/floor penetration, so it was not shipped. Its matrices are archived solely as `TPOSE_REJECTED_20_DEGREES.tsv.gz` for reproducibility, not used at runtime.

The later shipped attempt did not correct that problem: it merely equalized left/right clavicle slope about their mean, raising one side while lowering the other. Consequently it gained only **0.04/0.01 cm**. It was an inadequate interpretation of the requested straightening and has been reverted. The missing work is a defensible 3D shoulder-girdle reconstruction with identified joint/landmark frames, clear scapular motion against the thorax, and collision/ragdoll tests; a cosmetic target or a measured width alone cannot establish that pose.

## Reproduce

```sh
python3 tools/skeleton-source/openskeleton/42-audit-hugging-pose.py
python3 tests/hugging_pose_audit.py
```

The script builds the real room exporter and uses registered source geometry and runtime matrices; the test also checks the archived rejected experiment. NumPy, SciPy, C++17 and vendored Bullet are required. Neither the audit nor the archived experiment modifies the game pose.
