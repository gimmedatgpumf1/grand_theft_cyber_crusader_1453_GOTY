# Skeleton measurements — corrected report

**30 October 2026.** This report supersedes the earlier mesh-diameter report. It measures the game meshes and the underlying registered OpenSkeleton surfaces; it does not change either skeleton or game behavior.

## Correction and bottom line

The earlier report treated farthest-vertex distances too much like anatomical measurements. **Its third-toe, male-femur and arm-ratio flags are withdrawn as claims of abnormal anatomy.** A mesh diameter is not automatically a clinical length. The new leg estimates use anatomical regions and a projected measurement instead; third-toe and upper-limb numbers remain explicitly geometric proxies. None has been checked by an anatomist against the original CT.

- **Neither skeleton can currently be certified normal or abnormal.** There is no diagnosis, matched clinical measurement, or original-image verification here.
- The estimated male femoral side difference persists in a different measurement: **12.4 mm in the game meshes** (12.9 mm in the source). This warrants verification, not a conclusion that the original person had a leg-length discrepancy.
- The female third proximal toe bones have game long-axis spans of **18.5 and 19.5 mm**, against **18.4–26.4 mm observed** in one female radiographic sample. These are different measurement techniques; similarity does not establish clinical normality. The earlier ratio-based alarm is not justified.
- Our best available leg ratio proxies are male **1.200 / 1.172** and female **1.187 / 1.204** (left / right). They are numerically within the observed **1.16–1.39** in Aitken's radiographic study, but **not measured by its method**; we cannot classify them against that interval.

## Known source-subject data

| Source subject | Sex recorded | Age | Recorded cadaver length | Weight | BMI | Race / ethnicity |
|---|---|---:|---:|---:|---:|---|
| N197925 | Male | Not available in this checkout | 170.0 cm | Not available | Not available | Not available |
| N186416 | Female | 41 years | 170.2 cm | 78.0 kg | 26.9 | Not available |

The values are transcribed in [OpenSkeleton README](openskeleton/README.md); the original source metadata file has not been independently rechecked for this report. Male age/weight and both subjects' race, specific ethnicity, diseases, injuries and other clinical history are **unknown**, not absent. Sex is the dataset label. Neither ancestry nor health can be determined from the mesh or the database's location.

[OpenSkeleton](https://doi.org/10.5281/zenodo.22833546) derives these registered structures from the New Mexico Decedent Image Database. Its public `Metadata.mat` is not in this repository. It may contain additional fields; this report cannot say which until the actual file is inspected. NMDID describes potentially available demographic and medical fields, but availability varies by case and full database access is controlled ([NMDID](http://nmdid.unm.edu/), [NIJ database description](https://nij.ojp.gov/library/publications/announcement-new-mexico-decedent-image-database)). Recorded cadaver length is not the height of the registered or posed game skeleton, nor necessarily living standing stature.

## How the revised estimates work

The input is the actual simplified `skeleton-{male,female}.obj` and the corresponding unsimplified registered source structures, in millimetres. Source coordinates were rigidly rotated and translated into the game; there was no per-bone scaling. Registration, segmentation and simplification can still change local surface geometry. Registered bone placement is not guaranteed to reproduce the original patient's standing pose.

1. **Legs:** for each femur and tibia, a shaft axis is estimated by PCA. An approximate medial direction is derived from the previously fitted femoral epicondyles. The femoral head is located using the earlier atlas-guided sphere fit. A vertex at the top of that head and an estimated medial-condyle surface point define the femoral estimate. On the tibia, automatic medial-plateau and central distal-surface candidates approximate the tibial endpoints, deliberately avoiding the intercondylar eminence and medial-malleolus tip. Distances are projected onto each bone's estimated medial/shaft plane. These are **landmark-guided geometric approximations**, not a simulated calibrated standing radiograph: the precise medial articular centres, image-plane orientation, positioning and radiographic silhouettes have not been validated. The epicondyle/head fits themselves are not expert-validated.
2. The displayed game leg values use the nearest game-surface vertices to the source-selected endpoints, inverse-transformed into the source frame. This allows a concrete endpoint comparison but is **not an independent manual identification** on the game mesh. Nearest-vertex errors are in the CSV (up to 3.7 mm); their effect can partially cancel in a length.
3. **Toes and other bones:** a long-axis span is the difference between maximum and minimum projections onto a bone's PCA axis. This avoids measuring the full diagonal between arbitrary end corners, but is **not a landmark-to-landmark caliper or X-ray measurement**. In particular, a phalanx articular surface may be concave, so its true articular midpoint need not be the farthest vertex. PCA may be unstable for short or irregular bones. The third-toe endpoints should be visually reviewed in [LANDMARK_AUDIT.png](LANDMARK_AUDIT.png).
4. Femur and tibia endpoint search windows of 3, 5 and 8 mm were tried; the table uses 5 mm. Across those choices the source estimates varied by up to **2.6 mm** for a bone. This is **not a total error bound**: systematic errors from the estimated plane and landmark definitions, registration, and missing original CT can be larger.

The accompanying audit image shows the source surfaces, search results and measurement lines. Axes are local projections; points which overlap in a view may be separated in depth. The image makes the algorithm inspectable, but does not establish that the automatic endpoints are anatomically correct.

## Re-measured legs: landmark-guided estimates

Lengths in mm. Ratios use the unrounded values on the same side. Source values follow the slash. These should not be mixed with the PCA spans in the next table.

| | Male L | Male R | Female L | Female R |
|---|---:|---:|---:|---:|
| Femur, game / source | 457.0 / 457.9 | 444.6 / 445.0 | 412.2 / 412.8 | 418.4 / 417.9 |
| Tibia, game / source | 380.7 / 380.6 | 379.3 / 380.1 | 347.4 / 347.4 | 347.5 / 347.6 |
| Femur / tibia, game / source | 1.200 / 1.203 | 1.172 / 1.171 | 1.187 / 1.188 | 1.204 / 1.202 |

The earlier male **12.2 mm** figure was a game mesh-diameter difference. Its revised projected estimate is 12.4 mm, and the female difference is 6.2 mm. Aitken's 324 paired radiographs had 95% of femoral and tibial differences no greater than 9 mm [1], but its landmarks and standing imaging differ from our algorithm. We **cannot apply that threshold to these proxies** or decide whether a real asymmetry, image-registration effect, surface error or landmark error is responsible. Tibial proxy differences are 1.3 mm male and 0.1 mm female, not a measurement of total leg-length discrepancy.

Aitken's actual femoral measurement is **top of femoral head to centre of medial femoral condyle**, and the tibial measurement is **centre of medial tibial plateau to centre of tibial plafond**. The previous report's unqualified shorthand for those points was imprecise. Aitken studied 1,077 limbs of 753 patients, 61% male, and did not publish sex-specific ratio ranges in that report [1]. Its observed range is not a universal biological or diagnostic boundary.

## Other limb sizes and ratios: game long-axis spans

These are **PCA spans, not validated osteometric lengths**. They retain the source's left/right labels and describe the entire bone's projected surface extent, not joint-to-joint distances. Femur and tibia here intentionally include their extremities and consequently differ from the leg estimates above.

| Bone, mm | Male L | Male R | Female L | Female R |
|---|---:|---:|---:|---:|
| Humerus | 311.8 | 316.1 | 313.8 | 317.7 |
| Radius | 257.0 | 257.9 | 222.5 | 224.1 |
| Ulna | 276.5 | 278.7 | 240.1 | 243.0 |
| Fibula | 388.4 | 387.1 | 347.6 | 347.3 |
| Femur | 459.8 | 446.9 | 414.7 | 420.0 |
| Tibia | 392.1 | 391.7 | 358.3 | 357.2 |

| Same-side ratio, PCA spans | Male L | Male R | Female L | Female R |
|---|---:|---:|---:|---:|
| Humerus / ulna | 1.128 | 1.134 | 1.307 | 1.308 |
| Humerus / radius | 1.214 | 1.226 | 1.411 | 1.418 |
| Radius / ulna | 0.929 | 0.925 | 0.927 | 0.922 |

For anatomical definitions, the osteometric maximum humerus length is head to trochlea, and maximum ulna length is olecranon to styloid; PCA extrema do not guarantee those landmarks ([CoRA measurement guide](https://docs.coracore.org/en/v2.0.0/forensics-anthro-guide/measurements/)). Pietak et al. [2] report osteometric-board male/female H:U means 1.22 ± 0.09 / 1.20 ± 0.06, and H:R means 1.30 ± 0.08 / 1.30 ± 0.06 in a small Asian Indian sample. These are study means and standard deviations, not individual normal limits; our spans are not sufficiently matched to classify either subject. No ethnicity match is known.

## Third-toe audit

Long-axis spans, mm, and same-side ratios. These are not the centre-of-articular-surface measurements in the radiographic reference.

| | Male L | Male R | Female L | Female R |
|---|---:|---:|---:|---:|
| Third metatarsal, game / source | 80.6 / 80.1 | 81.6 / 81.3 | 71.1 / 71.1 | 69.4 / 69.4 |
| Third proximal phalanx, game / source | 26.4 / 25.8 | 24.6 / 24.5 | 18.5 / 18.7 | 19.5 / 19.2 |
| Metatarsal / proximal phalanx, game / source | 3.049 / 3.110 | 3.310 / 3.315 | 3.843 / 3.793 | 3.568 / 3.619 |

The numbers remain different from Pietak's small-sample dry-bone ratios, even after changing from diameter to axis span, but neither our endpoints nor reference population match adequately. They do **not** establish a toe problem. Patil et al. [3] studied radiographs from 33 healthy male and 27 healthy female volunteers, ages 21–37. Their third proximal phalanx means were male **24.6 ± 1.77 mm** (observed 20.62–28.67) and female **22.6 ± 1.51 mm** (18.37–26.38). They measured between the middle of articular surfaces on standardized radiographs, while our numbers span a PCA axis on registered meshes. Their individual observed ranges are **not universal limits**, and the female subject is older than that sample. Likewise, their third-metatarsal reference means were male **65.88 ± 4.41 mm**, female **60.92 ± 2.99 mm**; the difference in endpoint method and population prevents a direct classification.

## Files and verification

- [LEG_LANDMARK_ESTIMATES.csv](LEG_LANDMARK_ESTIMATES.csv): source and game endpoint estimates, search-window sensitivity, source-frame endpoint coordinates, and nearest-game-vertex errors.
- [TOE_AXIS_ESTIMATES.csv](TOE_AXIS_ESTIMATES.csv): source/game axis spans and source extrema for each third metatarsal and proximal phalanx.
- [LANDMARK_AUDIT.png](LANDMARK_AUDIT.png): all four sides and sexes, plotted for review.
- [BONE_LENGTH_MEASUREMENTS.csv](BONE_LENGTH_MEASUREMENTS.csv): the original 176 mesh diameters and PCA spans for major limb bones and each hand and foot digit. These are **geometric measurements only**, not newly validated clinical lengths. The old [pelvis proxy](PELVIS_WIDTH_MEASUREMENTS.csv) is also not a verified bi-iliac measurement.
- Reproduce the new files with `python3 tools/skeleton-source/openskeleton/16-measure-anatomical-proxies.py` (NumPy, SciPy, Matplotlib). The earlier geometry CSVs can be regenerated with `15-measure-bone-lengths.py`. No runtime assets are changed.
- For clinical-level answers, an anatomist/radiologist must mark the original CT or appropriately positioned radiographs using the exact reference definitions, repeat the measurements, and check model-to-image correspondence. Obtaining the original `Metadata.mat` is needed to check further source-demographic fields.

## References

1. Aitken SA. *Normative Values for Femoral Length, Tibial Length, and the Femorotibial Ratio in Adults Using Standing Full-Length Radiography*. Osteology. 2021;1:86–91. [doi:10.3390/osteology1020009](https://doi.org/10.3390/osteology1020009).
2. Pietak A, Ma S, Beck CW, Stringer MD. *Fundamental ratios and logarithmic periodicity in human limb bones*. J Anat. 2013;222:526–537. [doi:10.1111/joa.12041](https://doi.org/10.1111/joa.12041).
3. Patil S, Hanumantharaya GH, Desai SP, Nidoni M. *Radiological Biometric Study of Metatarsals and Phalanges*. J Clin Diagn Res. 2017;11:AC05–AC09. [doi:10.7860/JCDR/2017/27887.10589](https://doi.org/10.7860/JCDR/2017/27887.10589).
4. Wollmann-Reynolds JS et al. *Measurement Error in Osteometric, Photographic, and Virtual Methodologies to Quantify the Torsional Profile of the Lower Limb*. Am J Biol Anthropol. 2025;188:e70154. [doi:10.1002/ajpa.70154](https://doi.org/10.1002/ajpa.70154). Defines distinct virtual maximum and bicondylar femoral lengths and reports method and observer variation; it does not validate our automated landmarks.
