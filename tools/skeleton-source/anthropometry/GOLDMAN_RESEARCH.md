# Paired osteometric bone ratios: Goldman dataset

This analysis adds **same-skeleton, same-side measured long-bone pairs**, not estimated from unrelated group means. It is useful for relationships between the bones in the existing skeletons, but **the dataset does not contain living stature**. A femur-length bin is a femur-length bin, not a human-height bin. These historical samples cannot establish what a 1.0 m or 2.5 m person looks like, or provide pediatric growth curves. Scaling has not been implemented in the game.

## Origin and what was measured

The [Goldman Osteometric Data Set](https://web.utk.edu/~auerbach/GOLD.htm) was assembled by Benjamin M. Auerbach; see Auerbach and Ruff, *Human body mass estimation*, 2004, and *Limb bone bilateral asymmetry*, 2006. It has osteometric measurements of 1,538 historical human skeletal records from many Holocene collections, most from preindustrial contexts. Provenience and date differ markedly; the samples are not a modern representative medical population. Sex and age in many records are **skeletal estimates**, not necessarily documented attributes of the deceased. The study owner requests citation when the data are used in research.

We analyzed the unchanged [`Goldman.csv` mirror](https://github.com/geanes/bioanth/blob/b179b396648dbe46bc3b9a7515ab1405958bbf43/inst/extdata/Goldman.csv) in the `bioanth` R package, whose [documentation](https://rdrr.io/github/geanes/bioanth/man/goldman.html) labels measurements as follows: HML humerus maximum length, RML radius maximum length, FML femur maximum length, TML tibia maximum length, and BIB bi-iliac breadth. All are mm. Source descriptions and definitions should be checked against the primary dataset's measurement protocol before drawing fine-grained anatomical conclusions; these are **not** necessarily centre-to-centre joint lengths, and tibial maximum length does not necessarily match Pietak's tibial endpoint. BIB is a pelvic breadth, not a long-bone length.

The mirror's [conversion script](https://github.com/geanes/bioanth/blob/b179b396648dbe46bc3b9a7515ab1405958bbf43/data-raw/goldman.R) maps `0` to male and `1` to female; we exclude ten uncertain `0?` / `1?` records, leaving 985 and 543 input records, respectively. Source age fields range from young adults (including `18–20`) to older adults, often as broad estimated intervals; this is not a child sample. Missing measurements are omitted **per analysis**, not imputed. Each row is a source record; no exact duplicates occur, but some collection/ID identifiers repeat and it is not established that all records are independent people. Do not combine left and right into an independent doubled sample. Location labels are broad reported locations, not verified ethnicity and not homogeneous time periods; some legacy characters may be mistranscribed in the mirror.

## Files and interpretation

- `GOLDMAN_PAIRED_RATIO_SUMMARY.tsv`: sex, side, ratio, paired-record n, mean and sample SD of **individual** ratios, plus empirical 5th, 50th, 95th percentiles. For example the column `femur / tibia` is mean(FML/TML), not mean(FML)/mean(TML). Reverse-ratio means are computed individually and are not obtained by inverting the published mean.
- `GOLDMAN_RATIOS_BY_FEMUR_BIN.tsv`: the same ratios in 25 mm observed same-side femur maximum-length bins. The n is the number with *all* variables and femur present. Small bins are noisy; use n to judge support. Conditioning on a ratio's denominator can cause a mathematical association even without a biological change; the bins describe the sample, not growth trajectories or trends caused by height.
- `GOLDMAN_RATIOS_BY_LOCATION.tsv`: geographic-label subsets with at least 10 pairs, *all* records (no review-flag exclusion); context/century/age mixture remain unresolved. A location is not an ethnicity.
- `GOLDMAN_BONE_ON_FEMUR_FITS.tsv`: sex/side-specific ordinary least squares in the **forward direction** `response_mm = intercept_mm + slope_mm_per_mm × observed_femur_mm`, with pair counts, observed support, R² and residual SD. These are cross-sectional bone-on-bone associations, not bone-on-stature equations; do not extrapolate beyond the recorded femur range or use across populations as a clinical rule. Residual SD is in mm and is not the SD of the ratio.

Every pooled or femur-binned analysis includes two transparent quality views. `all` uses all positive measured values. `review_flag_excluded` excludes a row for that analysis if either *relevant* long bone has both sides present but left/right maximum lengths differ by more than 20 mm. Femur-binned analyses additionally check the femur. This is a **heuristic data-review flag**, not a diagnostic or normality cutoff; cases with missing opposite sides cannot be flagged. The mirror includes some extreme differences (>90 mm) that require primary-source inspection. Both versions are retained rather than silently treating the flag as a medical fact. Source ages, sex estimates, endpoints and additional unflagged errors remain unvalidated.

## Examples from same-side, review-flag-excluded pairs

| Sex | Side | Measured ratio | Records | Mean individual ratio ± sample SD |
|---|---|---|---:|---:|
| Male | L | humerus / radius | 798 | 1.2995 ± 0.0509 |
| Female | L | humerus / radius | 428 | 1.3201 ± 0.0538 |
| Male | L | femur / tibia | 863 | 1.2096 ± 0.0403 |
| Female | L | femur / tibia | 464 | 1.2141 ± 0.0377 |
| Male | L | pelvis bi-iliac breadth / femur | 869 | 0.6052 ± 0.0403 |
| Female | L | pelvis bi-iliac breadth / femur | 481 | 0.6342 ± 0.0468 |

Within the male sample, left tibia/femur is 0.8281 ± 0.0268 for 400–<425 mm femurs (n=188) and 0.8243 ± 0.0254 for 450–<475 mm femurs (n=225). These averages are fairly similar in these bins, but cover different people and mixed historical locations, not stature or a longitudinal growth model. Female 375–<400 mm: 0.8221 ± 0.0269 (n=154); 425–<450 mm: 0.8302 ± 0.0209 (n=96). Variation among individuals remains substantial even if some bin means are similar.

A same-side female left tibia-on-femur fit from the QC view is `tibia_mm = −11.411 + 0.85268 × femur_mm`, n=464, femur 345–459 mm, R²=0.781, residual SD 10.18 mm. The male left fit is `tibia_mm = 13.616 + 0.79649 × femur_mm`, n=863, femur 360–531 mm, R²=0.776, residual SD 12.04 mm. These are not interchangeable with stature formulas, and fitted means do not predict an individual without uncertainty.

## Comparison with the current game meshes

[`BONE_RATIOS.tsv`](../BONE_RATIOS.tsv) retains the requested six-column report and its previously published Pietak references. The Goldman numbers above are additional independent population references, **not** replacements asserted to match the meshes. The game's measured ratios use PCA surface spans; these do not guarantee the source's osteometric landmarks, and neither skeleton's ethnicity is known. Do not infer pathology, a normality verdict, or corrective scale factors from a mismatch. No mesh or runtime files are modified by this analysis.

## Reproduce

Download the pinned `inst/extdata/Goldman.csv` from the GitHub mirror above, preserving bytes (SHA-256 `bc566cb3105330108ebae249bb778f8b4c888eb05b70b47cefeaa7aa1a87cb45`). With Python, pandas and NumPy installed, run:

```
python3 tools/skeleton-source/openskeleton/22-goldman-osteometry.py /path/to/Goldman.csv
```

The script verifies the input hash and writes only grouped statistics, not raw skeletal records. Source data may carry rights and reuse conditions beyond the GPL-licensed package code; consult the source owner before distributing or relying on the raw dataset. See [the continuing research checklist](SCALING_DATA_CHECKLIST.md).
