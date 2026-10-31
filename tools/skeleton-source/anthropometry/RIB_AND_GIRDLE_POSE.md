# Girdle pose and rib projection audit

## What changed in the game

The room T-pose now balances the **left and right geometric clavicle slopes** about their mean. Each clavicle rotates around its existing sternoclavicular pivot, carrying its scapula and arm; the arms and elbows are then re-aligned to their existing horizontal T-pose. The acromioclavicular relative pose and the bone meshes are not flattened or scaled. The correction is capped at 5° on each side, and restoring the original pose still restores the registered mesh. This is a **symmetry convention**, not a clinically validated shoulder posture or measured clinical joint angle. It does not establish acromial breadth. The AC ragdoll hinge uses a higher positional error correction to help keep the girdle attached under load.

Angles below are the X–Y projection of the chord connecting centroids of the medial and lateral 5% of the posed clavicle vertices. After rotating a bone, the 5% selection can change, so the resulting values are approximately, not exactly, equal. They are not the same anatomical landmarks/axes as published SC angles.

| Skeleton | Left / right proxy before | Left / right proxy after | Scapula X span before → after |
|---|---:|---:|---:|
| Male | 36.32° / 42.58° | 39.65° / 39.29° | 31.36 → 31.40 cm |
| Female | 35.98° / 34.57° | 35.32° / 35.12° | 33.21 → 33.22 cm |

The wider goal of fully straightening the girdle is **not complete**: absolute clavicle/scapula alignment is not validated. For context, a [100-volunteer CT study](https://pubmed.ncbi.nlm.nih.gov/32933527/) reported standing SC elevation 7 ± 4°, but used defined bony landmarks and local thorax coordinates, with arms in a different posture; it cannot be substituted for these mesh-proxy angles. A larger experimental lowering (20° proxy target) produced unacceptable simulated joint gaps and floor penetration; it was discarded. Straightening by assuming that target would also risk first-rib contact. The present change is deliberately limited rather than claiming the experimental pose was safe.

## How far forward are the ribs?

For each rib, find the closest pair of vertices on that rib and the correspondingly numbered thoracic vertebra. This is a **contact-area proxy**, not an identified joint centre. Average the rib vertices in the most anterior 5% of global Z and subtract the vertebra-side contact vertex's Z; +Z faces forward. The table shows the mean of left and right, in centimetres. It is a geometric chord projection, **not displacement due to breathing**. Individual values, contact gaps, both poses and 3D coordinates' Z components are in `RIB_PROJECTION_AUDIT.tsv`. Vertex sampling and independent registration affect the result, and short floating ribs 11–12 do not end at the sternum.

| Rib | Male | Female |
|---|---:|---:|
| 1 | 5.06 | 2.87 |
| 2 | 8.36 | 5.95 |
| 3 | 11.80 | 9.63 |
| 4 | 13.84 | 10.06 |
| 5 | 15.09 | 11.97 |
| 6 | 14.86 | 12.28 |
| 7 | 14.24 | 13.16 |
| 8 | 12.13 | 11.94 |
| 9 | 10.52 | 10.19 |
| 10 | 7.14 | 7.18 |
| 11 | 1.91 | 4.22 |
| 12 | 0.12 | 0.37 |

The global anterior–posterior bounding depth of all 24 rib meshes plus sternum is **20.81 cm male and 16.44 cm female** in the actual T-pose, before and after the girdle change. Width is 29.81/27.40 cm and height 36.56/38.01 cm. These are model-frame bounding extents, not measurements of one horizontal chest slice, and include no skin or costal cartilage. They are affected by orientation and segmentation.

For context only, a [CT study of 1,719 adults aged 25–45](https://pmc.ncbi.nlm.nih.gov/articles/PMC11445027/) reported whole rib-cage bounding depth **19.53 ± 1.75 cm for 1,225 males** and **17.77 ± 1.52 cm for 494 females**. That study normalized the orientation and its automated segmentations omit portions of posterior rib heads; the present registered models and individual demographics/pose are not matched. These values do not establish a subject-specific normal depth, diagnosis, or an amount that should be moved.

Inspiration can raise and move ribs/sternum forward, but one static [registered post-mortem assembly](https://zenodo.org/records/22833546) contains no paired inhalation/exhalation frames or known lung volume. The original structures were registered independently. Neither the breathing phase nor a correction toward expiration can be inferred from this geometry. The missing anterior costal cartilage can also affect how the bare bony ribs look. Ribs naturally curve and slope; arbitrarily flattening or translating them would change shape or damage attachments. Therefore **rib positions have not been changed**; fully straightening them remains unresolved. The existing physics uses limited, inferred cartilage constraints, not scanned cartilage.

## Reproduce and limitations

`TPOSE_BASELINE_d26c7b3.tsv.gz` stores matrices exported from the real room pose at commit `d26c7b3` (SHA-256 `e523557fa618536f6ce31c6a6b4173afcdb926e5bef81df43fe841d8e94e153b`). The audit builds the current exporter, transforms the same game vertices and writes both sets. No simulated breathing or per-bone scaling is applied.

```sh
python3 tools/skeleton-source/openskeleton/41-audit-rib-girdle.py
python3 tests/rib_girdle_audit.py
```

Requires NumPy, SciPy, C++17 and vendored Bullet. `tests/skeleton_girdle_pose.cpp` checks six SC/AC/shoulder anchors, rigid transforms and exact source/reference restoration for each model. Existing T-pose and ragdoll tests check horizontal arms, joint attachment, floor contacts, hand joints and cartilage links. These regressions do not establish clinical validity, continuous collision safety for every possible pose, or validate the simplified V-view against patient anatomy.
