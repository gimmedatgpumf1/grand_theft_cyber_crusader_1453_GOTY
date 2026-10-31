# Paired pediatric crown–chin and stature ratios

[Domljan, Iliev and Jurčević Lulić, 2024, *Applied Sciences*, 14:7185, doi:10.3390/app14167185](https://www.mdpi.com/2076-3417/14/16/7185), measured standing stature and head height in the **same children**. Head height is the vertical distance **vertex to gnathion (chin)**, not the ear-to-crown partial measure, skull-only height, or front-to-back head length. The Frankfort plane, upright posture and specified caliper measurement are described in Tables 3 and the methods. This is a surface measure including skin and hair, not a bone measurement.

The cross-sectional data comprise 1,307 children initially, 676 boys and 631 girls, aged approximately 2–16 years. Preschoolers were measured 2017–2020 in Zagreb (Croatia), Sofia (Bulgaria) and Skopje (North Macedonia); older schoolchildren were measured 2004–2005 in Zagreb only. The authors describe participants as European/Slavic, and study location and age are confounded. They report removal of statistical outliers before deriving ratios, including iterative group-level exclusions; resulting n varies. Such exclusion narrows the observed ranges and may exclude real atypical anatomy. The article itself describes this as design-oriented research, **not precise medical or clinical anthropometry**; its results are an exploratory supplementary source, not a universal clinical standard or game-art canon.

## Data

`PEDIATRIC_PAIRED_HEAD_BODY_RATIOS.tsv` transcribes the paper's Table 5: **mean and SD of the individual ratios `stature / head_height`**, by sex and age group, with sample n and published minimum/maximum. It has 26 rows, ages ≥2–<15. It is not the mean `head_height / stature`: **reciprocal of a mean is not the mean of reciprocals**, and SD cannot be inverted. No individual paired records, height-binned means or sex-specific absolute head-length means are supplied in this table. The authors say further details are in dissertations or available by request. The final 15–<16 age group had only two cases after exclusions and is omitted from the sex-specific Table 5; do not fabricate it.

| Age | Sex | n | Mean individual stature / head height ± SD |
|---|---|---:|---:|
| 3–<4 | Male | 64 | 5.76 ± 0.25 |
| 3–<4 | Female | 62 | 5.93 ± 0.28 |
| 10–<11 | Male | 30 | 6.61 ± 0.42 |
| 10–<11 | Female | 55 | 6.99 ± 0.50 |
| 14–<15 | Male | 38 | 7.58 ± 0.51 |
| 14–<15 | Female | 24 | 7.69 ± 0.53 |

These are **different cross-sectional age groups**, not observed individual growth, and samples of 9–10 two-year-olds and 22–55 participants in several older cells are especially small. Sex-specific age-group differences are not necessarily differences at a matched stature, and the paper does not supply conditional head-size-on-stature fits. The article also inconsistently describes some total outlier percentages and Table 5 values for age 2 in its narrative, so the TSV consistently reproduces Table 5 rather than mixing in narrative rounded values. These data do not establish universal proportions or justify extrapolation to 2.5 m.

## Status and reproduction

This fills a gap for **published paired ratio summaries by age and sex**, not for verified individual records. Finding individual crown–chin/stature pairs across ages and calculating sex-specific 5 cm stature bins remain open. The NIST 1977 data have a related menton–vertex definition; gnathion and menton are distinct landmarks, so do not merge them as identical. The skeletal runtime is unchanged.

Transcription: `python3 tools/skeleton-source/openskeleton/23-pediatric-head-paired-summary.py`. The article is CC BY 4.0; attribution: Domljan D, Iliev B, Jurčević Lulić T, 2024, DOI above. Values are transcribed without inventing or redistributing any individual data.
