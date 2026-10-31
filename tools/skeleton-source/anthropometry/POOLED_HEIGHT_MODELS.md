# Height fits with extra weight at the measured extremes

Each sex and dimension uses one quadratic fitted through **individual child and
adult observations together**. There is no child/adult formula blend. This
revision adds individual infant records where corresponding measurements are
available, increases the influence of underrepresented stature ranges, and
uses explicitly labelled neonatal summary targets. It does **not** turn a
surface-reference model into medically validated bone anatomy.

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

The actual paired records are fitted by weighted least squares with a single
quadratic per sex/dimension. Smoothed inverse-density weights emphasize rare
heights; a separate smooth weight emphasizes the top 10% of adult heights. The
weights affect fitting only: they do not blend age-specific runtime formulas.
Weights are capped so individual outliers do not dominate.

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
0.53 cm; that is an optimization check, not validation against independent
newborns. The formula has no hard height cutoff. It uses one quadratic from
50–250 cm and bounded value/slope/curvature-matched continuations beyond that.
The tallest source subjects are approximately 183–200 cm, depending on sex and
dimension. Therefore 250 cm is **extrapolated**, not an empirically validated
extreme. The 50 cm estimates combine sparse infant records and summary priors;
not every dimension has infant data.

## Tail evaluation and reproducibility

`POOLED_HEIGHT_TAIL_HOLDOUT.tsv` compares the previous unweighted, no-infant
fit with the new fit after the same deterministic identifier-level 80/20 split.
Short means <90 cm; tall means ≥190 cm male or ≥180 cm female. It reports
per-dimension sample counts, bias, MAE and RMSE, not only continuity or the
training error. Equal-weighted mean cell MAE changed **1.560→1.489 cm** over
19 short sex/dimension cells (14 improved), and **3.128→2.966 cm** over 18
tall cells (14 improved). Some cells got worse; several tall cells have only
3–9 test observations and female head and neck have none. Counts across
dimensions reuse subjects. The weights were adjusted after examining this first
holdout, so it is not an untouched final test. `POOLED_HEIGHT_TAIL_REPLICATION.tsv`
uses a different, disjoint fifth of identifiers as test subjects and refits both
models on the other four fifths. Its equal-cell short MAE is **1.535→1.490 cm**
(12/19 improved); tall MAE is **3.813→3.695 cm** (14/18 improved). The two
splits share training subjects and are not two independent surveys. ANSUR adult proxy conversion ratios use the full adult sample, so the
proxy results are not completely independent and none is clinical validation.
`POOLED_HEIGHT_MODELS.tsv` gives all-input counts, model range, summary targets
and in-sample RMSE in the converted proxy units.

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

At 50 cm the requested/fitted torso is **23.23/18.80 cm male** and
**22.73/18.43 cm female**, and requested/fitted leg is **22.52/18.09 cm male**
and **22.75/18.45 cm female**. The rigid adult pelvic ring and mismatched
surface-to-bone proxies cannot realize both requested lengths simultaneously;
the comparison projects them rather than distorting the ring. Skull, C1 and
vertebrae retain source asymmetry; the white source and ragdoll are untouched.
These are unresolved skeletal-fit limitations, not evidence of correct infant
anatomy.

A 50–250 cm motion sweep measured all 90,782 female and 90,703 male vertices
at 0.5 cm steps and 1,092 axis extrema at 0.1 cm steps. Maximum sampled changes
in mesh motion rate were 0.0011 female and 0.0014 male cm/cm². This does not
prove clinical correctness or collision freedom. Selected-joint triangle
contact checks passed at 15 heights per sex; source Sacrum/L5 and some
cranial contacts remain.
