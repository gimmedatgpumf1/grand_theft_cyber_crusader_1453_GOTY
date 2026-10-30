# Bone-size proportion report

- **Models:** OpenSkeleton N197925 (male) and N186416 (female), as shipped in the test room.
- **Scope:** lengths of the six major limb bones and the metacarpals, metatarsals and phalanges on both sides, plus an approximate pelvic breadth.
- **Date:** 30 October 2026.

## Bottom line

The measured arm-to-leg indices are broadly human-like in both models. However, **it would be misleading to certify either entire skeleton as medically normal** from these meshes alone. The clearest items worth checking are:

1. **Third toes, both models:** the third metatarsal is long relative to the third proximal phalanx compared with the published reference sample. This difference is especially pronounced in the female model. It is present in the included source meshes as well as the simplified game meshes; it is not solely an export artefact.
2. **Male femora:** the left mesh's maximum span is 12.2 mm longer than the right (12.0 mm in the source meshes). That is larger than the 9 mm 95th-percentile side-to-side femoral difference in one adult radiographic series, but the mesh and clinical measurements use different landmarks. This is a **review flag**, not proof of a real limb-length discrepancy or a source error.
3. **Upper arms:** the female humerus-to-femur and humerus-to-forearm ratios are above selected sex-specific reference means; the male humerus-to-forearm ratios are below them. Reference populations are small and methods differ, so a mean is not a clinical boundary.

The numerical tables below do not evaluate bone thickness, bone health, joint positions, alignment, visual pose, or bone proportions of the skull and spine. They cannot diagnose a patient or establish a universal range of normal anatomy.

## What was measured

The main numbers are measured directly from `skeleton-male.obj` and `skeleton-female.obj`, **not from a posed screenshot or collision hulls**. Each OBJ source-bone group was measured separately. The reported **mesh diameter** is the longest straight-line distance between any two vertices on that bone's convex hull, in millimetres. A farthest pair of mesh vertices must lie on the convex hull. A separate PCA-axis span is also supplied in the data file as a cross-check. Neither is guaranteed to reproduce an osteometric-board measurement, an anatomical joint-to-joint distance, or a radiographic landmark measurement.

The CSV includes both the shipped, simplified OBJ and the included unsimplified OpenSkeleton registered meshes, measured in the same way. The source is in millimetres; the OBJ is in metres and was converted to millimetres. No body-wide or individual bone rescaling was applied for this report. The importer changes axes and translates the whole subject, then simplifies each bone. In the 12 major long bones for each subject, game/source maximum-span differences are at most **1.1 mm male** and **1.7 mm female**. For the measured hand and foot bones, the largest such differences are **1.2 mm male** and **1.1 mm female**. These checks do not establish the accuracy of the underlying segmentation or registration.

These are registered shape-model structures derived from CT, not a verified reproduction of the patient's original assembled pose. Rigid T-posing or ragdoll rotation does not itself change an individual bone's dimensions, but the simplified V-view is not the CT bone surface. The recorded cadaver lengths in the repository documentation are **170.0 cm male** and **170.2 cm female**. They must not be conflated with the vertical spans of the registered or T-posed game models; we therefore do not use game-model height as a proxy for living stature.

## Major limb bone dimensions

Mesh diameters in **mm**, rounded to 0.1 mm. Left and right are the labels in the source dataset.

| Bone | Male L | Male R | Female L | Female R |
|---|---:|---:|---:|---:|
| Femur | 460.1 | 447.9 | 414.9 | 420.5 |
| Tibia | 392.8 | 392.6 | 359.0 | 358.8 |
| Fibula | 388.6 | 387.3 | 347.7 | 347.4 |
| Humerus | 312.1 | 316.6 | 314.5 | 318.3 |
| Radius | 257.6 | 258.5 | 222.9 | 224.6 |
| Ulna | 276.7 | 279.1 | 240.3 | 243.1 |

For context, Pietak et al. [1] measured adult dry bones with an osteometric board. Their Asian Indian samples had male/female mean femur lengths of **425.4 / 403.6 mm**, tibia lengths of **353.3 / 339.9 mm**, and humerus lengths of **307.0 / 286.6 mm**. Male *n* = 24, female *n* = 14. The sample was not selected as a universal sex-specific normal range, and its measurement method differs from ours. The authors also reported a separate, very small New Zealand European sample. Absolute length varies with body size and population; these means do not by themselves imply a problem with a larger or smaller bone.

## Ratios and reference comparisons

Each entry uses the mesh diameter for the stated bones on the **same side**. Reference values are published **means**, not cutoffs. Where a standard deviation (SD) is provided it describes the study's sample, not a validated normal range for these individuals.

| Ratio | Male L | Male R | Female L | Female R | Published comparison |
|---|---:|---:|---:|---:|---|
| Femur / tibia | 1.171 | 1.141 | 1.156 | 1.172 | Pietak [1]: male 1.21 ± 0.05 SD; female 1.19 ± 0.02 SD |
| Humerus / ulna | 1.128 | 1.134 | 1.309 | 1.310 | Pietak [1]: male 1.22 ± 0.09; female 1.20 ± 0.06 |
| Humerus / radius | 1.212 | 1.225 | 1.411 | 1.418 | Pietak [1]: male 1.30 ± 0.08; female 1.30 ± 0.06 |
| Radius / ulna | 0.931 | 0.926 | 0.927 | 0.924 | Descriptive; no matched individual-level reference range used |
| Fibula / tibia | 0.989 | 0.986 | 0.969 | 0.968 | Descriptive; no matched individual-level reference range used |
| Humerus / femur × 100 | 67.84 | 70.68 | 75.81 | 75.71 | Bogin and Varela-Silva [2]: male mean 71.4, female mean 69.8; no individual range reported there |
| (Humerus + radius) / (femur + tibia) × 100 | 66.79 | 68.42 | 69.44 | 69.66 | Bogin and Varela-Silva [2]: male mean 69.7, female mean 68.5; no individual range reported there |

The two lower-limb indices in [2] are called the humerofemoral and intermembral indices. They give useful context, not a test of every bone or of stature.

**Important femur/tibia caveat:** Aitken's larger radiographic series [3] reports a mean femur/tibia ratio of **1.28** and an observed **1.16–1.39** in 1,077 limbs. Its femur is measured from the top of the head to the medial femoral condyle; its tibia is measured from the medial tibial plateau to the tibial plafond. Our unconstrained farthest-vertex tibia can include the intercondylar eminence and medial malleolus. Pietak's tibial osteometric procedure also explicitly excludes the intercondylar eminence. **Our 1.14–1.17 mesh ratios cannot be classified as inside or outside Aitken's clinical range.** The difference in definitions could account for part of the difference; it has not been quantified here.

## Pelvis breadth: an approximate trunk comparison

In the registered pose, the transverse X span of both pelvic bones together is **273.6 mm male** and **292.1 mm female**; the corresponding unsimplified spans are **273.3 mm** and **292.0 mm**. Dividing by the average left/right femur mesh diameter gives a descriptive pelvis-to-femur ratio of **0.603 male** and **0.699 female**. This is the outermost projected mesh width, **not a clinically located iliac-crest measurement**. The relative assembly and pose of the two registered halves can influence it.

For context only, Gruss et al. [4] measured bi-iliac breadth on living adults with external calipers: **286 ± 24 mm in 12 males** and **269 ± 14 mm in 14 females**; the observed female sample range was 236–294 mm. Soft tissue and anatomical endpoint definitions make those numbers different from our projected bone-mesh extent. The female proxy is near the upper end of that small sample, not evidence of a problem. Pelvis shape and inlet dimensions have not been assessed.

## Hand and foot spot checks

Lengths are mesh diameters in mm. These are the **third digit**, not the entire hand or foot. Short bones are particularly sensitive to definition of endpoints, mesh simplification and small absolute errors.

| Third-digit bone | Male L | Male R | Female L | Female R |
|---|---:|---:|---:|---:|
| Hand metacarpal | 72.4 | 70.9 | 64.0 | 62.5 |
| Hand proximal phalanx | 44.8 | 47.6 | 44.4 | 44.1 |
| Hand middle phalanx | 30.5 | 31.3 | 25.9 | 26.0 |
| Hand distal phalanx | 16.9 | 19.3 | 15.1 | 13.6 |
| Foot metatarsal | 80.7 | 81.6 | 71.2 | 69.7 |
| Foot proximal phalanx | 27.2 | 25.2 | 19.6 | 20.2 |
| Foot middle phalanx | 12.9 | 10.9 | 9.3 | 9.5 |
| Foot distal phalanx | 12.9 | 14.0 | 12.7 | 11.2 |

| Third-digit ratio | Male L | Male R | Female L | Female R | Pietak et al. [1] reference means ± SD |
|---|---:|---:|---:|---:|---|
| Hand metacarpal / proximal phalanx | 1.616 | 1.490 | 1.440 | 1.416 | Male 1.50 ± 0.07; female 1.50 ± 0.07 |
| Hand middle / distal phalanx | 1.803 | 1.620 | 1.718 | 1.912 | Male 1.52 ± 0.13; female 1.58 ± 0.16 |
| Foot metatarsal / proximal phalanx | 2.972 | 3.237 | 3.628 | 3.447 | Male 2.57 ± 0.15; female 2.70 ± 0.16 |
| Foot middle / distal phalanx | 0.998 | 0.783 | 0.733 | 0.844 | Male 1.19 ± 0.22; female 1.11 ± 0.22 |

The short-bone reference groups were only **13 males and 9 females** from the Asian Indian subset. Their short bones were measured by caliper between proximal and distal endpoints, not as unrestricted farthest vertex pairs. Consequently, the apparent deviations are indications for further measurement, **not matched clinical z-scores or proof of abnormality**. For the female third toe, the same metatarsal/proximal ratios calculated from the unsimplified source meshes are approximately **3.67 left and 3.50 right**, close to the shipped values. This makes a pure simplification explanation unlikely, but does not separate subject anatomy, shape-model registration and endpoint-definition effects.

## Side-to-side differences

Absolute L–R mesh-diameter differences, in mm:

| Bone | Male | Female |
|---|---:|---:|
| Femur | 12.2 | 5.5 |
| Tibia | 0.3 | 0.2 |
| Fibula | 1.4 | 0.4 |
| Humerus | 4.5 | 3.8 |
| Radius | 0.9 | 1.7 |
| Ulna | 2.4 | 2.7 |

In Aitken's 324 paired adult limbs [3], 95% of the *radiographically measured* femoral and tibial differences were 9 mm or less. The male mesh femora merit a closer, landmark-based check. The table above measures each bone's longest vertex pair and is not an assessment of full leg length, hip/ankle position, or gait.

## Recommended next step

Before changing any meshes or scales, have the flagged femora and third toes measured with the same anatomical endpoints as the chosen reference on the original image data, preferably by a qualified anatomist or radiologist. In particular, compare the male femoral asymmetry and the short female third proximal toe phalanges against the underlying images. Those images and expert landmarks are not included here; the registered meshes alone cannot determine which finding is anatomical, a registration/segmentation effect, or a measurement-method difference.

## Reproducibility and files

- [All 176 bilateral limb measurements](BONE_LENGTH_MEASUREMENTS.csv): six major bones plus 19 hand and 19 foot bones on each side of each model. The first digit has no middle phalanx. Each row contains both OBJ and source-mesh diameters and PCA spans.
- [Pelvic-width measurements](PELVIS_WIDTH_MEASUREMENTS.csv), for the registered source and game models.
- Run `python3 tools/skeleton-source/openskeleton/15-measure-bone-lengths.py` from the repository root with NumPy and SciPy installed. It regenerates both CSV files without changing the skeletons. Measurements use convex-hull vertices and all source/OBJ vertices, not a visual estimate.
- OpenSkeleton data and attribution: [source documentation](openskeleton/README.md), [attribution](ATTRIBUTION.md), and [dataset record](https://doi.org/10.5281/zenodo.22833546).

## Medical and anthropometric references

1. Pietak A, Ma S, Beck CW, Stringer MD. *Fundamental ratios and logarithmic periodicity in human limb bones*. Journal of Anatomy. 2013;222:526–537. [doi:10.1111/joa.12041](https://doi.org/10.1111/joa.12041). Sex-specific values are from its Asian Indian subset; maximum long-bone lengths were measured with an osteometric board and third-digit bones with calipers.
2. Bogin B, Varela-Silva MI. *Leg Length, Body Proportion, and Health: A Review with a Note on Beauty*. International Journal of Environmental Research and Public Health. 2010;7:1047–1075. [doi:10.3390/ijerph7031047](https://doi.org/10.3390/ijerph7031047). Human sex-specific index means, not clinical thresholds.
3. Aitken SA. *Normative Values for Femoral Length, Tibial Length, and the Femorotibial Ratio in Adults Using Standing Full-Length Radiography*. Osteology. 2021;1:86–91. [doi:10.3390/osteology1020009](https://doi.org/10.3390/osteology1020009). Clinical radiographic endpoints differ from this report's vertex-pair measurements.
4. Gruss LT, Gruss R, Schmitt D. *Pelvic Breadth and Locomotor Kinematics in Human Evolution*. The Anatomical Record. 2017;300:739–751. [doi:10.1002/ar.23550](https://doi.org/10.1002/ar.23550). Living-subject caliper measurements; not a universal normative interval.
