# Leftmost-to-rightmost scapula, actual T-pose

Both measured points are selected from the **posed game meshes**, using the matrices from `SkeletonRoom::restoreReferencePose()`. The extreme X coordinate over **all vertices of both scapulae** is used on each side. No humerus or finger geometry is involved and there is no subtraction of arm lengths.

| Skeleton | Scapula width, X | Selected points' 3D distance | Biological shoulder mean ± SD | Height formula | Raw X difference |
|---|---:|---:|---:|---:|---:|
| Male, 170.0 cm | **31.36 cm** | 31.61 cm | 40.45 ± 2.12 cm | 40.09 cm | −8.73 cm (−21.78%) |
| Female, 170.2 cm | **33.21 cm** | 33.24 cm | 37.94 ± 2.13 cm | 38.18 cm | −4.97 cm (−13.02%) |

X is the left-to-right axis of the T-pose. The 3D distance also includes vertical and front-to-back separation: male 1.88 cm and 3.50 cm; female 0.83 cm and 1.16 cm, respectively. For unrounded results, actual selected vertices and which bone each belongs to, see `TPOSE_SCAPULA_EXTREMA.tsv`. The absolute XYZ coordinates use the unscaled model frame before the room floor placement.

The *geometric* extrema are directly measured; they are **not verified acromion landmarks**, and the assembled registered scapular pose has not been validated against the biological survey posture. The biological values are unweighted NHANES III measurements of acromion-to-acromion breadth for adults 165 cm ≤ height < 175 cm, n=3,557 male / 2,118 female; the formula estimates the biological measurement at each recorded cadaver length. The requested arithmetic comparisons are shown, but are **unmatched differences**, not clinical deviations or z-scores. The bone extremum is not automatically the exact lateral acromion and there is no skin in the skeleton. See `SHOULDER_POSE_AUDIT.md` for the earlier source-pose audit and its limitations.

## Reproduce

```sh
python3 tools/skeleton-source/openskeleton/40-measure-scapula-span.py
python3 tests/tpose_scapula_extrema.py
python3 tools/skeleton-source/openskeleton/35-seven-dimensions.py
```

Requires NumPy, SciPy, a C++17 compiler and the repository's vendored Bullet. The tool rebuilds the T-pose exporter and NHANES formula evaluator in a temporary directory. The seven-dimension comparison reads this TSV; regenerate it after mesh or rig changes.
