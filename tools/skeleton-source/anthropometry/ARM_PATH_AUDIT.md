# How the previous arm lengths were measured

**Correction:** the old 72.67 cm male / 68.30 cm female numbers are lengths of a *bone-centre path*, not matched acromion-to-fingertip measurements. The previously reported −4.46% / −10.00% comparisons are withdrawn as biological arm-length deviations. The true standardized arm lengths have not been established. This does not mean the arms are definitely short or definitely normal.

## Exact old method

For each side, on the **registered source-pose simplified OBJ**, the script summed 3D distances along this chain:

1. fitted **humeral-head centre** → fitted **elbow axis centre**;
2. elbow centre → centroid of the distal 2% of **radius vertices** selected by the radius's principal axis;
3. that radius point → centroids of the third metacarpal and its proximal, middle and distal phalanges → an end vertex of the distal phalanx, selected in the direction from the middle to the distal phalanx.

Left and right totals were averaged. The method uses bone geometry, estimated joint centres and a bent-finger path; the digit path is not a fully straightened anatomical measurement, and the wrist and fingertip are not validated surface landmarks. The elbow is a geometric fit, with clearance refinements, not the surface radiale landmark.

By contrast, the adult ANSUR formula is fitted to **sleeve outseam + hand length** on each individual: acromion-to-stylion with the arm straight at the side, plus stylion-to-middle-fingertip. [ANSUR II's manual](https://apps.dtic.mil/sti/tr/pdf/ADA611869.pdf/) and the [ADAS definitions](https://mreed.umtri.umich.edu/mreed/downloads/anthro/ansur/ADAS-Dimension_Definitions.pdf) identify these endpoints. The humeral-head centre is **inside** the shoulder joint, not the acromion on the top/lateral scapula. The skin fingertip is beyond the distal finger bone. No verified acromion positions or equivalent straightened pose were established on either skeleton. We cannot correct the missing length by simply adding an arbitrary amount.

## Where the numbers came from

The table shows **bilateral means**, in cm. The side values are in `ARM_PATH_AUDIT.tsv`. Biological component columns are separate paired-data height regressions, at each subject's recorded cadaver length. Their endpoints differ from the bone paths; the table diagnoses method differences, **not segment-specific clinical deviations**.

| Old path component | Male bone path | Male ANSUR component | Female bone path | Female ANSUR component |
|---|---:|---:|---:|---:|
| Humeral-head centre → elbow centre vs acromion → radiale | 27.45 | 32.38 | 27.92 | 32.68 |
| Elbow centre → radius end vs radiale → stylion | 26.83 | 25.88 | 23.54 | 25.33 |
| Radius end → finger-bone tip path vs stylion → skin fingertip | 18.39 | 18.78 | 16.84 | 18.85 |
| Sum of old bone path vs sleeve outseam + hand fit | 72.67 | 76.06 | 68.30 | 75.89 |

The first row shows the largest method mismatch: the reference includes the path starting at the acromion, while the old calculation starts inside the shoulder. It is **not** a direct estimate of missing length: both shoulder and elbow endpoints differ, and the subjects were not scanned in the reference posture. The female forearm and hand proxies are also shorter than the corresponding reference components; whether this reflects anatomy, endpoint placement, registration or pose requires matched-landmark validation. Summing the three reference *component* fits does not equal the outseam-plus-hand fit, because the outseam is one straight shoulder–wrist distance while the two component measures meet at the elbow.

As a sensitivity check only, using the previously selected **unverified scapular extremum** as a candidate shoulder point changes the mean straight point-to-elbow distance from 27.45 to 30.12 cm male and from 27.92 to 30.92 cm female. This shows the size of the endpoint issue; it does not establish that the candidate is the acromion, correct the pose, or yield a valid new full-arm measurement.

The ANSUR adult military regressions use 4,081 male / 1,985 female paired observations. The sex-specific formula at height `h` in cm is male `−1.9444316094 + 0.4588502422 × h`, female `−3.3344315179 + 0.4654548245 × h`. It is an estimated population reference, not a measurement of these skeletons. Height alone does not determine individual arm length.

## Reproduce

```sh
python3 tools/skeleton-source/openskeleton/37-audit-arm.py
python3 tests/arm_path_audit.py
```

The audit calls the exact same path function as the original generator, independently refits each ANSUR component on paired records and records left, right and mean values. A valid comparison would require identifiable acromion, radiale, stylion and fingertip landmarks and a comparable straightened pose, or a validated mapping from those to the skeleton's joint/bone landmarks.
