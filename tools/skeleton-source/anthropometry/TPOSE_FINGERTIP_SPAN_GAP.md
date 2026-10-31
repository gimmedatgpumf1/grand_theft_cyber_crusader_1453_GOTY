# Fingertip span minus both arms

Measured on the actual room T-pose. For each hand, find the most lateral vertex among all finger phalanges; these happen to be the third (middle) distal phalanx on all four hands. The **3D fingertip span** is the straight-line distance between these two points. Each **3D arm length** goes from the same side's most medial shoulder-end humerus vertex to that same fingertip. Subtract both arms from the span:

| Skeleton | Fingertip span | Left arm | Right arm | Span − both arms |
|---|---:|---:|---:|---:|
| Male | 173.57 cm | 75.04 cm | 75.20 cm | **23.34 cm** |
| Female | 166.77 cm | 70.56 cm | 70.05 cm | **26.16 cm** |

Numbers in the table are rounded independently; the subtraction uses unrounded values from `TPOSE_FINGERTIP_SPAN_GAP.tsv`. The original arm audit selected the end vertex in the direction of the finger shaft rather than the absolute lateral extreme; on the male right side this changes the chosen point and arm length by about 0.03 cm. Both methods use bone tips, not skin.

## What the result means

The subtracted arm measurements start at the **medial humeral-head surfaces**, inside the shoulder joints. Therefore the result is approximately the **gap between those inner humerus ends**, not the outer acromion-to-acromion shoulder breadth. It cannot be treated as biacromial breadth or tested directly against NHANES biological biacromial averages (male 40.45 ± 2.12 cm, female 37.94 ± 2.13 cm at 165–<175 cm stature). The skeletal endpoints differ from the biological endpoints, and the source pose/landmarks remain unvalidated.

Since the four points are not perfectly collinear, subtracting 3D lengths is not exactly the 3D point-to-point distance between the two humerus endpoints. For reference, the endpoint **3D** distances are 23.59 cm male and 26.22 cm female; their **horizontal X** spacings are 23.33 cm and 26.17 cm. Projecting the span and both arms onto X before subtraction equals the horizontal X spacing exactly. No extra adjustments for skin or posture are added.

## Reproduce

With NumPy, SciPy, a C++17 compiler and the repository's vendored Bullet:

```sh
python3 tools/skeleton-source/openskeleton/39-measure-fingertip-span.py
python3 tests/tpose_fingertip_span_gap.py
```

The tool loads the runtime `SkeletonRoom::restoreReferencePose()` matrices via `38-export-t-pose.cpp`. It records all four endpoint coordinates, finger-bone names, arm lengths, span, subtracted result and independent 3D/X endpoint spacings in the TSV. Vendored Bullet objects are built in ignored `build/` if needed.
