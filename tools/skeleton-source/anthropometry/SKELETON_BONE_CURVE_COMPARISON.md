# Existing skeletons compared with bone-on-stature curves

The game loads `skeleton-male.obj` and `skeleton-female.obj` directly. This audit **remeasured their vertices**, not a picture or a whole-body bounding box, then compared six bones per side to the existing sex-specific **SVAD bone-on-stature linear research fits**. These are bone curves, **not** the NHANES skin-landmark curves used by the blended surface evaluator. The bones do not contain the skin landmarks needed for a like-for-like comparison with that evaluator.

Statures used: the source metadata gives a **170.0 cm male** cadaver length and a **170.2 cm female** cadaver length. The female was 41; male age is unknown. A recorded cadaver length is not necessarily the upright game's actual posed height or the person's living standing height.

## Results

Values are cm; L/R retain the source labels. `difference = 100 × (mesh span / fitted length − 1)`. A positive value means the **geometric mesh span** is longer than the study curve's predicted measurement at the listed cadaver length. It does not mean the original patient was clinically abnormal.

The geometric measurement is each individual bone's span projected onto its own principal axis. This avoids measuring the diagonal between the farthest pair of vertices, but it still uses projected surface extrema and is **not** a validated anatomical endpoint measurement. Its endpoints do not necessarily reproduce the reference dataset's endpoints. Therefore the numerical differences are exploratory **comparisons**, not measurement errors or recommended correction factors.

### Male: 170.0 cm

| Bone | SVAD predicted cm | Game L / R cm | Difference L / R | Fit n |
|---|---:|---:|---:|---:|
| Femur | 45.75 | 45.98 / 44.69 | +0.5% / −2.3% | 71 |
| Tibia | 38.04 | 39.21 / 39.17 | +3.1% / +2.9% | 71 |
| Fibula | 37.51 | 38.84 / 38.71 | +3.6% / +3.2% | 71 |
| Humerus | 32.04 | 31.18 / 31.61 | −2.7% / −1.3% | 71 |
| Radius | 24.45 | 25.70 / 25.79 | +5.1% / +5.5% | 71 |
| Ulna | 26.32 | 27.65 / 27.87 | +5.1% / +5.9% | 69 |

### Female: 170.2 cm

| Bone | SVAD predicted cm | Game L / R cm | Difference L / R | Fit n |
|---|---:|---:|---:|---:|
| Femur | 45.07 | 41.47 / 42.00 | −8.0% / −6.8% | 48 |
| Tibia | 37.18 | 35.83 / 35.72 | −3.6% / −3.9% | 48 |
| Fibula | 36.42 | 34.76 / 34.73 | −4.6% / −4.6% | 48 |
| Humerus | 31.21 | 31.38 / 31.77 | +0.5% / +1.8% | 50 |
| Radius | 23.41 | 22.25 / 22.41 | −5.0% / −4.3% | 49 |
| Ulna | 25.23 | 24.01 / 24.30 | −4.8% / −3.7% | 50 |

The numerical differences here range from approximately **−8.0% to +5.9%**. The largest observed difference is the female left femoral **mesh span**, 41.47 cm, against 45.07 cm for the research curve. That is a reason to check the landmark definitions and original subject data, **not** proof the female skeleton is faulty. Sex, age, population, positioning, and landmarks are not matched well enough for such a conclusion.

## Independent endpoint check for legs

The earlier landmark-guided procedure was rerun on the source and game meshes. It finds candidate femoral-head and condyle points and tibial plateau/plafond points, projects into an estimated anatomical plane and matches the selected source points to game vertices. It gave the following values, in cm:

| | Male L / R | Female L / R |
|---|---:|---:|
| Femur | 45.70 / 44.46 | 41.22 / 41.84 |
| Tibia | 38.07 / 37.93 | 34.74 / 34.75 |

These differ from the axial spans above because the chosen endpoints differ, particularly at the tibia. They are still automated estimates, **not expert-validated landmarks**, and neither method exactly reproduces the SVAD protocol. The complete source/game point data and sensitivity to search windows are in `../LEG_LANDMARK_ESTIMATES.csv` and `../BONE_PROPORTION_REPORT.md`. The rerun reproduced the saved figures within 0.00006 mm; this tests reproducibility, not anatomical correctness.

## Reference limitations

The selected curve is a forward ordinary least-squares fit of CT-recorded bone length on stature, for recorded ages **18–<21**. Its input stature range covers both source cadaver lengths, but the reference ages do not match the 41-year-old female. The original forensic sample is not representative of healthy living adults, and stature mixes cadaveric and reported living lengths. Some bones may include epiphyses according to fusion status, which is not available per record in the extracted data. This is an exploratory fit and has not been externally validated. Residual SDs and ranges are in `SKELETON_BONE_CURVE_COMPARISON.tsv`; they must not be used as diagnostic thresholds because of the measurement and cohort mismatches. See `SVAD_BONE_STATURE_RESEARCH.md` for dataset provenance and definitions.

We did not establish comparable in-game skull, torso, pelvis, hand or foot dimensions against height-specific bone curves. We also did not measure live ragdoll articulation. Matching every existing mesh to the fitted **surface** curves, or identifying whether these two individuals are healthy, would require the original CT/skin and independently verified anatomical landmarks; it cannot be deduced from PCA spans.

## Reproduce

From the repository root:

```
python3 tools/skeleton-source/openskeleton/29-compare-skeleton-bone-curves.py
```

Requires NumPy. It writes `SKELETON_BONE_CURVE_COMPARISON.tsv` with 24 rows including fit n, height range, residual SD and unrounded-to-report differences. The femur/tibia endpoint audit is reproduced by `openskeleton/16-measure-anatomical-proxies.py` (NumPy, SciPy and Matplotlib). No game geometry or physics is changed.

Audited inputs SHA-256: male OBJ `6b6313d5f8514161e7c7a516505368963c98a11fdc00eb35f81640d8dfe88874`, female OBJ `68cb28f79c10bd310d4307486ac8502526a048b40e180622a4f05eb0fa51b510`, SVAD fit table `bf3d0d36733ba259787c5b6a9595bf16228d3b2eb959a5277d1973f92248ca87`.
