# What changes most as height changes?

## Direct answer

For the human samples below, **head width, front-to-back head length and ear-to-crown height change relatively little with standing height**; they are not literally fixed. **Leg-related vertical distances change much more**, and upper-arm and forearm lengths are close to proportional to stature. Seated height (which includes head and neck) changes less than stature. These are cross-sectional relationships between different people, not the amount by which the same person grows. Children and adults are explicitly included below, rather than discarding children's measurements.

There is a difference between **absolute size staying similar** and **the ratio to stature staying similar**. Head width is comparatively stable in cm, so head width/stature falls as stature increases. The upper arm grows roughly with stature, so its ratio changes less. A low R² means height explains little of the *individual* variation: it does **not** mean all heads are the same size.

## Comparable sex-specific changes: actual measured army sample

Fitted separately to all adult ANSUR II participants (4,081 males, 1,985 females). This table compares fitted means at **160 cm versus 175 cm**; stature itself increases **9.375%**. Both heights lie in each sex's observed range. The values at exactly 160/175 are OLS estimates from all paired records, not individual measured pairs or a longitudinal growth experiment. Adult female 175 cm lies near the upper tail, so it has fewer supporting observations. Values are cm, percentages are change relative to the fitted dimension at 160 cm.

| Surface measurement (not bone length) | Male, 160 → 175 | Male change | Female, 160 → 175 | Female change |
|---|---:|---:|---:|---:|
| Head breadth | 15.34 → 15.43 | +0.6% | 14.74 → 14.93 | +1.3% |
| Tragion to vertex, ear-to-crown | 12.84 → 13.10 | +2.1% | 12.58 → 12.92 | +2.6% |
| Head length, front to back | 19.36 → 19.93 | +2.9% | 18.86 → 19.48 | +3.3% |
| Sellion to menton, nose-root to chin | 11.80 → 12.25 | +3.7% | 11.23 → 11.65 | +3.8% |
| Biacromial shoulder breadth | 39.25 → 41.48 | +5.7% | 36.08 → 38.49 | +6.7% |
| Sitting height, vertex to seat | 85.49 → 91.58 | +7.1% | 84.54 → 90.44 | +7.0% |
| Foot length | 24.97 → 27.03 | +8.2% | 24.23 → 26.33 | +8.7% |
| Whole fingertip-to-fingertip arm span | 165.54 → 180.78 | +9.2% | 163.01 → 178.89 | +9.7% |
| Shoulder acromion to elbow radiale | 30.36 → 33.40 | +10.0% | 30.52 → 33.70 | +10.4% |
| Elbow radiale to wrist stylion | 24.28 → 26.69 | +9.9% | 23.67 → 26.12 | +10.3% |
| Standing crotch height | 75.50 → 84.23 | +11.6% | 76.55 → 85.41 | +11.6% |
| Height minus sitting height: subischial proxy | 74.51 → 83.42 | +12.0% | 75.46 → 84.56 | +12.1% |
| Tibiale height minus lateral-malleolus height | 34.91 → 39.35 | +12.7% | 36.65 → 41.12 | +12.2% |

Definitions matter: head length is front-to-back, **not** vertical head height; tragion-to-vertex is only part of the head; seated height is **not** torso alone; the tibial surface-height difference and subischial proxy are **not measured bones**. Arm measurements terminate at external landmarks and are not direct humerus/radius lengths. Relative percentage growth and R² do not quantify causation or the variation between individuals at fixed height. In the sample, head breadth's R² is approximately 0.006 male and 0.024 female; there remains real individual scatter and measurement error.

`ANSUR_ABSOLUTE_SIZE_CHANGE.tsv` contains the same 160-to-175 comparison for **96 measured or explicitly derived dimensions per sex**, including the dimensions above. `relative_change_percent` is `(pred175/pred160 - 1)*100`. `elasticity_at_167_5 = slope*167.5/pred167.5`: around 0 means near-static absolute size over this range; around 1 means near proportional; above 1 means greater percentage change than stature. It is a local summary of a straight line, not a physical law and not proof that a part is static everywhere. Variability from weight, muscularity, body composition, posture, age and population ancestry also affects results. Sizes in the table are predictions, not upper or lower limits for humans.

The fitted formula for every ANSUR row is `size_cm = intercept_cm + slope_cm_per_cm * stature_cm`. `ratio_at_160` and `ratio_at_175` are ratios of fitted values, **not** mean observed ratios. For actual paired measurements, individual ratios and unfit 5-cm-bin averages, see `ANSUR_II_PAIRED_MM.csv`, `PAIRED_RATIOS_VS_HEIGHT.tsv` and `ANSUR_HEIGHT_BINS.tsv`.

## Children and adults: height bins, not a rejection of shorter data

`NHANES_ALL_AGES_HEIGHT_BINS.tsv` bins the public 2017–2018 CDC examination measurements in 5-cm intervals for each sex and each of three populations: `all_ages`, `under_18`, and `18_plus`. It gives sample count, actual mean height, mean age, upper arm or upper leg mean, SD and the mean of each participant's dimension/height ratio. It is an **unweighted sample**; CDC population estimates require examination weights, strata and PSU from the paired CSV. There is no sitting-height or complete-head-height measurement in this cycle. Empty height/dimension combinations are absent, not zero. Upper-leg measurement begins at age 8, so there is no supporting upper-leg curve around 100 cm for most children.

Examples, measured group averages (not individual paired points or a same-person trajectory):

| Group | Sex | Sample | Mean stature | Mean age | Arm / stature mean | Mean arm length |
|---|---|---:|---:|---:|---:|---:|
| Child, 100–<105 cm | Male | 55 | 102.11 cm | 3.42 y | 0.2064 | 21.07 cm |
| Child, 100–<105 cm | Female | 54 | 102.47 cm | 3.69 y | 0.2048 | 20.99 cm |
| Adult, 170–<175 cm | Male | 622 | 172.45 cm | 49.91 y | 0.2247 | 38.75 cm |
| Adult, 160–<165 cm | Female | 704 | 162.36 cm | 47.52 y | 0.2245 | 36.45 cm |

Measured upper-leg examples: male children at 125–<130 cm (n=31) mean height 128.00 cm, leg 30.24 cm; male adults at 170–<175 cm (n=616) mean height 172.46 cm, leg 41.00 cm. Female children at 125–<130 cm (n=41) mean height 127.88 cm, leg 29.45 cm; female adults at 160–<165 cm (n=693) mean height 162.37 cm, leg 37.87 cm. The NHANES upper-arm length is acromion–olecranon and upper-leg length is inguinal crease–upper patella; neither is a bone length, and the arm endpoint differs from ANSUR's radiale measurement. These age-stratified comparisons are useful for fitting but cannot isolate height from age, population or body composition. [CDC protocols](https://wwwn.cdc.gov/Nchs/Data/Nhanes/Public/2017/DataFiles/BMX_J.htm).

## Actual crown-to-chin head-height data across ages

[**Yeo et al., Journal of Craniofacial Surgery, 2022**](https://pubmed.ncbi.nlm.nih.gov/34292252/), [accessible research preprint, tables 1–3](https://www.researchgate.net/publication/347918984_Anthropometric_Analysis_of_the_Growth_Proportions_of_the_Head_and_Face_in_Koreans/fulltext/5fee0fada6fdccdcb81e87e4/Anthropometric-Analysis-of-the-Growth-Proportions-of-the-Head-and-Face-in-Koreans.pdf), analyzed 1,255 Korean people in three sex/age groups using craniofacial landmarks. The linked preprint describes 3D scans and software; the published abstract describes standardized photographs, so verify the final methods before assuming equivalence to another protocol. `HEAD_AGE_STUDY_SUMMARY.tsv` transcribes **all 11 dimensions**, with sample sizes and mean ± SD, separately for both sexes and all three age groups. These are **group summaries**, not individual stature/head pairs; stature by age group is not in the accessible table, so do not fit them to invented stature values. Dimensions and measurement method need not be identical to the ANSUR calipers.

| Dimension | Male 8–10 → 20–24 y | Female 8–10 → 20–24 y | Male / female increase |
|---|---:|---:|---:|
| Entire head, vertex–menton | 194 → 223 mm | 190 → 212 mm | 14.9% / 11.6% |
| Upper head, vertex–sellion | 101 → 109 mm | 100 → 107 mm | 7.9% / 6.0% as published |
| Lower head, sellion–menton | 93 → 114 mm | 90 → 106 mm | 22.6% / 17.8% |
| Upper face, trichion–sellion | 71 → 74 mm | 70 → 70 mm | 4.2% / 0.0% |
| Midface, sellion–stomion | 59 → 72 mm | 56 → 67 mm | 22.0% / 19.6% |
| Lower face, stomion–menton | 34 → 42 mm | 34 → 39 mm | 23.5% / 14.7% |
| Front-to-back head length | 164 → 183 mm | 162 → 173 mm | 11.6% / 6.8% |

The cited paper compares age groups; it is not a study following the same individuals. Published percentages can differ slightly from arithmetic on rounded millimetre means. Upper-face dimensions and upper head grew less than lower head/face *in these groups*; even crown-to-chin head height was not constant. Separate craniofacial dimensions should therefore not all be assumed to scale identically. [Farkas et al. 1992](https://pubmed.ncbi.nlm.nih.gov/1643057/) report early growth of head width, length and circumference in 1,537 North American children; its reported ~11 cm “head height” is **not** the complete crown-to-chin height and should not be substituted for it.

## Additional exploratory paired vertical-head dataset

A [Korean-language GitHub dataset](https://github.com/learn-programmers/KDT_DATA_Analysis_1st/blob/92c72ff71e80a6d261fb8909c5179955a66fd29e/ML%20%EC%98%88%EC%B8%A1%20%EB%AA%A8%EB%8D%B8%20%EA%B5%AC%ED%98%84/2%ED%8C%80%201%EC%A1%B0/%E1%84%83%E1%85%A6%E1%84%8B%E1%85%B5%E1%84%90%E1%85%A5%26%E1%84%8C%E1%85%A5%E1%86%AB%E1%84%8E%E1%85%A5%E1%84%85%E1%85%B5/body_dataset.csv) provides 1,000 rows with stature `키`, head vertical length `머리수직길이`, sex `성별` and age `나이`, apparently cm. **Important: its original survey, collection methods, endpoints, selection procedure, location, consent and redistribution license are not documented in the repository.** It must not be described as the clinical Yeo cohort, Size Korea, or a representative population. It is a research lead, not verified clinical data. The published preprocessing notebook manually alters some measurements; these results use its earlier `body_dataset.csv`, not the modified `body_data2.csv`. Two male head values contain concatenated nonnumeric strings and are omitted; no guessed corrections or other outlier exclusions were applied. A numeric value of 29.0 cm may be an outlier. Raw data remain at the linked repository rather than being redistributed here.

Exploratory ordinary least-squares results, stature H and head Y in cm:

* Male: `Y = 9.519390 + 0.079034 H`, n=498, measured H 154.7–187.7 cm, residual SD 1.20 cm, R² 0.117.
* Female: `Y = 11.177137 + 0.063486 H`, n=500, measured H 142.3–172.6 cm, residual SD 0.95 cm, R² 0.112.

`EXPLORATORY_KOREAN_HEAD_FITS.tsv` and `EXPLORATORY_KOREAN_HEAD_BINS.tsv` contain reproducible summary estimates, observed ranges, residual scatter and 5-cm bins. The raw file uses CP949 and SHA-256 `fa7854cf9c5266ffbc54f235e382839cf9a8e5c1373564712994fbfa3bec7a5f`. These are useful leads and paired-data availability, **not** established crown-to-chin landmarks or a medically validated formula; seek the primary data dictionary before treating them as such. Compare with the correctly identified vertex-to-menton Yeo age summaries above, without pretending the samples are the same.

## From 100 to 250 cm

`ILLUSTRATIVE_MODEL_GRID_100_TO_250_CM.tsv` evaluates all 76 previously fitted equations from 100 to 250 cm in 5 cm increments; every row explicitly flags `outside_observed_range`. This gives a continuous **mathematical extrapolation**, not additional measurement data or a validated generic body across the range. There is no blending of children into these adult models. `nonpositive_prediction` flags invalid values; do not blindly clamp them and call them data. Available child bins show what actual ~1 m persons in the NHANES sample measured, and the earlier [251-cm clinical case](https://dergipark.org.tr/en/download/article-file/1076540) supplies a separate extreme male example with caveats in `README.md`. No evidence here establishes a universal 100–250 cm curve for either sex; the pediatric, adult and extreme-case patterns can legitimately differ. Pick the data source appropriate to the person being modeled or fit your own curve with the paired data.

## Reproduction

From the repository root with Python, pandas, NumPy and SciPy:

```
python tools/skeleton-source/openskeleton/19-height-variation.py
```

This regenerates the ANSUR comparison, NHANES bins, head-age study table and illustrative grid from committed paired data. To additionally regenerate the exploratory head summaries, download the exact third-party `body_dataset.csv` linked above and run:

```
python tools/skeleton-source/openskeleton/19-height-variation.py --exploratory-korean-csv PATH_TO_BODY_DATASET.csv
```

The original cohorts, units, measurement definitions, statistical limitations and formulas are documented in `README.md`. No runtime/game code is changed.
