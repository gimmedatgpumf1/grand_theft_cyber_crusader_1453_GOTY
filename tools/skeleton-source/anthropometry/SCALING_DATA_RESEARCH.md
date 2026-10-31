# Additional data for scalable skeletons

The working checklist is [SCALING_DATA_CHECKLIST.md](SCALING_DATA_CHECKLIST.md). This research adds real pediatric head, torso, foot, pelvic/shoulder surface breadth and skeletal-length data; it does **not** change the skeleton runtime. Data are different people at different heights and/or ages, unless a source explicitly says otherwise. Age is not determined by height. Selected medical bone scans, historic ergonomic surveys and modern Army personnel are not interchangeable cohorts.

[PEDIATRIC_PAIRED_HEAD_RESEARCH.md](PEDIATRIC_PAIRED_HEAD_RESEARCH.md) adds published age/sex-group ratios of individually paired stature and vertex–chin height (not raw pairs or a clinical model). Additional measured hand, foot and vertebral-body references are in [SMALL_BONE_CT_XRAY_REFERENCES.md](SMALL_BONE_CT_XRAY_REFERENCES.md). [GOLDMAN_RESEARCH.md](GOLDMAN_RESEARCH.md) adds sex-specific, same-skeleton long-bone and pelvis ratios and bone-on-femur fits. Neither source is paired with actual stature.

## 1. Paired sitting height, standing height and other measurements

`NHANES_III_PAIRED_CM.csv`: **27,487** records from the CDC's 1988–1994 U.S. NHANES III examination with sex, age in months, height, sitting height where available, upper arm and leg, biacromial and biiliac surface breadth, head circumference (younger children), weight, measurement source flag and survey design variables. Retained records have a valid standing height and examination age ≥2 years. Not every measurement was taken on every person. `subischial_cm = BMPHT - BMPSITHT`; it is a standing-height-minus-seat-height proxy, not a direct measurement of femur, tibia or a joint-to-joint leg length. Sitting height includes the head.

`NHANES_III_HEIGHT_BINS.tsv`: sex-specific 5-cm stature bins, all ages / under 18 / 18+, for sitting height, the subischial proxy, upper arm, upper leg, shoulder breadth, iliac surface breadth and head circumference. Each row gives `n`, mean age, measured dimension mean/SD and **mean of the individual ratios** to stature. It excludes nonzero `BMPHTFLG` values conservatively; the source calls this a **height source flag**, not necessarily a quality flag. The full paired CSV preserves all flags. The bins are **unweighted exploratory descriptive statistics**, not nationally representative estimates; use `WTPFEX6`, `SDPSTRA6` and `SDPPSU6` with complex-survey procedures for valid population estimates. The data are from a civilian noninstitutionalized sample, not a skeletal CT dataset. Age 90 is top-coded. The converter retains NHANES's digit-filled missing codes, so the extraction explicitly converts known 8- and 9-fill values to missing rather than treating 88888 as a height.

Selected sitting-height means:

| Sex and group | Standing height bin | n | Mean stature | Mean seated height | Mean seated/stature ratio |
|---|---:|---:|---:|---:|---:|
| Male under 18 | 100–<105 cm | 386 | 102.49 cm | 56.03 cm | 0.5468 |
| Female under 18 | 100–<105 cm | 396 | 102.62 cm | 55.68 cm | 0.5426 |
| Male under 18 | 125–<130 cm | 225 | 127.48 cm | 66.55 cm | 0.5221 |
| Female under 18 | 125–<130 cm | 227 | 127.45 cm | 66.34 cm | 0.5205 |
| Male 18+ | 170–<175 cm | 2,046 | 172.50 cm | 89.89 cm | 0.5211 |
| Female 18+ | 160–<165 cm | 2,263 | 162.35 cm | 85.19 cm | 0.5247 |

These are different people and different age mixtures. A head/torso-to-leg relationship need not vary monotonically with height at all ages. The original [NHANES III examination codebook](https://wwwn.cdc.gov/nchs/data/nhanes3/1a/exam-acc.pdf) specifies variables and survey weighting. Inputs were obtained via the [JSONL conversion of CDC's examination file](https://github.com/taw/nhanes-data/tree/db7d052969ea60af364b6524cb51fbadcc9cc2c8/data/nhanes3); source conversion is an additional processing step, so confirm any clinical application against the original CDC fixed-width file. See [CDC sitting-height reference work](https://doi.org/10.1016/j.dib.2020.106131) for weighted, age/ancestry-stratified reference charts.

## 2. Published pediatric DXA segment data

[Abrahamyan, Gazarian and Braillon, *Pediatric Radiology* (2008)](https://doi.org/10.1007/s00247-007-0720-x), [accessible full text](https://www.researchgate.net/publication/5655751_Estimation_of_stature_and_length_of_limb_segments_in_children_and_adolescents_from_whole-body_dual-energy_X-ray_absorptiometry_scans), studied 413 normally growing Caucasian children (170 boys 5.9–18.0 y, 243 girls 6.1–18.0 y) with DXA scans. The authors excluded musculoskeletal/hormonal pathologies. `PEDIATRIC_DXA_STUDY_SUMMARY.tsv` transcribes sex-specific age-group n, means and SDs of stature and four distances; `PEDIATRIC_DXA_POOLED_RATIOS.tsv` contains the published mean and SD of individual distance/stature ratios:

| DXA measurement / stature | Male mean ± SD | Female mean ± SD |
|---|---:|---:|
| Acromion–lateral epicondyle, labeled humerus by authors | 0.183 ± 0.008 | 0.183 ± 0.008 |
| Radial head–radial styloid | 0.152 ± 0.008 | 0.151 ± 0.008 |
| Greater-trochanter level–lateral epicondyle | 0.246 ± 0.011 | 0.248 ± 0.008 |
| Medial tibial condyle–medial malleolus | 0.232 ± 0.011 | 0.232 ± 0.010 |

**Crucial landmark distinction:** the authors' “humerus length” starts at the acromion, a scapular landmark, so it is **not** maximum humerus-bone length; the femur and tibia are likewise their specified projections rather than generic full-bone or joint-centre lengths. These pooled ratios describe the full sample, not independent sex/height regressions, and the standard deviations are for ratios, not predictive errors. Ratios times height are a transparent proportional approximation within the study's ages, not a fitted bone-on-height conditional model. DXA excluded children below ~6 years because of scan motion; apply cautiously elsewhere.

`PEDIATRIC_DXA_PUBLISHED_EQUATIONS.tsv` records 12 published sex-specific equations exactly as presented: `predicted_y = slope * predictor_x + intercept_cm`. For example, based on the study's definitions, predicted femur distance from tibia distance is male `1.012 × tibia + 1.729 cm` (R² 0.969), female `0.999 × tibia + 2.436 cm` (R² 0.934). The paper's stature-from-femur equations are included **only in that direction**; inverting them is not a valid estimate of expected femur given stature. Use only within the sample's age/size range and account for residual variability.

A newer study of [700 U.S. children](https://pubmed.ncbi.nlm.nih.gov/36727888/) notes historical age-based femoral and tibial standards can differ by 1–3 cm from more recent scans; one historical curve is not universally accurate.

## 3. Complete head height from age 2 through 19

A stronger primary source for crown-to-chin height than the previously identified unverified Korean-language file is the U.S. 1977 CPSC/NIST [AnthroKids study](https://math.nist.gov/~SRessler/anthrokids/), which defines head height as **menton to vertex, perpendicular to the Frankfort plane**, subject seated. `ANTHROKIDS_HEAD_HEIGHT_BY_AGE.tsv` transcribes the primary [male](https://math.nist.gov/~SRessler/anthrokids/data1977/175m.prn) and [female](https://math.nist.gov/~SRessler/anthrokids/data1977/175f.prn) summaries, including sample size, SD, percentiles and observed extrema. At 2.0–3.5 y mean head height is **17.6 cm male** (n=42) and **17.0 cm female** (n=31); at 17.5–19.0 y it is **22.1 cm male** (n=26) and **20.3 cm female** (n=19). These are between-group changes, not an individual's growth. Percentiles missing in the source are `NA`.

NIST explicitly makes [original 1977 individual records](https://math.nist.gov/~SRessler/anthrokids/orig77/individuals.csv) and their [record-oriented text](https://math.nist.gov/~SRessler/anthrokids/orig77/children.txt) available; the header includes stature, head height, age, sex, hand and finger measurements and many others. The primary [measurement procedures](https://math.nist.gov/~SRessler/anthrokids/data1977/descrip.htm) describe the landmarks. **This is a genuinely relevant paired source**, not the small classroom excerpt below. Its 87 measurements were taken in different measurement groups, so verify valid paired rows, zero/missing encodings, sex coding and units before fitting, and avoid the identifying/date fields. The current workspace has not ingested or verified the full primary individual file, because access to that host is restricted in this environment. Until that is done, the transcribed head-height rows are **age-group means**, not height-conditioned regressions. Comparing these head means to separately published stature means is not an individual ratio: the n differs.

## 4. Pediatric foot length / stature pairs

`ANTHROKIDS_FOOT_HEIGHT_BINS.tsv` summarizes stature/foot pairs by sex and 5-cm height from a [published classroom excerpt of the 1977 study](https://github.com/masteramyx/AnthroKidsProject/blob/41e86e1a7dcfc71e8792b4dd80a87ce5ae8e1ec0/anthrokids.csv). The excerpt contains 3,900 rows, ages 2.001–20.054 where given, rather than the complete set of NIST's 87 measurements. Columns used here are explicitly labeled stature, foot length, age and sex; the source documentation lists height and foot in cm. [NIST defines foot length](https://math.nist.gov/~SRessler/anthrokids/data1977/descrip.htm) as heel to longest toe with the right foot standing and weight distributed. The data are not foot-bone lengths. The linked excerpt also labels a field `forearmLength`; NIST calls the comparable measurement elbow-hand length, including the hand, and separately defines radiale-to-stylion. Do **not** assume that excerpt field is a radius length. Likewise its `upperLegLength` corresponds in magnitude to the NIST buttock–knee dimension, not directly to the femur; verify mapping before using it.

For heights 100–<105 cm, mean foot length is **16.18 cm** male (n=82) and **16.14 cm** female (n=78). At 155–<160 cm, it is **24.66 cm** male (n=97) and **23.44 cm** female (n=214); all groups here are under 18. These examples are not a longitudinal growth experiment, and are historic U.S. sample means, not individual predictions or universal limits. The bin file includes means, SDs and observed average individual ratios. The excerpt does not cover infants below age 2, and no recumbent-length data are mixed into these standing-height bins.

## 5. Promising direct bony pelvis and lower-leg data still to ingest

[Carman et al., *Scientific Reports* (2023)](https://www.nature.com/articles/s41598-023-38783-6) measured **333 pediatric CT scans, ages 4–18**, including pelvis (ASIS, PSIS, hip joint centre distances), femur and tibia/fibula linear measurements and angles, sex and height. The [SimTK project](https://simtk.org/frs/?group_id=2210) lists a 220 KB clinical measurement archive and a separate 137 MB statistical-shape-model archive. Unlike surface measurements this directly addresses bony landmarks, and the researchers' [2022 results](https://simtk.org/projects/paed_ssm) found age-specific shape modeling outperformed linear adult-mesh scaling on their dataset. The data archives have **not** been downloaded or checked for reuse terms in this environment; they are leads, not included data or implemented models. Refer to exact [CT definitions](https://www.nature.com/articles/s41598-023-38783-6): e.g. greater-trochanter–lateral-epicondyle femur and lateral-condyle–lateral-malleolus tibia distances.

## Reproduce

Install Python dependencies `numpy`, `pandas`, `zstandard`. Obtain the pinned NHANES III mirror's `data/nhanes3/exam.jsonl.zst` and optionally the exact AnthroKids classroom `anthrokids.csv` from the linked commits. SHA-256 checks in the script reject altered inputs. From repository root:

```
python tools/skeleton-source/openskeleton/20-child-growth-data.py --exam-zst PATH_TO_EXAM_JSONL_ZST --anthrokids-csv PATH_TO_ANTHROKIDS_CSV
```

The optional AnthroKids flag is required only for its foot bins. The DXA summaries are transcribed in the script from the cited paper. The separate NIST `ANTHROKIDS_HEAD_HEIGHT_BY_AGE.tsv` is a direct transcription of the two sex-specific NIST age tables, not generated from the inaccessible original individual file. All dimensions use cm in these outputs, except explicitly stated original variable units (age months, weight kg); radiographic and surface landmarks are not assumed equal.
