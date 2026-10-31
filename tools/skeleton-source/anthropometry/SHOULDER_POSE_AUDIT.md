# Shoulder-width measurement correction

**The previous 31.38 cm male and 33.14 cm female shoulder widths and their percentage differences are withdrawn.** They were the X separation of two geometric scapular extrema in the registered mesh, not validated biacromial breadths. I presented those proxies too prominently as shoulder widths. Neither provides a reliable comparison with the NHANES reference.

## What the reference actually measures

[NHANES III's anthropometry manual, §3.3.1.6](https://wwwn.cdc.gov/nchs/data/nhanes3/manuals/anthro.pdf), specifies the **maximum breadth between the lateral borders of the two acromial processes**, seated upright, arms hanging freely, using sliding calipers on the skin while compressing the soft tissue. It is incorrect to explain the entire discrepancy as ordinary shoulder muscle width: this is a bone-landmark measurement through compressed tissue, not bideltoid breadth. The model predicts 40.09 cm male at 170.0 cm and 38.18 cm female at 170.2 cm. Observed unweighted NHANES adults at 165–<175 cm have mean ± SD 40.45 ± 2.12 cm male and 37.94 ± 2.13 cm female.

## What was measured

`35-seven-dimensions.py` originally selected the most lateral vertex in the upper quarter of each registered scapula and subtracted their X coordinates. This has two independent problems:

1. The algorithm did not identify the **acromion**; an upper scapular extremum is not automatically a verified acromial lateral border. The selected points are recorded in `SHOULDER_POSE_AUDIT.tsv` so they can be examined rather than guessed.
2. [OpenSkeleton](https://zenodo.org/records/22833546) states that the upper-extremity structures were repositioned into a common frame. The source does not establish that this registered assembly reproduces the NHANES seated, arms-down pose or the original cadaver pose. Thus the separation of separate scapular vertices is pose dependent.

The prior figures and any percentage deviations or standard-deviation scores derived from them should not be interpreted as real shoulder breadth or abnormality. Joint-centre spacing is also not biacromial breadth.

## Reproducible checks

| Diagnostic | Male | Female |
|---|---:|---:|
| Old simplified-mesh upper-scapula X span | 31.38 cm | 33.14 cm |
| Same geometric span, original full-resolution registered meshes | 31.28 cm | 33.03 cm |
| Simplification change | +0.10 cm | +0.11 cm |
| Clavicle endpoint X–Y angle proxy, left / right | 41.9° / 37.3° | 38.4° / 32.2° |
| Hypothetical span if both proxy angles were 10° | 34.48 cm | 36.85 cm |
| Hypothetical span if both proxy angles were 20° | 34.35 cm | 36.16 cm |

The simplified meshes reproduce their registered-source extrema to about 1 mm, so simplification does **not** explain the several-centimetre gap. The clavicle angles above are not clinical angles: each comes from centroids of the most medial and lateral 5% of clavicle vertices projected into the asset's X–Y plane. They indicate pose sensitivity, not a diagnosis or proof of a specific pose error.

For the last two rows, the already-selected scapular point is rotated in the X–Y plane around the medial clavicle centroid until the **clavicle proxy** has the named hypothetical angle. This simplistic manipulation does not move or fit a whole skeleton, is not a clinically validated motion, does not reconstruct seated posture, and is **not a replacement shoulder-width measurement**. It only demonstrates that the selected cross-body separation changes by several centimetres with plausible-looking changes to assumed pose. No actual patient shoulder width can be inferred from the example angles.

A defensible biological comparison needs identifiable lateral acromial borders on both scapulae, a documented comparable anatomical pose and validation against paired CT/surface landmarks or expert annotation. At the user's request, the current comparison table does show the measured T-pose scapular X extrema and raw subtraction from the reference; those numbers are not verified biacromial values. See `TPOSE_SCAPULA_EXTREMA.md`.

## What we can and cannot conclude about the cause

The original registered-pose simplified-mesh X widths were 31.38/33.14 cm. The prior T-pose widths were 31.36/33.21 cm; the current T-pose widths are 31.40/33.22 cm after a conservative bilateral clavicle-slope balancing. The scapula follows the clavicle at its existing joint, and the arms are re-aligned. This is a geometric symmetry convention, not a validated clinical neutral shoulder-girdle pose or evidence of the subjects' living shoulder breadth; see `RIB_AND_GIRDLE_POSE.md`. As shown above, mesh simplification accounts for only about 1 mm; different landmarks and joint/registration pose remain possible contributors, but their individual contributions are not known. The hypothetical rotation is a sensitivity exercise, not proof of the cause or of what the biological width would have been.

Neither the mesh width nor the available metadata establishes that either person had a medical condition. Both assets have left and right clavicle meshes, but their existence alone does not diagnose or rule out a condition. The female subject is recorded as 41; the male subject's age and clinical history are not established. Original patient CT and expert-identified bony acromia, with a comparable standardized posture, would be needed to evaluate individual biological width; a clinical diagnosis would require appropriate medical history and examination as well.

## Reproduce

```sh
python3 tools/skeleton-source/openskeleton/36-audit-shoulder.py
python3 tests/shoulder_pose_audit.py
python3 tools/skeleton-source/openskeleton/35-seven-dimensions.py
python3 tests/seven_dimension_comparison.py
```

Inputs are the two tracked source NPZ files and simplified game OBJs. The raw registered coordinates receive only the same global coordinate/unit conversion and translation as the importer. The audited points, centroids, angles and spans are recorded in `SHOULDER_POSE_AUDIT.tsv`.
