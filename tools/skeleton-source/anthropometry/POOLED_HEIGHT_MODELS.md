# Height fits with extra weight at the measured extremes

These pooled reference models remain in use for the female blue comparison
and other reference APIs. The male blue comparison now uses King's supplied
equations instead; see `COMPARISON_SLIDERS.md`. The male coefficients documented
here are historical pooled-reference coefficients, not its active targets.

Each sex and dimension uses **one quadratic or cubic** fitted through combined
individual child and adult observations. Infant upper-arm records and neonatal
summary targets are included where available. There is no child/adult formula
blend. Surface measurements, converted proxies and adult skeletal meshes are
not the same anatomy; these are not clinically validated pediatric models.

## Exact formulas used

For a height `h` in centimetres between 50 and 250, let `t = (h - 50)/200`.
Every listed dimension, in centimetres, is evaluated as

```
f(h) = c0*(1-t)^3 + 3*c1*t*(1-t)^2 + 3*c2*t^2*(1-t) + c3*t^3
```

There is one coefficient row per sex and dimension, not a fitted childhood
curve switched to an adult curve. Some rows are quadratics represented exactly
in this cubic basis. Coefficients below are rounded for display; the runtime
full-precision runtime values are in `native/client/pooled_height_coefficients.h`;
`POOLED_HEIGHT_MODELS.tsv` also records the selection results.

| Sex | Dimension | c0 | c1 | c2 | c3 | Selected fit |
|---|---|---:|---:|---:|---:|---|
| male | sitting_height | 33.556 | 61.781 | 93.038 | 127.325 | quadratic, tall boost 48 |
| male | upper_arm_surface | 9.509 | 21.877 | 40.814 | 54.561 | cubic, penalty 1 |
| male | upper_leg_surface | 8.076 | 26.948 | 45.190 | 62.804 | quadratic |
| male | shoulder_breadth | 12.318 | 25.527 | 41.506 | 60.254 | quadratic |
| male | pelvis_surface_breadth | 7.917 | 18.861 | 29.982 | 41.280 | quadratic, tall boost 48 |
| male | head | 12.803 | 20.176 | 23.896 | 23.963 | quadratic, tall boost 24 |
| male | neck | 5.804 | 10.025 | 12.169 | 12.236 | quadratic |
| male | torso | 23.228 | 44.656 | 68.444 | 94.591 | quadratic |
| male | arm | 21.182 | 58.442 | 86.510 | 105.386 | quadratic |
| male | leg | 22.519 | 58.161 | 95.009 | 133.063 | quadratic |
| female | sitting_height | 33.449 | 60.513 | 93.201 | 131.514 | quadratic |
| female | upper_arm_surface | 9.494 | 21.672 | 42.069 | 52.480 | cubic, penalty 1 |
| female | upper_leg_surface | 8.148 | 27.165 | 45.215 | 62.297 | quadratic |
| female | shoulder_breadth | 12.293 | 26.337 | 40.928 | 56.066 | quadratic |
| female | pelvis_surface_breadth | 7.866 | 18.680 | 31.538 | 46.440 | quadratic |
| female | head | 12.690 | 19.720 | 23.268 | 23.334 | quadratic, tall boost 24 |
| female | neck | 5.883 | 11.322 | 11.388 | 11.767 | cubic, penalty 1 |
| female | torso | 22.734 | 43.762 | 67.197 | 93.040 | quadratic |
| female | arm | 21.248 | 56.439 | 85.878 | 109.566 | quadratic |
| female | leg | 22.747 | 59.038 | 96.696 | 135.721 | quadratic |

For example, at 50 cm the male head proxy is `c0 = 12.803 cm`; at 250 cm it
is `c3 = 23.963 cm`. The male arm proxy uses `21.182, 58.442, 86.510,
105.386` as its coefficients. Below 50 and above 250, bounded continuations
match the formula's value, slope and curvature; they do not establish medical
validity outside the data. `height_minus_sitting` is exactly `h - sitting`.


## Data and targets

* For sitting height, upper arm, upper leg, biacromial breadth and bicristal
  breadth, the source is NHANES III paired standing stature and surface
  measurements, ages 2–90. Height-minus-sitting is computed as an exact
  complement, not a fitted bone length. For upper-arm length, **3,049**
  additional individual NHANES III measurements at ages 2–<24 months are
  included using recumbent length. The full-arm model uses these individual
  arm measurements only after multiplying by an adult ratio: this is a proxy.
* `NHANES_III_INFANT_PAIRED_CM.csv` contains the infant records: identifier,
  sex, age, recumbent length, upper arm and head circumference. It retains
  source anomalies. The fitter excludes one record whose head circumference
  exceeds its recorded total length; other questionable values are not silently
  relabelled. The infant and standing-length methods are not identical.
  At 50–<55 cm, there are only 2 male and 7 female infant records in the
  original extracted bins, and the survey begins at 2 months, not at birth.
* The other five outputs use ANSUR II adult records and NHANES III child
  records, but **the child measurements do not match the adult landmarks**:

| Output | Child measurement | Adult target / conversion |
|---|---|---|
| Head | head circumference | ANSUR adult head circumference scaled to Korean 20–24 vertex–menton group mean (22.3/21.2 cm male/female). No individual head heights are fitted. Circumference is not head height, and different populations are mixed. |
| Neck | head circumference | Adult `stature - tragiontopofhead - cervicaleheight`; child circumference multiplied by the mean adult paired neck/circumference ratio. This is not a measured child neck length. |
| Torso | sitting height | Adult `cervicaleheight - crotchheight`; child value multiplied by mean adult torso/sitting-height ratio. Sitting height includes the head. |
| Full arm | upper arm | Adult `sleeveoutseam + handlength`; child/infant upper arm multiplied by mean adult full-arm/acromion–radiale ratio. Landmarks differ. |
| Full leg | stature minus sitting height | Adult trochanterion height; child subischial proxy multiplied by mean adult trochanterion/subischial ratio. Landmarks differ. |

The head, neck, torso, arm and leg data substitutions are **not matched
measurements**, nor are the skin landmarks skeletal measurements. The Korean
head-height means come from https://pubmed.ncbi.nlm.nih.gov/34292252/ and are
not paired with the U.S. Army individuals. NHANES is a civilian U.S. survey;
ANSUR II is a different, military adult sample. These cross-sectional samples
are not individual growth trajectories. NHANES sampling weights are not used,
so the resulting fit is not a nationally representative mean.

## Fitting the extremes

Each candidate is fitted by weighted least squares to the combined records.
Smoothed inverse-density weights emphasize underrepresented heights, and a
sigmoid based on the training-adult 90th percentile adds weight toward the tall
end. Inverse-density weight is capped; the separately applied tall multiplier
is bounded, too. Both act during fitting, never as runtime age blends.

For each sex/dimension, training-only cross-validation chooses among the
previous quadratic (tall boost 6), quadratics with boosts 12, 24 and 48, and
single monotonic cubics with a penalty on their third derivative. Shoulder and
arm cubics use stronger minimum regularization because their curvature moves
whole hands; this restriction was informed by actual blue mesh sweeps. A
non-head candidate must lower the inner score by at least 1%; where there are
at least ten extreme-tail validation subjects, it must also improve their MAE
by at least 0.5%. The inner score equally averages squared error in short (<105 cm),
tall (above the training-adult 90th percentile) and overall groups, omitting
empty groups. These are modeling choices, not physiological laws.

Head circumference is a poor proxy for head height. For head only, the
selection also uses the independent sex/age group summaries of **paired**
stature/head-height ratios in `PEDIATRIC_PAIRED_HEAD_BODY_RATIOS.tsv`, rather
than selecting solely for a low circumference-proxy error. Age-matched
height distributions are taken from NHANES, not the ratio study, so this is a
cross-population comparison rather than measured head heights per NHANES child.
A candidate is eligible only if its proxy score is within 20% of the prior
quadratic; selection requires a 1% improvement on the ratio-summary score.
The latter groups are withheld by age in the external audit described below.

Eight dimensions have a **single extra soft loss term** at 50 cm, using these
published group summaries or a clearly identified heuristic. It has weight
3,000 relative to normalized individual-record weights; that choice prioritizes
the requested small extreme but is not a claim that 3,000 newborns had the same
measurement. The target is a ratio of group means, reported ratio, or design
estimate, **not** a directly measured individual at exactly 50 cm:

| Dimension | 50 cm target / source | Limitation |
|---|---|---|
| Sitting | male 33.60, female 33.43 cm | Aziz sex-specific neonatal crown–rump/crown–heel group means; flexed crown–rump is not sitting height. |
| Upper arm | male 9.50, female 9.54 cm | Chellappan 39-week subgroup limb/length median ratios, not 50-cm conditional means. |
| Upper leg | 8.20 cm | Abdel-Rahman pooled mean upper leg 8.3 cm / mean length 50.6 cm; ratio of means, not a matched NHANES joint-to-joint length. |
| Shoulder | 12.40 cm | Pooled neonatal mean biacromial/mean length; population and protocol may differ. |
| Pelvis | 7.94 cm | Pooled neonatal bi-iliac and length group means; not a skeletal hip-centre distance. |
| Head | 12.50 cm | Rough neonatal head/length **one-quarter convention**, not individual vertex–menton data. |
| Full arm | male 21.43, female 21.405 cm | Chellappan 39-week subgroup ratio medians; the surface endpoint differs from the blue skeletal proxy. |
| Full leg | male 22.80, female 22.93 cm | 39-week trochanter–malleolus ratio plus separately extrapolated ankle-to-floor estimate; not an observed neonatal whole-leg measurement. |

Original references and transcriptions: `NEONATAL_BODY_PART_REFERENCES.tsv`,
`NEWBORN_SEGMENT_RATIO_REFERENCES.tsv`, `NEWBORN_SEGMENT_RESEARCH.md`,
https://pmc.ncbi.nlm.nih.gov/articles/PMC5751918/,
https://pmc.ncbi.nlm.nih.gov/articles/PMC9743354/,
https://link.springer.com/article/10.1007/BF02896728 and the approximate head
convention https://brooksidepress.org/ob_newborn_care_2/?page_id=251.
These are not interchangeable populations. There is **no neonatal neck or
cervicale-to-crotch target** here. For those two outputs the previous
50-cm bound based on ratios in the shortest older children remains, and is not
newborn evidence. The head convention is not a clinical head-height dataset.

The current 50 cm formula values differ from these weighted targets by at most
0.31 cm; that is an optimization check, not validation against independent
newborns. There is no hard height cutoff: beyond 50–250 cm, bounded
value/slope/curvature-matched continuations are used.
The tallest source subjects are approximately 183–200 cm, depending on sex and
dimension. Therefore 250 cm is **extrapolated**, not an empirically validated
extreme. The 50 cm estimates combine sparse infant records and summary priors;
not every dimension has infant data.

## Tail evaluation and reproducibility

`POOLED_HEIGHT_NESTED_CV.tsv` compares against the **previous tail-weighted
quadratic**, not an unweighted baseline. In each of five identifier-level
folds, both models are fitted without that fold; model selection is repeated
inside the remaining four, and adult proxy conversion factors are recalculated
without test adults. Short means <90 cm, tall means ≥190 cm male or ≥180 cm
female. Grouping predictions by sex/dimension, pooling folds by subject count,
and then averaging cells equally gives:

| Region | Previous MAE | New MAE | Improved / worsened cells |
|---|---:|---:|---:|
| Short | 1.480 cm | 1.460 cm | 7 / 2 of 20 |
| Tall | 2.372 cm | 2.362 cm | 5 / 4 of 20 |
| All | 1.684 cm | 1.683 cm | 3 / 6 of 20 |

Remaining cells are unchanged. These are **small** average changes, not proof
of better extreme-height anatomy. Some tail cells contain only 3–26 women;
subjects contribute to multiple dimensions. `POOLED_HEIGHT_TAIL_HOLDOUT.tsv`
and `POOLED_HEIGHT_TAIL_REPLICATION.tsv` are fold 0 and fold 1 of the same nested
audit. Every table reports counts, bias, MAE and RMSE per cell. The previous
weights and selection controls were developed after exploratory analyses on
these cohorts, so even nested splits are not an untouched external population.

`POOLED_HEAD_RATIO_CV.tsv` holds out age groups of the separate head-ratio
study during selection. Equal-age mean absolute ratio error changed from
**0.142 to 0.134** male and **0.141 to 0.120** female. These are unitless
stature/head ratios, not centimetres of measured per-person error. NHANES
heights and the ratio study are different cohorts, so this is not a clinical
validation. Some individual age cells and circumference-proxy errors get worse.
The actual ratio study has small subgroups, including only 9 boys and 10 girls
aged 2–<3; there are no paired neonatal or 250-cm head measurements here.
`POOLED_HEIGHT_MODELS.tsv` records coefficients, selection scores, data counts,
50-cm targets and in-sample proxy RMSE.

```
python3 -m pip install numpy pandas scipy zstandard
# Optional: regenerate the individual infant extract from the pinned SHA-256 source:
python3 tools/skeleton-source/openskeleton/44-extract-nhanes-infants.py /path/to/exam.jsonl.zst
OPENBLAS_NUM_THREADS=1 python3 tools/skeleton-source/openskeleton/26-pooled-height-models.py --check
```

The source is the pinned conversion of the CDC examination file at
https://github.com/taw/nhanes-data/tree/db7d052969ea60af364b6524cb51fbadcc9cc2c8/data/nhanes3;
the extractor verifies its SHA-256. Remove `--check` to regenerate model and
holdout files. Positive slope constraints are mathematical, not clinical.

## Blue skeleton geometry

At 50 cm the requested/fitted torso is **23.23/18.87 cm male** and
**22.73/18.47 cm female**, and requested/fitted leg is **22.52/18.16 cm male**
and **22.75/18.49 cm female**. The rigid adult pelvic ring and mismatched
surface-to-bone proxies cannot realize both requested lengths simultaneously;
the comparison projects them rather than distorting the ring. Skull, C1 and
vertebrae retain source asymmetry; the white source and ragdoll are untouched.
These are unresolved skeletal-fit limitations, not evidence of correct infant
anatomy.

A 50–250 cm motion sweep measured all 90,782 female and 90,703 male vertices
at 0.5 cm steps and 1,092 axis extrema at 0.1 cm steps. Maximum sampled changes
in mesh motion rate were 0.0014 female and 0.0014 male cm/cm². This does not
prove clinical correctness or collision freedom. Selected-joint triangle
contact checks passed at 15 heights per sex; source Sacrum/L5 and some
cranial contacts remain.
