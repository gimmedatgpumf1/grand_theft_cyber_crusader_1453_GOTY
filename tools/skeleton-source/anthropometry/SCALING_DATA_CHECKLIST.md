# Scalable skeleton research checklist

This tracks *data and evidence*, not a claim that runtime scaling has been implemented. A measurement is useful even when it does not cover the full height range. Record source, population, sex, age, units, landmark definition and observed range; distinguish measured bones from skin landmarks and derived equations from published ones. Never invert an inverse regression and present it as a forward fit.

## Priority 1 — proportions and growth

- [x] Adult male/female paired body dimensions and stature: ANSUR II (`ANSUR_II_PAIRED_MM.csv`), plus NHANES 2017–18 paired upper-arm/upper-leg lengths.
- [x] Child and adult sex-specific 5 cm height bins for upper-arm and upper-leg lengths (`NHANES_ALL_AGES_HEIGHT_BINS.tsv`).
- [x] Crown-to-chin and facial landmark means, sex and age groups 8–24 (`HEAD_AGE_STUDY_SUMMARY.tsv`); paired stature absent.
- [x] Published sex/age-group means and SD of paired stature / vertex–chin ratios ([PEDIATRIC_PAIRED_HEAD_RESEARCH.md](PEDIATRIC_PAIRED_HEAD_RESEARCH.md)); exploratory, nonclinical source.
- [ ] Extract and validate individual paired stature and complete head height, by sex and age, from the [NIST AnthroKids individual records](https://math.nist.gov/~SRessler/anthrokids/orig77/individuals.csv) and verify sex coding and missing fields. The primary source includes full head height. Its separately published [age-group means](ANTHROKIDS_HEAD_HEIGHT_BY_AGE.tsv) have been transcribed. Third-party `EXPLORATORY_KOREAN_HEAD_FITS.tsv` is a lead only; its origin and landmark protocol remain unverified.
- [x] Child/adult paired stature and sitting height, by sex, age and 5 cm stature bin (`NHANES_III_PAIRED_CM.csv`, `NHANES_III_HEIGHT_BINS.tsv`); subischial height is stature minus sitting height, not a measured bone.
- [x] Child foot and stature by sex and 5 cm stature bin (`ANTHROKIDS_FOOT_HEIGHT_BINS.tsv`); additional hand/forearm research remains open. NHANES III adds biiliac/biacromial surface breadths and sitting height for children. Infant recumbent posture has not been combined with these standing-height datasets.
- [x] Pediatric DXA sex-specific age-group stature/radius/femur/tibia and acromion-to-epicondyle summaries, published bone ratios and forward bone-to-bone equations (`PEDIATRIC_DXA_*`). These are summary data, not paired individuals, and ulna remains open.
- [ ] Pediatric pelvis width, hip joint centres, femur and tibia geometry/angles, by sex and height. [Carman et al.](https://www.nature.com/articles/s41598-023-38783-6) describes 333 pediatric CT datasets, age 4–18; [SimTK](https://simtk.org/projects/paed_ssm) reportedly hosts measured data. An independent fetch script reports an account gate; authenticated download and reuse conditions remain unverified.
- [x] Direct adult sex-specific hand-bone, toe-bone and thoracolumbar vertebral summaries ([SMALL_BONE_CT_XRAY_REFERENCES.md](SMALL_BONE_CT_XRAY_REFERENCES.md)); these do not supply stature-linked scaling.
- [x] Paired same-side adult humerus/radius/femur/tibia and pelvic breadth by estimated sex, femur length and broad source location ([GOLDMAN_RESEARCH.md](GOLDMAN_RESEARCH.md)); **not** paired with actual stature.
- [ ] Individual vertebrae, ribs, scapula, clavicle, metacarpals and phalanges versus stature/age/sex. Do not mistake external lengths or inverse-stature forensic equations for direct bone predictions.
- [ ] Better documented extreme-stature measurements. Keep children, clinical cases, exceptionally short/tall adults and dysplasia separate; do not treat a clinical condition as generic for a height.

## Priority 2 — applying data safely

- [ ] Map measurable landmarks to the actual skeleton bones and report ambiguity; original subjects' ethnicity and male subject age remain unknown.
- [ ] Derive forward bone-on-stature models only from paired data, published forward models, or use explicitly labeled observed ratios/group values; include sample count, uncertainty, units, age and height support.
- [ ] Decide how age/body shape affects estimates; height alone does not uniquely determine either. Assess discontinuities where population models meet; explicitly mark every extrapolation.
- [ ] Recompute bone-local joints, hand/foot attachments, collisions, mass/inertia and ground contact with transformed bone geometries; document approximations. Research alone does not perform this.
- [ ] Verify original-size skeletons stay unchanged, links remain attached, pose/physics remain stable, and both sex presets behave coherently across target heights. Verify on the target Vulkan/Windows build as available.
