# Bone-ratio TSV: definitions and sources

[`BONE_RATIOS.tsv`](BONE_RATIOS.tsv) is a UTF-8, tab-separated file with the six requested columns. It contains 188 data rows: each ratio for both recorded sexes and both source-labelled sides. `Bone name` specifies **numerator / denominator**. All ratios are dimensionless; `mean(a, b)` means `(a + b) / 2`, and `sum(phalanges N)` means the proximal, middle, and distal phalanges of digit N (the first digit has no middle phalanx). Each side uses bones from that same side and model.

**Important:** the measured values are ratios of **game-mesh PCA-axis spans**, not expert-validated osteometric measurements. The published studies use different methods and their samples are not known to match these two subjects' ancestry. Comparing the numbers does not establish abnormality and should not be used as an automatic per-bone correction target. In particular, PCA extrema can include the tibial eminence or other protrusions; short or irregular phalanges can have an unstable axis. The landmark-guided leg estimates in [the corrected report](BONE_PROPORTION_REPORT.md) use different definitions and consequently produce different femur/tibia ratios. None has yet been validated against the original CT.

Uniformly scaling all bones by the same factor leaves these ratios unchanged. Ratios can nevertheless vary between individuals and populations, with age, and with body proportions; applying a single reference mean to every height is not biologically guaranteed.

## Columns

- **Bone name:** ratio formula, including the denominator. Hand/foot digit numbers 1–5 run from thumb/great toe to little finger/toe.
- **Gender:** `male` or `female`, the OpenSkeleton source's sex label; it does not infer gender identity.
- **L/R:** source-labelled left or right; reference values below are not side-specific and are repeated for both sides.
- **Known biological ratio avg** and **std:** published mean of the individual ratios and its sample standard deviation, when found. An **empty cell means not established here**, not zero. These are sample statistics, not clinical limits. No third-digit reference is copied onto a different digit.
- **Measured ratio:** numerator and denominator calculated from the original, unrounded-to-report `game_pca_span_mm` values in [`BONE_LENGTH_MEASUREMENTS.csv`](BONE_LENGTH_MEASUREMENTS.csv), then rounded to four decimal places for export. Four decimals do not imply that level of anatomical accuracy. The game inputs were recorded to 0.0001 mm in the CSV; they themselves are geometric mesh estimates.

## Reference mapping

- **Pietak et al., 2013, Table 2:** all ratios for femur/tibia, femur/mean(tibia,fibula), humerus/radius, humerus/ulna, humerus/mean(radius,ulna), and the listed third-hand and third-foot digit ratios. The values are the study's Asian Indian sample: long bones 24 male and 14 female; third-digit bones 13 male and 9 female. Male and female mean/SD are reported separately. Long bones were manually measured with an osteometric board and short bones with calipers. Tibia length excluded the intercondylar eminence. The corresponding game measurements do not reproduce those endpoints. Source: [Pietak et al., *J Anat* 222:526–537, Table 2](https://pmc.ncbi.nlm.nih.gov/articles/PMC3633342/), [doi:10.1111/joa.12041](https://doi.org/10.1111/joa.12041).
- **Bogin and Varela-Silva, 2010, Table 1:** `Humerus / Femur` (humerofemoral index) and `sum(Humerus, Radius) / sum(Femur, Tibia)` (intermembral index). Their published mean indices are divided by 100 to produce dimensionless ratios. Their table gives no standard deviations, so the TSV cells are empty. Source: [doi:10.3390/ijerph7031047](https://doi.org/10.3390/ijerph7031047).
- Other rows are included to provide ratios for each hand and foot digit, radius/ulna, and fibula/tibia. Their reference cells are empty because a matching sex-specific published mean and standard deviation have **not** been established in this export. Absence of a value does not mean a biological reference cannot exist.

The cohort descriptions above refer to the **research samples**, not to the unknown ethnicity of the game skeleton subjects.

## Reproduce

Run `python3 tools/skeleton-source/openskeleton/17-export-bone-ratios.py` from the repository root. It uses Python's standard library and reads the precomputed measurement CSV. To regenerate the underlying CSV from both OBJ and registered source meshes, first run `python3 tools/skeleton-source/openskeleton/15-measure-bone-lengths.py` with NumPy and SciPy installed. Neither script alters game assets.
