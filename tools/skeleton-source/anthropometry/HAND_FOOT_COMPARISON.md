# Hands and feet: posed bone proxies versus surface measurements

Both skeletons are measured from their **actual room T-pose**, without scaling, using the 182 per-bone runtime transforms. Results are centimetres. These are bone-geometry proxies, not clinical measurements or diagnoses. The recorded cadaver lengths 170.0/170.2 cm are used for height predictions; living height is not verified.

| Dimension | Male L / R (mean) | Male height prediction | Male observed mean ± SD | Female L / R (mean) | Female height prediction | Female observed mean ± SD |
|---|---:|---:|---:|---:|---:|---:|
| Hand length | 18.82 / 17.89 (18.36) | 18.78 | 18.86 ± 0.78 | 16.97 / 16.36 (16.67) | 18.85 | 18.72 ± 0.87 |
| Hand breadth | 7.14 / 7.68 (7.41) | 8.66 | 8.69 ± 0.39 | 6.21 / 6.15 (6.18) | 8.02 | 7.99 ± 0.36 |
| Foot length | 24.26 / 24.58 (24.42) | 26.35 | 26.45 ± 0.99 | 20.58 / 21.48 (21.03) | 25.66 | 25.48 ± 0.96 |
| Foot breadth | 7.48 / 8.01 (7.74) | 9.99 | 10.02 ± 0.46 | 6.69 / 6.61 (6.65) | 9.50 | 9.47 ± 0.47 |

Height predictions are separate sex-specific ordinary least-squares fits to paired adult ANSUR II data (4,081 men and 1,985 women). Sample means and **sample** SDs come from the subset at 165–<175 cm stature (1,706 men, 651 women); they are unweighted and not conditional SDs at exactly the subject's height. ANSUR II is a U.S. military sample, not a universal clinical reference. The participants were living, the measurements are right-side skin-surface dimensions, and the posed bone figures are averaged left/right. See the [ANSUR measurement definitions](https://mreed.umtri.umich.edu/mreed/downloads/anthro/ansur/ADAS-Dimension_Definitions.pdf) and [foot measurement review](https://pmc.ncbi.nlm.nih.gov/articles/PMC4215017/).

## What was actually measured

* **Hand length:** the radius's distal 2%-PCA-end centroid transformed with the radius, to the distal middle-finger bone tip transformed with the finger. The wrist point is **not identified stylion**, and neither it nor the fingertip includes skin. ANSUR measures surface stylion to skin fingertip. Left/right differences can include mesh, wrist proxy or pose effects.
* **Hand breadth:** transverse distance between outer surfaces of the distal regions of the second and fifth metacarpals; the distal-most 10% of each metacarpal's vertices along its metacarpal-to-proximal-phalanx direction are used. These points are **not validated metacarpale-II/V landmarks**. ANSUR measures the skin at those landmarks, excluding the thumb.
* **Foot length:** distance projected horizontally along the heel-centroid to third-metatarsal-centroid axis, from the most posterior calcaneus point to the most anterior distal toe-bone point. In these poses the longest projected bone tip is second toe on both male feet and big toe on both female feet. This is not the surface heel or toe tip and not a weight-bearing measurement. At the selected extrema the actual 3D chord is 24.38/24.90 cm male and 21.04/22.04 cm female, versus the projected lengths above. These are not farthest-vertex bone diameters.
* **Foot breadth:** horizontal distance between outer surfaces of the distal 10% regions of first and fifth metatarsal meshes, perpendicular to the heel–third-metatarsal axis. These are **not verified metatarsale-tibiale/fibulare skin landmarks** or weight-bearing feet.

Each breadth is sensitive to the automatic cap definition. Varying the region from the distal 15% to distal 5% gives left/right-mean ranges: male hand **7.10–7.60 cm**, female hand **5.91–6.36 cm**; male foot **7.49–8.19 cm**, female foot **6.32–6.77 cm**. These are sensitivity ranges, not statistical confidence intervals or exhaustive measurement uncertainty. Neither skin thickness nor missing soft tissue has been estimated, and these ranges cannot be directly judged against a surface-data SD.

The female skeleton's projected bony foot length is **4.63 cm shorter** than its height-model surface prediction, the largest raw length gap here. The female hand proxy is **2.19 cm shorter**, and the male hand proxy is **0.42 cm shorter**. These unmatched differences warrant checking landmark identification, source registration, foot/toe posture and geometry; they **do not establish that a person had unusually short hands or feet**. In the T-pose, the first/second toe centroid-line proxies still slope about 13–23° from horizontal, so neither a weight-bearing posture nor perfectly straight toes has been established. The source structures were registered separately, and the soft-tissue contribution and possible geometry effects are unknown.

## Reproduce

`python3 tools/skeleton-source/openskeleton/43-measure-hands-feet.py` builds the real pose exporter and writes `HAND_FOOT_COMPARISON.tsv`; `python3 tests/hand_foot_comparison.py` checks the saved values against freshly generated room transforms and paired ANSUR records. NumPy, SciPy, a C++17 compiler and vendored Bullet are needed. The TSV includes L/R, means, cap sensitivity, subject-height fits and coefficients, source sample sizes, and unmatched raw differences.
