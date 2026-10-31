# Rib projection, biological comparison and breathing

## Revert

The clavicle/scapula straightening from commit `ffabe7b` has been reverted, including its shoulder alignment, arm compensation, and ragdoll hinge change. The test checks **every bone matrix in both room T-poses** against the saved export from `d26c7b3`; the ordinary original/source pose is checked too. Bone geometry and ribs were not changed by this investigation. Historical shoulder and arm measurement tables have been restored to that pose.

## How far forward are the cages?

The first measurement is the overall front-to-back extent of the actual posed rib and sternum meshes, not the amount of breath movement. Model +Z points forward. The second uses an approximate thorax frame: take the centroid of the most posterior 1% of each rib's vertices; fit a plane through those 24 centroids and measure extent perpendicular to that plane. This helps account for thorax tilt. These are bounding extents across the entire cage, **not** a depth at one particular height; extrema may be on different bones or at different heights. They include bone, not skin or unossified cartilage.

| Skeleton | Global Z depth | Approx. thorax-frame depth | Approx. thorax-frame width | CT depth mean ± SD | CT width mean ± SD |
|---|---:|---:|---:|---:|---:|
| Male N197925, 170.0 cm | 20.81 cm | **21.31 cm** | 29.87 cm | 19.53 ± 1.75 cm | 30.28 ± 2.19 cm |
| Female N186416, 170.2 cm | 16.44 cm | **17.85 cm** | 27.39 cm | 17.77 ± 1.52 cm | 27.54 ± 1.98 cm |

These biological means are from [Robinson et al. (2024)](https://pmc.ncbi.nlm.nih.gov/articles/PMC11445027/), 1,225 men and 494 women aged 25–45, with mean heights 178 cm and 164 cm and mean weights 88.33 kg and 80.35 kg respectively. The female cadaver's recorded age is 41, height 170.2 cm and weight 78 kg; the male's age and weight are not established. Differences from the sex-specific CT mean are **+1.78 cm male** and **+0.09 cm female**, but these are **unmatched descriptive differences**, not a clinical z-score, population percentile, breath displacement, or proof of an abnormally expanded cage. The male is shorter than that sample's mean; body size and weight also affect depth.

The CT study used living hospital scans and its own posterior-point orientation normalization; the game meshes are independently registered post-mortem structures, with different segmentation and proxy landmarks. Their method omits part of the posterior rib head. Our fitted plane is an approximation, not a validated reproduction of their protocol. As a sensitivity check, using the single rearmost vertex or the rearmost 5% instead of 1% gives depths 21.32/21.33 cm male and 17.85/17.93 cm female. This tests only the proxy selection, not all segmentation and clinical errors. Global versus thorax-frame depth is notably different for the female, so orientation matters.

`RIB_CAGE_COMPARISON.tsv` records both restored room T-pose and registered-source pose. Their approximate thorax-frame depths are identical to numerical precision here: the T-pose re-levels the torso rigidly rather than inflating the ribs. This does **not** mean the original assembly reproduces the person's actual living posture.

## Forward reach of individual ribs

For each pair, select the closest vertices between each rib and its correspondingly numbered vertebra, as a rough posterior contact proxy. Compare the average position of the most anterior 5% of rib vertices with the vertebra-side point. The table gives the left/right mean projected on the game's global +Z; these are absolute bony reaches, **not excess forward movement**. The point selection and nearest contact are not verified joint or cartilage landmarks. Both sides and an approximate thorax-frame projection are available in `RIB_PROJECTION_AUDIT.tsv`.

| Rib | Male | Female |
|---|---:|---:|
| 1 | 5.06 cm | 2.87 cm |
| 2 | 8.36 cm | 5.95 cm |
| 3 | 11.80 cm | 9.63 cm |
| 4 | 13.84 cm | 10.06 cm |
| 5 | 15.09 cm | 11.97 cm |
| 6 | 14.86 cm | 12.28 cm |
| 7 | 14.24 cm | 13.16 cm |
| 8 | 12.13 cm | 11.94 cm |
| 9 | 10.52 cm | 10.19 cm |
| 10 | 7.14 cm | 7.18 cm |
| 11 | 1.91 cm | 4.22 cm |
| 12 | 0.12 cm | 0.37 cm |

Ribs 11–12 are short floating ribs; the 5%-vertex proxy may be a poor estimate of their anatomical tips. Ribs 1–7 normally connect to the sternum through cartilage, not necessarily visible in the bone meshes. Ribs 8–10 have indirect cartilage connections; the physics constraints in this repository are approximations.

## Could this be a deep breath?

In living subjects, inspiration can lift ribs and move the sternum forward. In a [quiet-breathing study of five seated subjects](https://journals.physiology.org/doi/full/10.1152/jappl.1997.83.5.1531), chest-wall **skin markers** moved on average 3–5 mm cranially/ventrally; this is not a bone or deep-breath measurement. In a [separate rib-motion study](https://journals.physiology.org/doi/abs/10.1152/jappl.1987.62.5.1872), rib 3 showed about 11° pump-handle and 13° bucket-handle rotation between functional residual capacity and total lung capacity; rib 7 showed about 7° and 10°. Those are paired changes between lung volumes, not expected static angles for these models.

**The single registered post-mortem assembly has no paired lung-volume scans or known respiratory phase.** Therefore we cannot tell whether the living person had been inhaling, whether retained lung volume or registration affected the assembly, or how much to move the ribs toward expiration. The male depth is above the cited cohort's mean by 1.78 cm on this approximate measurement; the female is near its mean. That pattern alone neither establishes nor rules out a deep-inspiration-like pose. No rib adjustment was made.

## Reproduce

```sh
python3 tools/skeleton-source/openskeleton/41-audit-rib-breathing.py
python3 tests/rib_breathing_audit.py
```

Requires NumPy, SciPy, C++17 and vendored Bullet. The first script builds the runtime pose exporter, verifies restored matrices against `TPOSE_BASELINE_d26c7b3.tsv.gz` (SHA-256 `e523557fa618536f6ce31c6a6b4173afcdb926e5bef81df43fe841d8e94e153b`), exports the registered-source pose and writes both TSVs. No clinical reconstruction, respiratory animation, or bone scaling is performed.
