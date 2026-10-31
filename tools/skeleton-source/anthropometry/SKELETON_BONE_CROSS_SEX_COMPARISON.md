# Bilateral average against both sex-specific bone curves

The two mesh-side measurements are averaged **before** comparison. For each skeleton and bone the male and female SVAD 18–<21-year bone-on-stature fits are evaluated at that skeleton's recorded cadaver length (male: 170.0 cm; female: 170.2 cm). Both heights fall inside the observed stature range of both formulae. `difference = (mean mesh span − predicted length) / predicted length × 100`; negative means the mesh proxy is shorter.

## Male skeleton, 170.0 cm

| Bone | Mean mesh span (cm) | Male curve (cm; difference) | Female curve (cm; difference) |
|---|---:|---:|---:|
| Femur | 45.34 | 45.75 (−0.9%) | 45.02 (+0.7%) |
| Tibia | 39.19 | 38.04 (+3.0%) | 37.14 (+5.5%) |
| Fibula | 38.78 | 37.51 (+3.4%) | 36.38 (+6.6%) |
| Humerus | 31.40 | 32.04 (−2.0%) | 31.19 (+0.7%) |
| Radius | 25.74 | 24.45 (+5.3%) | 23.39 (+10.1%) |
| Ulna | 27.76 | 26.32 (+5.5%) | 25.21 (+10.1%) |

## Female skeleton, 170.2 cm

| Bone | Mean mesh span (cm) | Male curve (cm; difference) | Female curve (cm; difference) |
|---|---:|---:|---:|
| Femur | 41.73 | 45.80 (−8.9%) | 45.07 (−7.4%) |
| Tibia | 35.77 | 38.08 (−6.1%) | 37.18 (−3.8%) |
| Fibula | 34.74 | 37.54 (−7.5%) | 36.42 (−4.6%) |
| Humerus | 31.58 | 32.07 (−1.5%) | 31.21 (+1.2%) |
| Radius | 22.33 | 24.47 (−8.8%) | 23.41 (−4.6%) |
| Ulna | 24.15 | 26.34 (−8.3%) | 25.23 (−4.3%) |

## Summary of these six comparisons

| Skeleton | Formula | Mean absolute difference | Mean absolute percentage difference |
|---|---|---:|---:|
| Male | Male | 1.03 cm | 3.34% |
| Male | Female | 1.64 cm | 5.61% |
| Female | Male | 2.33 cm | 6.84% |
| Female | Female | 1.49 cm | 4.31% |

The mean absolute percentage difference is the simple, equally weighted average of the six absolute percentages; cm is likewise an unweighted average of six absolute differences. It is descriptive for these two skeletons, not a validated model error, a population accuracy statistic, or a diagnostic measure. The same-sex curve is closer on this aggregate for each subject; the opposite-sex curve is closer for some individual bones. The difference in error is not evidence that either skeleton is faulty or that one sex formula should always be selected.

**Measurement caveats:** the mesh measurements are PCA-axis spans of the OBJ surfaces, not clinically validated anatomical landmark lengths. The SVAD CT values have different endpoints and potentially different epiphyseal coverage; the 18–<21 reference is not age-matched to the female subject, who was 41. Cadaver length is not necessarily posed skeleton height or living stature. The reference is a forensic cohort, not necessarily healthy or representative. These are **bone research curves**, not the separate NHANES surface-landmark curves. See `SKELETON_BONE_CURVE_COMPARISON.md` and `SVAD_BONE_STATURE_RESEARCH.md` for full methods, uncertainty and the separate landmark-based leg remeasurements. Averaging hides side differences; this does not change either mesh.

Reproduce with `python3 tools/skeleton-source/openskeleton/29-compare-skeleton-bone-curves.py` (NumPy). `SKELETON_BONE_CROSS_SEX_COMPARISON.tsv` has 24 rows with both predictions, differences, sample size and residual SD.
