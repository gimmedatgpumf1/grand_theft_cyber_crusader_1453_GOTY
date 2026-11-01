# Shoulder girdle: implementation and limits

Both figures' sternoclavicular (SC) and acromioclavicular (AC) joints now have three bounded rotational coordinates, with a fourth fixed source-to-reference rotation. SC uses an outward clavicle chord for the third axis; AC uses approximate thorax-frame directions. Runtime shoulder and elbow registration compensates for the complete girdle rotation, rather than only the torso's Z rotation. Ragdolls use locked-translation, three-rotation Bullet constraints at the same estimated anchors, replacing the two old hinges. The independent coordinates are exposed through `referenceGirdleCoordinate` / `setReferenceGirdleCoordinate`; in the room, `z` controls the first, `i` the second and `o` the third coordinate. Shift reverses their direction. Original source restoration is unchanged.

**These are approximations, not validated clinical joints.** Pivots use nearest sampled mesh surfaces, not identified articular centres. The source structures were separately registered, and their original living positions are unknown. There is no scapulothoracic sliding/contact constraint or soft tissue. The numerical angular bounds are approximate, independent-coordinate simulation limits, not subject-specific medical ROM. Bullet orthogonalizes the third direction, so its rotational limits are only an approximation of the kinematic joint's generally non-orthogonal axes. Controls can move bones into each other; the static tests below do not establish clearance throughout their ROM. Anatomical validation requires identified SC/AC landmarks, reference-frame registration, coupled motion and collision/contact data.

The initial T-pose convention maps each clavicle's medial/lateral **5%-vertex centroid chord** to a 24° coronal slope and 14° posterior transverse slope in an upright torso. At AC a 4° outward Z rotation and 5° mirrored Y rotation are composed. These are geometric convention parameters chosen after checking these two meshes for surface intersections, **not clinical SC/AC angles or a measurement of the subjects**. While arm elevation involves coupled clavicular and scapular motion (e.g. [a 30-volunteer study](https://pmc.ncbi.nlm.nih.gov/articles/PMC2759875/)), its clinical reference frames and arms-down starting posture cannot be assumed for the source meshes. No bone is scaled.

## Actual measurements

Using the full leftmost-to-rightmost posed scapula mesh X extent, not fingertip subtraction:

| Skeleton | Previous room T-pose | New T-pose | Change | Height prediction | Biological sample mean ± SD |
|---|---:|---:|---:|---:|---:|
| Male, 170.0 cm | 31.36 cm | 37.61 cm | +6.25 cm | 40.09 cm | 40.45 ± 2.12 cm |
| Female, 170.2 cm | 33.21 cm | 36.31 cm | +3.10 cm | 38.18 cm | 37.94 ± 2.13 cm |

The prediction is a sex/height model of NHANES III **surface biacromial breadth**, and sample statistics are unweighted adults 165–<175 cm. Bony extrema are not identified acromial landmarks; NHANES used seated participants with arms down, whereas this pose has raised arms. These are unmatched geometric comparisons, not clinical deviations or proof of correct anatomy. Full precision, source endpoints and sample sizes are in `TPOSE_SCAPULA_EXTREMA.tsv`; the seven-dimension comparison is refreshed. The previous 20° experiment remains archived and unused. The measured arm-to-fingertip bone proxies remain approximately 75.10/70.30 cm on average; the shoulder correction moves their attachment without rescaling the arms.

## Verification and remaining physics limitations

* `tests/skeleton_shoulder_girdle.cpp` exercises all three coordinates in all eight SC/AC joints, their bounds, finite-input handling, fixed-anchor continuity, source restoration and multi-axis motion during the fall.
* `tests/shoulder_mesh_clearance.py` tests triangle-surface intersections of each clavicle and scapula against same-side ribs and sternum, plus AC and scapula/humerus pairs, in the initial T-pose. No intersections were detected. This requires NumPy, SciPy and `python-fcl`; it does not test containment, all bones, continuous motion, or clinical contact.
* The ragdoll has local additional iterations for girdle, finger, toe and patella constraints, a 4 mm hand collision margin, and a 0.3 kg minimum effective vertebral mass to reduce severe constraint mass ratios. These are simulation approximations, not measured tissue masses. The original finger projection and rib/cartilage connections remain.
* The regression checks joint gaps **every physics step**, hand/floor and cartilage displacement, source restoration and inter-figure contacts. In this deterministic five-second fall the worst SC/AC anchor separation was **9.4 mm**; the peak over all joints was approximately **28.8 mm** at C4/C3, still visibly substantial under impact. The male/female worst hand floor penetration was approximately **16.2/12.7 mm**. These limitations are not a fully correct or clinically validated ragdoll, despite the permissive regression thresholds. Solver convergence and shape/contact modeling need further work before claiming all joints and collisions are fixed.

Reproduce with `python tests/shoulder_mesh_clearance.py`, `tests/run_tests.sh` and the commands in `TPOSE_SCAPULA_EXTREMA.md`. Performance on a physical GPU and Windows/MSVC are not verified here.
