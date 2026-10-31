# Pooled height models used by the blue comparison

Each sex and dimension now has **one quadratic fitted simultaneously to individual
child and adult records**. There are no separate age-group curves, 5 cm bin
means, child/adult crossfades, spline approximations or infant bridges in the
active 50–250 cm calculation. The raw records and generated coefficients are
in this repository; `26-pooled-height-models.py` regenerates the coefficients
and `POOLED_HEIGHT_MODELS.tsv`. This is an exploratory visual approximation,
not medical validation or a model of an individual child's growth.

## What was actually fitted

For sitting height, upper arm, upper leg, biacromial breadth and bicristal
breadth, the input pairs are unweighted **individual NHANES III** stature and
the corresponding measurement, with all eligible children (<18 years) and
adults (>=18 years) pooled by recorded sex. Height records flagged by NHANES,
missing ages, missing measurements and invalid pairs are excluded. Each
remaining record has equal weight, not the NHANES survey weight. The sixth
quantity, stature-minus-sitting-height, is exactly the mathematical complement
of the fitted sitting height, not an independently measured bone length.

The other five references also fit one pooled formula to individual child and
adult records, **but their child and adult inputs are not matched landmarks**.
The repository has no directly matching individual child measurements for
these five adult surface dimensions. We use the following intentionally rough
substitutions; the scaled child values are proxies, not observed measurements
of the named target:

| Output | NHANES child measurement used | ANSUR II adult input and conversion |
|---|---|---|
| Head | head circumference | Adult head circumference scaled so its sex-specific mean equals the Korean 20–24 vertex–menton group mean (male 22.3, female 21.2 cm); the same scaling is applied to the child circumference. Circumference is **not** head height, and the Korean study and U.S. Army are different populations. No individual head-height values are fitted. |
| Neck | head circumference | Adult `stature - tragiontopofhead - cervicaleheight`; child circumference multiplied by the adult mean paired ratio of that dimension to head circumference. Head circumference is **not** neck length. |
| Torso | sitting height | Adult `cervicaleheight - crotchheight`; child sitting height multiplied by the adult mean paired torso/sitting-height ratio. Sitting height includes the head. |
| Full arm | NHANES upper arm | Adult `sleeveoutseam + handlength`; child upper arm multiplied by the adult mean paired full-arm/acromion-to-radiale ratio. These upper-arm landmark definitions are not identical. |
| Full leg | `stature - sitting height` | Adult trochanterion height; child subischial surface distance multiplied by the adult mean paired trochanterion/subischial ratio. These are not the same landmarks. |

ANSUR is an adult U.S. Army sample, not the NHANES population. These
conversions are deliberately approximate and can be badly wrong, especially
for babies. The Korean group mean is an external calibration, not paired with
the ANSUR individuals and not a pediatric measurement. See
https://pubmed.ncbi.nlm.nih.gov/34292252/. All outputs are skin-surface
proxies; even genuinely matching skin measurements are not bone dimensions.
The requested and fitted skeletal torso/leg proxies can differ because the
intact pelvic ring cannot in general match both simultaneously. At 50 cm,
requested/fitted torso is 23.23/20.25 cm for male and 22.73/19.75 cm for
female; requested/fitted leg is 20.44/17.47 cm for male and 20.84/17.85 cm
for female. These are geometric proxy measurements, not verified infant body
dimensions. Skull, C1 and vertebrae retain source asymmetry; white source and
ragdoll poses are untouched.

## Reproduction and extrapolation

```
python3 -m pip install numpy pandas scipy
OPENBLAS_NUM_THREADS=1 python3 tools/skeleton-source/openskeleton/26-pooled-height-models.py --check
```

Remove `--check` to regenerate. The generator verifies both input SHA-256s. It
uses bounded least squares on every valid paired record, with three quadratic
Bernstein coefficients per curve over 50–250 cm. The generated C++ coefficients
are the mathematically equivalent degree-elevated cubic representation. Positive coefficient increments (minimum
slope 0.001) are a **mathematical shape constraint**, not a clinical limit.
At 50 cm the first coefficient is constrained to within 10% of the mean
dimension/height ratio in the shortest 10 cm of the available child sample.
This is an extrapolation assumption based on older children, **not** a
newborn observation or a fitted newborn cohort; there are no records near
50 cm in these data. The 50 cm lower constraint and the extrapolation
continuations replace the former neonatal anchors, so prior exact 50 cm values
no longer apply. The reported RMSE is **in-sample**, in the converted proxy
space where conversions were used, not holdout error or clinical uncertainty.
Counts, data ranges and conversion factors are in `POOLED_HEIGHT_MODELS.tsv`.

For positive heights outside 50–250 cm, C2-matched continuations provide
finite, bounded visual estimates, with no minimum or maximum height cutoffs.
At the low end the fraction tends to the 50 cm fraction; at the high end a
bounded positive asymptotic slope is used. This is not evidence for extreme
heights. A single quadratic is evaluated throughout 50–250 cm, **including outside
the actual raw data ranges**. Polynomial smoothness does not establish medical
accuracy, symmetric anatomy or collision freedom.

Over 100–190 cm the absolute polynomial curvature (cm/cm²) for the
female/male targets is: shoulder 0.00027/0.00011, pelvis 0.00046/0.00041,
head 0.00063/0.00062, neck 0.00031/0.00030, torso 0.00045/0.00045,
arm 0.00109/0.00158 and leg 0.00043/0.00043. A quadratic's curvature is
constant; this statistic does not establish mesh correctness.

A 0.1 cm sweep of 1,092 axis-extreme transformed blue vertices per sex over
50–250 cm measured peak motion-rate changes of 0.0014 female and 0.0017 male
cm/cm². The previous spline/blend measured 0.0093 and 0.0104 respectively.
A second 0.5 cm sweep covered all 90,782 female and 90,703 male mesh vertices;
the corresponding maxima were also 0.0014 and 0.0017 cm/cm². The sweeps do not
prove collision freedom or guarantee perceived smoothness. The selected-joint
FCL audit covers 15 heights per sex; at 250 cm the female pelvic halves are
3.04 mm apart, with no triangle intersection. The ring-gap tolerance scales
with stature for tall copies. Source Sacrum/L5 and some cranial contacts remain. See `COMPARISON_SLIDERS.md` for the rigid-ring projection and
other geometric limits.
