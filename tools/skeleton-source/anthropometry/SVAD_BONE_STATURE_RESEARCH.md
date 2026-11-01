# Paired bone and stature measurements across childhood

## Source and scope

[Chu & Stull, *International Journal of Legal Medicine* 139 (2025), doi:10.1007/s00414-024-03336-7](https://pmc.ncbi.nlm.nih.gov/articles/PMC11732921/) studied the U.S. [Subadult Virtual Anthropology Database (SVAD)](https://zenodo.org/records/5193208). This repository's new outputs are **our analyses of the original paired measurements**, not equations published by the study. The study is cross-sectional CT-based forensic research on deceased individuals from Albuquerque, New Mexico and Baltimore, Maryland, from birth through 20 years. The reported stature values mix **cadaveric lengths and living lengths reported by next-of-kin**, with no per-row provenance in the extracted file. They are not all measured standing heights. It is not a representative sample of healthy living U.S. children; the cohort includes forensic cases and has uneven ages and potentially selection and reporting biases. Clinical predictions and extrapolations outside the observed ranges are not justified.

Original paper: 990 records with stature (589 male and 401 female). The [author's exact research dataset](https://github.com/ElaineYChu/chu-and-stull_implications-subadult-stature/blob/7ef0c674502531fc6c4d3ab42db2b5d80b5d5a7a/data/US_0-21_FINAL.rds) contains 1,026 records overall. We independently identified **two exact duplicate rows**, both with stature, and counted each once: 1,024 unique source records, 988 with stature (588 male and 400 female). No individual identifiers or subject-level rows are exported here. There are gaps in each bone's measurements. Some groups have small samples. The source repository code is MIT licensed; consult the original data owners for any reuse or redistribution of patient-level data. The original article's [2026 correction](https://doi.org/10.1007/s00414-026-03732-1) concerns journal page numbering, not a stated correction of these data.

## What the bones mean

`FDL`, `TDL`, `FBDL`, `HDL`, `RDL`, `UDL` refer to femur, tibia, fibula, humerus, radius and ulna length. Other labels are midshaft or proximal/distal breadth. The paper states that **diaphyseal (ossified shaft) measurements are used in immature bones; when fusion was judged active, measurements include epiphyses as maximum lengths**. Thus a child shaft can be substantially shorter than the eventual complete bone; measurement endpoint changes are intertwined with developmental change. A row's fusion status, original side and side-selection procedure are **not given in this RDS**, so they must not be inferred from the `meas_type` column or from age alone. They are not guaranteed joint-centre distances or directly comparable to adult full-bone lengths, the game's PCA mesh spans, or DXA's different projected endpoints. Ratios are useful measurements of what the study recorded, but not automatically correct scale factors for a full modeled skeleton.

The project's aggregate data contain no skull/head, spine, pelvis, hand or foot bone measurements. Age and sex are recorded in this dataset; population codes are intentionally not decoded into ethnicities here. The two sources and their age distributions differ. Child and adult rows should not be pooled into a universal model; height alone does not determine age, developmental stage or body proportions.

## Files

- `SVAD_BONE_STATURE_BINS.tsv`: measured sex- and age-group-specific 5 cm and 10 cm stature bins, for all 18 available bone dimensions. For each dimension the `n`, mean stature, mean age, mean and sample SD of bone dimension, and **mean and SD of individual `bone_dimension / stature` ratios** are computed from the same available records. Dimensions and stature are cm after converting the original mm. Missing bone records are excluded independently. An empty SD means fewer than two paired values, not zero variation. Inspect n: many cells are extremely sparse. The age ranges overlap intentionally (`2_to_under_18` contains both narrower child ranges); do not add their counts.
- `SVAD_BONE_ON_STATURE_FITS.tsv`: descriptive ordinary least squares fit **bone dimension = intercept + slope × stature**, sex/age-specific, n ≥10, with observed stature range, mean age, R² and residual SD. These are newly computed forward fits (not inverted forensic stature equations). No external validation or prediction interval has been established. Broad age ranges mix shaft and fused measurements, populations, and nonlinear growth; a high overall R² does not show accuracy within height bins. Negative intercepts make extrapolation especially unsafe. Use only as exploratory summaries within observed ranges, not as clinical rules or production skeleton-scaling parameters.

## Examples: ratio changes, not individual growth

All examples use ages 2–<18, 10 cm height bins, and lengths as recorded under the fusion-dependent protocol:

| Sex | Stature bin | Bone | n | Mean bone length | Mean individual bone/stature ± SD |
|---|---|---|---:|---:|---:|
| Male | 100–<110 cm | femur | 26 | 21.98 cm | 0.2104 ± 0.0099 |
| Male | 170–<180 cm | femur | 52 | 46.02 cm | 0.2648 ± 0.0124 |
| Female | 100–<110 cm | femur | 16 | 23.28 cm | 0.2210 ± 0.0065 |
| Female | 160–<170 cm | femur | 32 | 42.74 cm | 0.2607 ± 0.0117 |
| Male | 100–<110 cm | radius | 26 | 12.16 cm | 0.1163 ± 0.0053 |
| Male | 170–<180 cm | radius | 45 | 25.00 cm | 0.1438 ± 0.0051 |

The early-to-late difference is **not solely biological**: immature measurements exclude unfused epiphyses, older measurements may include them, and the groups are different individuals. The dataset still directly demonstrates how the actual CT-recorded dimensions relate to stature in this sample. It cannot tell us whether an unchanged full bone should be scaled by these ratios. See the complete table for female radius, tibia, fibula, ulna, humerus and width data and uncertainty.

## Reproduction and quality checks

The pinned RDS SHA-256 is `a79fc3d3deee9a3dac2681101e98a73a68c2888c8f0db1b7b504488e77b11c9b`. Obtain it from the author repository at the pinned commit linked above, then install `numpy`, `pandas` and `pyreadr` and run:

```
python3 tools/skeleton-source/openskeleton/24-svad-stature-bones.py /path/to/US_0-21_FINAL.rds
```

The script rejects a different input hash, removes only exact duplicate rows, fails on conflicting duplicate identifiers, excludes missing/nonpositive measurements per pair, and keeps neither identifiers nor raw records. No runtime skeleton, joints or physics have been changed. See [SCALING_DATA_CHECKLIST.md](SCALING_DATA_CHECKLIST.md) for remaining gaps.
