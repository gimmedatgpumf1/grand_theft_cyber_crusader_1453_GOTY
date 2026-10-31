# Five body dimensions from height, and the two skeletons

## Answer and limits

`native/client/height_body_reference.h` now exposes `estimateHeightBodyReference(sex, height_cm)`: one stature input and a sex preset return numerical estimates for head, neck, torso, full arm and full leg in centimetres. This is an **exploratory surface-reference profile**, not a completed one-height skeleton scaler. It does not yet move or scale bones, joint anchors, colliders, or ragdolls. Height alone cannot uniquely determine age, build or individual anatomy. Inputs outside observed cohorts are design extrapolations, not medical evidence.

The two OBJ skeletons can be measured, but their bone-only geometry does not supply the same surface landmarks as the reference data. The following are **raw geometric discrepancies between unmatched measures**, not how far each person is from a medical mean or proof of abnormality. In particular, the head reference is from a dataset whose endpoint/protocol has not been verified, and the adult neck source does not define the endpoints of its "neck link" clearly enough to compare. The raw bony neck measurement is reported but no deviation is claimed. The arm and leg use bilateral averages as requested; left/right are in the TSV.

### Male skeleton N197925, recorded cadaver length 170.0 cm

| Dimension | Measured skeletal proxy cm | Male height formula cm | Raw difference cm (%) | Female formula cm; raw difference |
|---|---:|---:|---:|---:|
| Head | 21.19 | 22.96 | −1.76 (−7.7%) | 21.97; −3.5% |
| Neck | 13.12 | 10.61 | **not comparable** | 11.14; not comparable |
| Torso | 65.42 | 65.30 | +0.11 (+0.2%) | 63.64; +2.8% |
| Full arm | 72.67 | 76.06 | −3.39 (−4.5%) | 75.79; −4.1% |
| Full leg | 87.14 | 86.67 | +0.47 (+0.5%) | 88.81; −1.9% |

### Female skeleton N186416, recorded cadaver length 170.2 cm

| Dimension | Measured skeletal proxy cm | Female height formula cm | Raw difference cm (%) | Male formula cm; raw difference |
|---|---:|---:|---:|---:|
| Head | 20.73 | 21.98 | −1.26 (−5.7%) | 22.97; −9.8% |
| Neck | 12.46 | 11.15 | **not comparable** | 10.62; not comparable |
| Torso | 64.93 | 63.71 | +1.22 (+1.9%) | 65.37; −0.7% |
| Full arm | 68.30 | 75.89 | −7.59 (−10.0%) | 76.15; −10.3% |
| Full leg | 79.99 | 88.92 | −8.93 (−10.0%) | 86.79; −7.8% |

`raw difference = bony proxy − surface prediction`, percentage divides that by the prediction. These numbers are provided so the arithmetic is visible, **not** as matched biological deviations. Similar torso or leg values do not prove good fit; larger arm or leg differences do not prove abnormality. The head gap is particularly uncertain. No percentile, standard deviation score, normality classification or overall average is warranted. `WHOLE_BODY_SKELETON_PROXIES.tsv` has unrounded calculations and both sex formulas.

## Exactly what was measured

Measurements are from the **registered source pose** of the shipped `skeleton-{male,female}.obj` in metres, converted to cm. Recorded cadaver lengths (170.0/170.2 cm), rather than mesh vertical bounding-box heights, are the formula inputs. They are not guaranteed to equal living standing stature. Automated joint fits and mesh landmarks are not expert-validated; position and segmentation can affect results.

* **Head:** difference in Y between highest `Head` and lowest `Jaw` bone vertices. Not a verified vertex-to-menton anthropometric measurement; the jaw is in its registered pose and skin/hair are absent. Formula source uses an unverified "vertical head length" field, so even landmark correspondence is unknown.
* **Neck:** Y extent from highest C1 bone vertex to lowest C7 bone vertex. This includes vertebral processes and is neither a true clinical neck length nor the unspecified source's neck link. It must not be compared numerically to the formula.
* **Torso:** Y of most posterior C7 vertex minus the lower Y extremum of the left/right pelvis bone meshes. This is neither palpated cervicale nor the standing skin crotch, and it depends on pose. The closest reference here is standing cervicale height minus crotch height.
* **Arm:** per side, sum 3D distances between fitted humeral-head centre, fitted elbow centre, average distal 2% of radius vertices along its long PCA axis, centroids of metacarpal 3 and three third-finger phalanges, and an end vertex on the distal third phalanx. This deliberately follows the bent finger rather than treating a flexed hand-to-elbow straight chord as a fully extended arm. The chain is an approximation to an extended centreline, not an acromion-to-fingertip skin measure; the humeral head is not the acromion, and wrist/finger landmarks are geometric. Mean of sides.
* **Leg:** per side, sum fitted femoral head centre–fitted knee centre–distal fibula extremity plus the Y distance from the latter to the lowest calcaneus vertex. This does **not** begin at the greater trochanter, does not include plantar soft tissue, and includes a mixture of path lengths and a vertical heel-height proxy. It is not the same as upright trochanterion-to-floor height. Mean of sides. The extremity is selected along the fibula's PCA long axis away from the knee, not by a farthest-vertex diameter.

The segment choices explain why even identical subjects would not be expected to produce zero discrepancy. For detailed independently remeasured bone spans, including their own endpoint limitations, see `SKELETON_BONE_CROSS_SEX_COMPARISON.md`.

## Reproducible height equations

All inputs and outputs below are cm. With `h` in the recorded adult support, the adult model is `intercept + slope*h`. These were fitted in the forward direction, never by inverting a stature-from-bone equation:

| Sex | Head | Neck | Torso | Arm | Leg |
|---|---|---|---|---|---|
| Male | 9.519390 + 0.079034 h | 11.2 + (2.3/31)(h−178) | 9.8246268891 + 0.3263433653 h | −1.9444316094 + 0.4588502422 h | −16.9948364970 + 0.6097644466 h |
| Female | 11.177137 + 0.063486 h | 10.8 + (1.9/28)(h−165) | 8.8316779887 + 0.3224152046 h | −3.3344315179 + 0.4654548245 h | −12.6914582511 + 0.5970413779 h |

Definitions and evidence:

* **Head:** exploratory Korean-language paired dataset, 498 men and 500 women. `머리수직길이` translates approximately as vertical head length, but survey provenance, population selection and endpoints are unknown; the 29 cm male value is a possible outlier. Observed heights male 154.7–187.7, female 142.3–172.6. This is not established clinical data; see `HEIGHT_VARIATION_REPORT.md` and `EXPLORATORY_KOREAN_HEAD_FITS.tsv`. Do not confuse head depth with head height.
* **Neck:** [Fichera et al., ESV26-044](https://static.nhtsa.gov/esv/pdf/ESV/Proceedings/28/ESV26-044-Fichera-2026-Neck-260131.pdf) report posterior predicted neck-link means at 5th, 50th, 95th stature (men: 10.1, 11.2, 12.4 at 163, 178, 194; women: 9.8, 10.8, 11.7 at 151, 165, 179), with BMI fixed at 25. The above formulas approximate the reported rounded predictions using the centre and endpoint slope, not the authors' exact Bayesian coefficients. Because BMI is held fixed, the user supplies only height, but the result is conditional on that **assumed BMI**. Source does not clearly define neck-link landmarks and it is not C1–C7 span. Source posterior predictive intervals span centimetres; this is not a precision anatomical value. Our two subjects' actual BMIs are not assumed to be 25. Sex-specific adult reference ranges for interpolation are 163–194 and 151–179 cm.
* **Torso:** ANSUR II adult forward OLS of paired `cervicaleheight - crotchheight`. This is a vertical surface measure, not vertebral chain length or sitting height.
* **Full arm:** ANSUR II adult forward OLS of paired `sleeveoutseam + handlength`: acromion-to-stylion plus stylion-to-middle fingertip. Two linear distances added at the wrist form an **extended-arm approximation**, not a directly observed one-piece arm measurement. Source definitions: [ADAS measurement manual](https://mreed.umtri.umich.edu/mreed/downloads/anthro/ansur/ADAS-Dimension_Definitions.pdf).
* **Full leg:** ANSUR II adult forward OLS of `trochanterionheight`, vertical height of the greater-trochanter landmark above the standing surface. Includes ankle-to-floor, does not start at the hip joint centre. It is a vertical standing measurement, not flexed-leg centreline length.

The three ANSUR fits used 4,081 adult men and 1,985 adult women, unweighted, military population, from `ANSUR_II_PAIRED_MM.csv` (SHA-256 `1b1c0865c328a6aa297a3133f49585eff61002e9208fe3beb183c866c316fbee`). Height ranges are 149.1–199.3 and 140.9–182.9 cm respectively. In-sample residual SDs (not cross-validation or a clinical normal interval) for male torso/arm/leg are 2.27/2.16/2.59 cm and female 2.17/2.20/2.30 cm. This scatter applies to the matched surface measures in that sample, **not** to the skeletal discrepancies above. Formula coefficients are independently refit and checked by the reproduction script.

## Below and above adult range

The code imposes no fixed height cutoff. Outside a formula's observed endpoints it smoothly extends the ratio `value/height` in log-height with matched endpoint value and slope; it remains positive and below height for ordinary finite inputs, but is **not a medically fitted growth curve**. Floating-point overflow/underflow may still cause a rejected estimate. The cervicale/crotch, head, and neck estimates at infant heights are unvalidated extrapolations.

For arm and leg, the code adds near-50-cm anchors from [Chellappan et al.](https://indianpediatrics.net/epub012024/OA-00580.pdf), 39-week sex-specific median individual ratios: full arm 42.86%/42.81% of length; greater-trochanter-to-lateral-malleolus 41.02%/41.50%, male/female. To estimate *floor* rather than ankle, it adds **2.2915/2.1843 cm**, separately extrapolated from adult ANSUR malleolus-height fits to 50 cm. This added ankle height is a modelling assumption, not infant data. A quintic smootherstep blends the newborn proportional approximation into the sex-specific adult model from 50 cm to the first adult observed height; it is continuous with continuous slope, but that whole interval is **not validated on children**. Below 50 cm the anchor ratios continue proportionally, not a fetal model. The newborn cohort and adult sample differ in pose and population; transition heights do not determine age. Chellappan uses recumbent length, adult ANSUR standing stature.

| Stature cm | Sex | Head | Neck | Torso | Arm | Leg |
|---:|---|---:|---:|---:|---:|---:|
| 50 | male | 11.26 | 2.44 | 23.30 | 21.43 | 22.80 |
| 50 | female | 12.26 | 3.12 | 22.59 | 21.41 | 22.93 |
| 100 | male | 16.96 | 5.61 | 41.88 | 43.47 | 45.32 |
| 100 | female | 17.21 | 6.42 | 40.68 | 43.14 | 46.90 |
| 200 | male | 25.31 | 12.83 | 75.09 | 89.83 | 104.96 |
| 200 | female | 23.80 | 13.18 | 73.28 | 89.77 | 106.77 |

All rows above are partially or wholly extrapolated/bridged; the numbers are shown as stress-test outputs, not validated references. The existing separate NHANES child/adult surface curves and their shoulder/bi-iliac breadth estimates are in `CHILD_ADULT_TRANSITION.md` and `GIRDLE_WIDTH_FORMULAS.md`. This five-part profile does not solve the missing height-to-humeral-centre or height-to-CT-hip-centre relationship, nor does it calibrate a surface length into a skeleton's centreline or bone scale. Better age-spanning matched skeletal data are still needed.

## Reproduce

From the repository root:

```sh
c++ -std=c++17 -O2 -I native tools/skeleton-source/openskeleton/31-height-profile.cpp -o /tmp/height-profile
/tmp/height-profile male 170
/tmp/height-profile female 170.2
python3 tools/skeleton-source/openskeleton/31-measure-whole-body.py
c++ -std=c++17 -O2 -Wall -Wextra -Werror -I native tests/height_body_reference.cpp -o /tmp/test-height-profile && /tmp/test-height-profile
```

The measurement script requires NumPy and a C++17 compiler. It refits the ANSUR coefficients to check them, compiles the same C++ profile, remeasures both OBJs/joint files and regenerates `WHOLE_BODY_SKELETON_PROXIES.tsv`. The test sweeps 50–250 cm at 0.001-cm increments for numerical continuity and monotonicity; this verifies the maths, **not anatomical accuracy**.
