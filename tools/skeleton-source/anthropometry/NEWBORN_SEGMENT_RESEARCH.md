# Paired newborn limb/length ratios

[Chellappan et al., Segmental Limb Length Measurements in Term Neonates From Southern India](https://indianpediatrics.net/epub012024/OA-00580.pdf), published online 9 January 2024, sampled 950 term newborns in Tamil Nadu. A subset of **408** (195 boys, 213 girls) also had their recumbent crown–heel length measured on an infantometer. The authors computed each child's limb measurement divided by their own length; Table III gives subgroup **median and interquartile range of these individual ratios**, not averages or standard deviations. `NEWBORN_SEGMENT_RATIO_REFERENCES.tsv` transcribes eight such surface dimensions for every sex/gestational-week subgroup. The table is reproduced by `python3 tools/skeleton-source/openskeleton/28-newborn-segment-ratios.py`.

Measurements are right-side surface measurements, repeated twice, ages 24–72 hours. They are *not bone lengths* or joint-centre distances. In particular:

- upper arm: acromion tip–olecranon, elbow bent 90 degrees;
- forearm: olecranon–radial styloid, elbow bent 90 degrees;
- hand: distal wrist crease–middle-finger tip;
- thigh: greater trochanter–lateral femoral condyle, knee bent 90 degrees;
- lower leg: lateral femoral condyle–lateral malleolus, knee bent 90 degrees;
- foot: posterior heel–big-toe tip, not necessarily the longest toe.

The separately measured whole-upper-limb and whole-lower-limb indices have different endpoints and **must not be reconstructed by adding the component ratios**. In particular, the whole lower limb includes hip-to-thigh/ankle geometry absent from the thigh and lower-leg measures. Femur shaft, tibia shaft and their joint centres are still different measurements, and the results cannot be directly used as bone-scale multipliers.

## One-height newborn approximation

`native/client/newborn_limb_profile.h` loads the sex-specific **39-week** rows (49 boys and 62 girls in the paired subset). The source's separately reported median recumbent lengths for these subgroups are 50 cm and 49 cm respectively. After selecting the source figure's sex, `estimate(lengthCm)` needs only the recumbent length and returns each dimension in cm as `lengthCm * subgroup_median_individual_ratio`. At 50 cm:

| Surface landmark (cm) | Male | Female |
|---|---:|---:|
| Upper arm | 9.500 | 9.540 |
| Forearm | 7.940 | 7.815 |
| Hand | 6.730 | 6.735 |
| Thigh | 8.825 | 8.855 |
| Lower leg | 9.785 | 9.695 |
| Foot | 7.980 | 8.000 |
| Whole upper limb | 21.430 | 21.405 |
| Whole lower limb | 20.510 | 20.750 |

This is an **approximation using subgroup medians**, not a regression fitted to length, not a 50-cm conditional median, and not a medical normality boundary. The authors report only group medians and quartiles, not raw individual pairs, so conditional fitting or leave-subject-out validation is not possible from the published table. The narrow 48–52 cm design window is an explicit guard, not a clinically validated range. A given length does not establish gestational age, sex, or healthy development; neonates and exceptionally short older people cannot be distinguished by length alone. Recumbent length is not standing height. The sample is a convenience sample of term neonates at one Indian hospital; 17.5% were small for gestational age. Other populations and premature babies may differ.

This newborn profile does **not** blend into the older-child or adult fitted curves, and does **not** deform the mesh, alter joints, collision shapes or physics. It is a safer real-data anchor for future integration, not the requested complete height-driven skeleton. The 39-week subgroup is chosen as a practical near-50-cm reference, not inferred from the input. Sources with different landmarks must not be spliced together without explicit mapping and validation. A 250-cm prediction remains unsupported by these data.
