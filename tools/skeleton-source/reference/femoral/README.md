# Femoral reference data

`template.mat` and `template_areas.mat` are from the CT-based femoral atlas
published with Fischer et al., *A robust method for automatic identification
of femoral landmarks, axes, planes and bone coordinate systems using surface
models*, Scientific Reports 10, 20859 (2020), DOI: 10.1038/s41598-020-77479-z.
The authors' repository is https://github.com/RWTHmediTEC/FemoralCoordinateSystem,
pinned at commit `0319313365517ead3439c60d8ca22d30cbe4e6de`. The project is
licensed EUPL 1.2; see `LICENSE.txt`. The template's head-region labels select
an anatomically defined articular region; this project adapts the data, but
**does not implement or claim validation as their full A&A algorithm**.

SHA-256:
- template.mat: a24c08fd6aeac4b6a07cec015ccf1a94e101b65f2a8a80cb293f05a8d220c731
- template_areas.mat: 965e40774cf23a2aab7f00784b8b189eeb616dbd4b4159dadf7df659ed4ea712

## Comparison against expert annotations

The fitting script was also compared with the publicly available medical data
in Fischer's [VSDFullBodyBoneModels](https://github.com/MCM-Fischer/VSDFullBodyBoneModels),
commit `2674d76e247060301833b077f964832ad8dc84af`. The dataset contains
cadaver CT-derived meshes and manually labelled femoral landmarks. Those MAT
files are **CC BY-NC-SA 4.0**, and are not included here. The evaluation code
is `../evaluate_femoral_reference.py`; see its header for input layout.

For each of 19 femora with manual annotations, the comparison fits a sphere to
the six labelled femoral head points for each of five raters' four repetitions,
and compares the atlas-based fitted center to the mean of those 20 centers.
Mean, median and maximum Euclidean center differences were 0.607, 0.513 and
1.488 mm, respectively. This is a retrospective comparison on the same
published cohort associated with the atlas, **not** an independent clinical
validation, a measurement of accuracy on either OpenSkeleton subject, or proof
of usable hip articulation. Their meshes were manually prepared and differ
from the registered OpenSkeleton meshes, which have documented joint overlap.
