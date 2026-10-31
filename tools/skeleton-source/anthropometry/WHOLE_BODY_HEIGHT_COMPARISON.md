# Five body dimensions from height, and the two skeletons

## Answer and limits

`native/client/height_body_reference.h` now exposes `estimateHeightBodyReference(sex, height_cm)`: one stature input and a sex preset return numerical estimates for head, neck, torso, full arm and full leg in centimetres. This is an **exploratory surface-reference profile**, not a completed one-height skeleton scaler. It does not yet move or scale bones, joint anchors, colliders, or ragdolls. Height alone cannot uniquely determine age, build or individual anatomy. Inputs outside observed cohorts are design extrapolations, not medical evidence.

The two OBJ skeletons can be measured, but their bone-only geometry does not supply the same surface landmarks as the reference data. The following are **raw geometric discrepancies between unmatched measures**, not how far each person is from a medical mean or proof of abnormality. In particular, the head reference is from a dataset whose endpoint/protocol has not been verified, and the neck uses the jaw–skull connection as a stand-in for the ear landmark and an approximate **unverified head reorientation**; its raw differences are particularly uncertain. The neck, arm and leg use bilateral averages; left/right are in the TSV.

### Male skeleton N197925, recorded cadaver length 170.0 cm

| Dimension | Measured skeletal proxy cm | Male height formula cm | Raw difference cm (%) | Female formula cm; raw difference |
|---|---:|---:|---:|---:|
| Head | 21.19 | 22.96 | −1.76 (−7.7%) | 21.97; −3.5% |
| Neck (jaw-joint proxy) | 11.90 | 10.36 | +1.54 (+14.8%) | 11.10; +7.2% |
| Torso | 65.42 | 65.30 | +0.11 (+0.2%) | 63.64; +2.8% |
| Full arm | 72.67 | 76.06 | −3.39 (−4.5%) | 75.79; −4.1% |
| Full leg | 87.14 | 86.67 | +0.47 (+0.5%) | 88.81; −1.9% |

### Female skeleton N186416, recorded cadaver length 170.2 cm

| Dimension | Measured skeletal proxy cm | Female height formula cm | Raw difference cm (%) | Male formula cm; raw difference |
|---|---:|---:|---:|---:|
| Head | 20.73 | 21.98 | −1.26 (−5.7%) | 22.97; −9.8% |
| Neck (jaw-joint proxy) | 13.70 | 11.11 | +2.59 (+23.3%) | 10.38; +32.0% |
| Torso | 64.93 | 63.71 | +1.22 (+1.9%) | 65.37; −0.7% |
| Full arm | 68.30 | 75.89 | −7.59 (−10.0%) | 76.15; −10.3% |
| Full leg | 79.99 | 88.92 | −8.93 (−10.0%) | 86.79; −7.8% |

`raw difference = bony proxy − surface prediction`, percentage divides that by the prediction. These numbers are provided so the arithmetic is visible, **not** as matched biological deviations. Similar torso or leg values do not prove good fit; larger arm or leg differences do not prove abnormality. The head gap is particularly uncertain. No percentile, standard deviation score, normality classification or overall average is warranted. `WHOLE_BODY_SKELETON_PROXIES.tsv` has unrounded calculations and both sex formulas.

## Direct neck-to-skull measurement

The neck attaches to the skull at the two **atlanto-occipital (C1–skull) joints**. The following measures from an estimated midpoint of each C1–skull contact to the most posterior C7 vertex, in the **original registered source pose**. This directly addresses the skeleton's skull connection; unlike the jaw proxy above, no head re-levelling is applied.

| Subject | L / R vertical cm | Mean vertical cm | L / R straight 3D cm | Mean straight 3D cm |
|---|---|---:|---|---:|
| male N197925 | 9.411 / 9.476 | 9.443 | 10.539 / 10.774 | 10.657 |
| female N186416 | 11.706 / 11.123 | 11.414 | 13.516 / 12.967 | 13.242 |

`34-measure-neck-skull-contact.py` uses the upper half of C1 vertices, splits at the C1 centroid into left/right with an 8 mm minimum lateral displacement, and finds the closest skull vertex for each candidate. The midpoint of the closest pair on each side is a geometric contact proxy. C1–skull surface gaps are L/R **1.34/0.36 mm male** and **1.72/0.47 mm female**. See `SKELETON_NECK_SKULL_CONTACT.tsv` for both actual surface points, the midpoint, C7 coordinates, gaps and side lengths. The triangulated meshes are simplified and registered, and nearest points do **not** prove those points are the true articular surfaces or joint centres. Bone pose and upper/lower landmark choices can materially change both lengths.

**Do not compare these numbers directly to the ANSUR neck-link formula.** That reference ends at the surface tragion near the ear, not at the C1–skull joint, and is a vertical surface measure. A jaw–skull contact proxy for that ear-level reference is reported separately in the five-dimension table. A validated height-to-C1–skull/C7 reference with matching endpoints has not been established here, so this direct skeletal measurement has no claimed formula deviation.

## Exactly what was measured

Measurements are from the **registered source pose** of the shipped `skeleton-{male,female}.obj` in metres, converted to cm. Recorded cadaver lengths (170.0/170.2 cm), rather than mesh vertical bounding-box heights, are the formula inputs. They are not guaranteed to equal living standing stature. Automated joint fits and mesh landmarks are not expert-validated; position and segmentation can affect results.

* **Head:** difference in Y between highest `Head` and lowest `Jaw` bone vertices. Not a verified vertex-to-menton anthropometric measurement; the jaw is in its registered pose and skin/hair are absent. Formula source uses an unverified "vertical head length" field, so even landmark correspondence is unknown.
* **Neck:** see the dedicated method and uncertainty audit below. The old C1–C7 extent was **removed**, because it does not measure neck link. The replacement is an approximately re-levelled jaw–skull joint to C7 vertical proxy, **not a measurement of tragion or an exact neck link**.
* **Torso:** Y of most posterior C7 vertex minus the lower Y extremum of the left/right pelvis bone meshes. This is neither palpated cervicale nor the standing skin crotch, and it depends on pose. The closest reference here is standing cervicale height minus crotch height.
* **Arm:** per side, sum 3D distances between fitted humeral-head centre, fitted elbow centre, average distal 2% of radius vertices along its long PCA axis, centroids of metacarpal 3 and three third-finger phalanges, and an end vertex on the distal third phalanx. This deliberately follows the bent finger rather than treating a flexed hand-to-elbow straight chord as a fully extended arm. The chain is an approximation to an extended centreline, not an acromion-to-fingertip skin measure; the humeral head is not the acromion, and wrist/finger landmarks are geometric. Mean of sides.
* **Leg:** per side, sum fitted femoral head centre–fitted knee centre–distal fibula extremity plus the Y distance from the latter to the lowest calcaneus vertex. This does **not** begin at the greater trochanter, does not include plantar soft tissue, and includes a mixture of path lengths and a vertical heel-height proxy. It is not the same as upright trochanterion-to-floor height. Mean of sides. The extremity is selected along the fibula's PCA long axis away from the knee, not by a farthest-vertex diameter.

The segment choices explain why even identical subjects would not be expected to produce zero discrepancy. For detailed independently remeasured bone spans, including their own endpoint limitations, see `SKELETON_BONE_CROSS_SEX_COMPARISON.md`.

## Neck comparison: why the previous deviations were high

**The previous 34%/38% matched-sex numbers were an error and are withdrawn.** I equated the height of the eye socket in an arbitrary source pose with the height of the ear, without establishing that the head was in the Frankfort plane. The two are only at the same height in the specified head orientation; these registered skulls do not meet that demonstrated condition. Further, the original registered source is not guaranteed to be an anatomically assembled standing pose. Neither error proves the subjects have long necks.

The relevant ANSUR D20 reference is *vertical cervicale to tragion* with the head in Frankfort alignment, not C1–C7 bone extent, or a 3D C7–ear chord. Neither the surface tragion nor a reliably identifiable ear-canal landmark is present in these skull meshes. To improve the geometric estimate without inventing a measured ear, we use a different nearby landmark: the posterior upper jaw condyle is immediately anterior to the ear canal ([anatomy](https://emedicine.medscape.com/article/1948907-overview)). A cephalometry reference describes the superior glenoid fossa near the condyle as a possible porion-level substitute ([source](https://plasticsurgerykey.com/cephalometry-and-cephalometric-analysis-3/)). The jaw-joint midpoint is an approximate anatomical proxy, not the soft-tissue tragion.

Method: use the orbit detector in `32-neck-orbital-proxy.py` to find approximate lower orbital-rim points; select the highest-Y vertex from the posterior 15% of each lateral half of the `Jaw` as a condyle point. On each side, find the closest `Head` vertex to that point and use the **midpoint between these two surfaces as the estimated jaw–skull connection**. This is a geometric approximation, not a fitted joint axis; the nearest skull point is not necessarily the deepest fossa point, and cartilage is absent. The jaw-to-skull surface separations are **3.74 / 2.49 mm male and 0.48 / 1.19 mm female** (L/R). Positive X is the fitted left-shoulder side.

For the standardized estimate, take the mean of the eye points and the two jaw-joint points, estimate their sagittal line angle, and rotate the two jaw-joint points about the `C1` mesh centroid to level that line. The posterior-most `C7` vertex remains fixed. The resulting measurement is a **re-levelled jaw-joint-to-C7 vertical proxy**. The orbital-to-joint slopes in the source poses are **14.2° male and 11.5° female**. These are not verified Frankfort angles: a jaw connection is not the same landmark as porion. This operation is only in the measurement script and does not change the game skeleton.

| Subject | Previous orbital-level proxy cm | Jaw-joint level in source pose cm | Re-levelled jaw-joint level cm | L/R re-levelled cm |
|---|---:|---:|---:|---|
| male N197925 | 13.882 | 12.444 | 11.901 | 11.879 / 11.924 |
| female N186416 | 15.337 | 13.995 | 13.700 | 14.294 / 13.107 |

The jaw-joint midpoint makes the requested comparison possible without inventing an ear point. The drop is an endpoint and pose-method correction, **not a change in the subjects**. The main table uses these bilateral means; ANSUR predictions are unchanged. The remaining 14.8%/23.3% are differences against a surface-landmark formula, not biological diagnoses: jaw joint and tragion are close but distinct, the source pose may not be anatomically assembled, C7 skin is absent, and the reorientation is approximate. The female's left/right joint proxy differs notably, so the mean hides a sizeable registered asymmetry.

An independent diagnostic uses the source's dense, corresponding per-bone vertices (`33-neck-pose-audit.py`): after centering each C7, a least-squares rigid rotation maps female to male with XYZ angles **15.36°, −5.59°, 1.07°** (shape residual 3.13 mm). T1 and T2 have similar relative rotations; these are shape-and-pose fits, not diagnostic joint angles. Their posterior C7 tip Y relative to its own centroid is **+11.39 mm male and −7.57 mm female**, a **18.96 mm difference** in registered coordinates. This shows why comparing the raw tip heights between subjects is pose/shape-sensitive; it does not isolate their anatomical effects. Do not rotate the bones to force a reference match.

Method sensitivity is not a confidence interval: changing the orbital recess threshold from 22 to 18–26 mm moves the re-levelled mean up to **0.18 cm male and 0.002 cm female**. Varying the posterior jaw subset from 5–30% moves it up to **0.03/0.04 cm**, respectively. These ignore the largest unknowns. As external context only, a [30-person CBCT landmark study](https://pmc.ncbi.nlm.nih.gov/articles/PMC11043836/) ages 12–19 reported first-trial mean porion and condylion vertical coordinates differing by 0.77 mm right and 0.13 mm left; these are differences of sample means, **not** individual errors or adult bounds, and neither is soft-tissue tragion. The true total error is unknown. A rigorous comparison requires validated CT/scan ear and C7 landmarks plus matched alignment; small residual differences must not be inferred from this proxy.

## Reproducible height equations

All inputs and outputs below are cm. With `h` in the recorded adult support, the adult model is `intercept + slope*h`. These were fitted in the forward direction, never by inverting a stature-from-bone equation:

| Sex | Head | Neck | Torso | Arm | Leg |
|---|---|---|---|---|---|
| Male | 9.519390 + 0.079034 h | −2.1659706847 + 0.0736988329 h | 9.8246268891 + 0.3263433653 h | −1.9444316094 + 0.4588502422 h | −16.9948364970 + 0.6097644466 h |
| Female | 11.177137 + 0.063486 h | 0.0376124352 + 0.0650600532 h | 8.8316779887 + 0.3224152046 h | −3.3344315179 + 0.4654548245 h | −12.6914582511 + 0.5970413779 h |

Definitions and evidence:

* **Head:** exploratory Korean-language paired dataset, 498 men and 500 women. `머리수직길이` translates approximately as vertical head length, but survey provenance, population selection and endpoints are unknown; the 29 cm male value is a possible outlier. Observed heights male 154.7–187.7, female 142.3–172.6. This is not established clinical data; see `HEIGHT_VARIATION_REPORT.md` and `EXPLORATORY_KOREAN_HEAD_FITS.tsv`. Do not confuse head depth with head height.
* **Neck:** forward OLS of the individually derived ANSUR II **D20 neck link**, the *vertical* cervicale-to-tragion difference: `stature − tragiontopofhead − cervicaleheight`. The [2012 U.S. Army pilot measurement report](https://docplayer.net/102326834-2012-anthropometric-survey-of-u-s-army-pilot-personnel-methods-and-summary-statistics.html) explicitly defines D20 this way, and describes tragion-to-top as a vertical seated measurement with the head in the Frankfort plane, while stature and cervicale height are standing measures. It is a derived quantity combining measurements in two postures, not a direct 3D C7-to-ear chord. The raw 93-column working file does not contain D20, so we derive it for each adult before fitting against stature. No BMI assumption enters our fit. Fichera et al. use a different height-and-BMI Bayesian model for neck link, with values tabulated at BMI 25 ([paper](https://static.nhtsa.gov/esv/pdf/ESV/Proceedings/28/ESV26-044-Fichera-2026-Neck-260131.pdf)); the present coefficients are **not** theirs. Some other publications use “neck link” for a linear 3D distance; these are not interchangeable with ANSUR D20.
* **Torso:** ANSUR II adult forward OLS of paired `cervicaleheight - crotchheight`. This is a vertical surface measure, not vertebral chain length or sitting height.
* **Full arm:** ANSUR II adult forward OLS of paired `sleeveoutseam + handlength`: acromion-to-stylion plus stylion-to-middle fingertip. Two linear distances added at the wrist form an **extended-arm approximation**, not a directly observed one-piece arm measurement. Source definitions: [ADAS measurement manual](https://mreed.umtri.umich.edu/mreed/downloads/anthro/ansur/ADAS-Dimension_Definitions.pdf).
* **Full leg:** ANSUR II adult forward OLS of `trochanterionheight`, vertical height of the greater-trochanter landmark above the standing surface. Includes ankle-to-floor, does not start at the hip joint centre. It is a vertical standing measurement, not flexed-leg centreline length.

The four ANSUR fits used 4,081 adult men and 1,985 adult women, unweighted, military population, from `ANSUR_II_PAIRED_MM.csv` (SHA-256 `1b1c0865c328a6aa297a3133f49585eff61002e9208fe3beb183c866c316fbee`). Height ranges are 149.1–199.3 and 140.9–182.9 cm respectively. For neck, in-sample residual SD is 1.16 cm male and 1.11 cm female; leave-one-out RMSE is 1.16/1.11 cm and R² is 0.159/0.124. Height alone leaves most variation unexplained. In-sample residual SDs (not a clinical normal interval) for male torso/arm/leg are 2.27/2.16/2.59 cm and female 2.17/2.20/2.30 cm. This scatter applies to the matched surface measures in that sample, **not** to the skeletal discrepancies above. Formula coefficients are independently refit and checked by the reproduction script.

## Below and above adult range

The code imposes no fixed height cutoff. Outside a formula's observed endpoints it smoothly extends the ratio `value/height` in log-height with matched endpoint value and slope; it remains positive and below height for ordinary finite inputs, but is **not a medically fitted growth curve**. Floating-point overflow/underflow may still cause a rejected estimate. The cervicale/crotch, head, and neck estimates at infant heights are unvalidated extrapolations.

For arm and leg, the code adds near-50-cm anchors from [Chellappan et al.](https://indianpediatrics.net/epub012024/OA-00580.pdf), 39-week sex-specific median individual ratios: full arm 42.86%/42.81% of length; greater-trochanter-to-lateral-malleolus 41.02%/41.50%, male/female. To estimate *floor* rather than ankle, it adds **2.2915/2.1843 cm**, separately extrapolated from adult ANSUR malleolus-height fits to 50 cm. This added ankle height is a modelling assumption, not infant data. A quintic smootherstep blends the newborn proportional approximation into the sex-specific adult model from 50 cm to the first adult observed height; it is continuous with continuous slope, but that whole interval is **not validated on children**. Below 50 cm the anchor ratios continue proportionally, not a fetal model. The newborn cohort and adult sample differ in pose and population; transition heights do not determine age. Chellappan uses recumbent length, adult ANSUR standing stature.

| Stature cm | Sex | Head | Neck | Torso | Arm | Leg |
|---:|---|---:|---:|---:|---:|---:|
| 50 | male | 11.26 | 2.26 | 23.30 | 21.43 | 22.80 |
| 50 | female | 12.26 | 3.28 | 22.59 | 21.41 | 22.93 |
| 100 | male | 16.96 | 5.36 | 41.88 | 43.47 | 45.32 |
| 100 | female | 17.21 | 6.54 | 40.68 | 43.14 | 46.90 |
| 200 | male | 25.31 | 12.57 | 75.09 | 89.83 | 104.96 |
| 200 | female | 23.80 | 13.05 | 73.28 | 89.77 | 106.77 |

All rows above are partially or wholly extrapolated/bridged; the numbers are shown as stress-test outputs, not validated references. The existing separate NHANES child/adult surface curves and their shoulder/bi-iliac breadth estimates are in `CHILD_ADULT_TRANSITION.md` and `GIRDLE_WIDTH_FORMULAS.md`. This five-part profile does not solve the missing height-to-humeral-centre or height-to-CT-hip-centre relationship, nor does it calibrate a surface length into a skeleton's centreline or bone scale. Better age-spanning matched skeletal data are still needed.

## Reproduce

From the repository root:

```sh
c++ -std=c++17 -O2 -I native tools/skeleton-source/openskeleton/31-height-profile.cpp -o /tmp/height-profile
/tmp/height-profile male 170
/tmp/height-profile female 170.2
python3 tools/skeleton-source/openskeleton/31-measure-whole-body.py
python3 tests/neck_orbital_proxy.py
python3 tools/skeleton-source/openskeleton/34-measure-neck-skull-contact.py
python3 tests/neck_skull_contact.py
python3 tools/skeleton-source/openskeleton/33-neck-pose-audit.py
c++ -std=c++17 -O2 -Wall -Wextra -Werror -I native tests/height_body_reference.cpp -o /tmp/test-height-profile && /tmp/test-height-profile
```

The measurement script requires NumPy, SciPy and a C++17 compiler. It refits the ANSUR coefficients to check them, compiles the same C++ profile, remeasures both OBJs/joint files and regenerates `WHOLE_BODY_SKELETON_PROXIES.tsv`. The test sweeps 50–250 cm at 0.001-cm increments for numerical continuity and monotonicity; this verifies the maths, **not anatomical accuracy**.
