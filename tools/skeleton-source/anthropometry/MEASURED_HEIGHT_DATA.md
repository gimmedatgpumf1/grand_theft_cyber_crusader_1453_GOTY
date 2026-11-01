# Measured-height export: incomplete Slovak excerpt

**These two TSVs replace the old mixed-population/proxy exports. They are not a
complete seven-dimension dataset and are not sufficient to fit all seven
formulas.** Blank cells are missing measurements, not zero. No Korean data,
circumference-to-height conversion, model prediction, synthetic row, or
population-mean-to-individual conversion is used in these TSVs.

## What is included

Each row is one person. Both nonblank values come from that same row of
Hitka, Gejdoš and Sydor's [Slovak anthropometry dataset, version 3](https://data.mendeley.com/datasets/2kjkw962gd/3),
`Table 2.xlsx`, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
The source workbook SHA-256 is
`a92af0f64164c754bb3888cdb94cd6484134808dc532385371126066b338cbb2`.
Credit: Miloš Hitka, Miloš Gejdoš and Maciej Sydor (2024), DOI
[10.17632/2kjkw962gd.3](https://doi.org/10.17632/2kjkw962gd.3). This is a
transcribed and filtered extract; the authors do not endorse this project.
[Associated paper and dimension descriptions](https://pmc.ncbi.nlm.nih.gov/articles/PMC11214164/).

The checked-in extract is deliberately **small and non-random**: 29 consecutive
male records from the start of the 2003 data and 44 selected female records
from 2015, ages 18–25. It is not representative and cannot characterize
extreme stature. `SLOVAK_EXCERPT.csv` records year, sex, age, weight, height and
breadth for traceability; the two output TSVs retain only the requested eight
columns. Two severely corrupted female records within the excerpted section
were omitted (including one reporting 102 cm shoulder breadth at 160 cm
stature); no other quality assurance of source data is implied.

The source reports measurements at Slovak universities, and its paper discusses
the Slovak adult population. Nationality, ethnicity, ancestry and first language
of each person are not provided; this is not proof that every participant was
ethnically Slovak or an Indo-European-language speaker. The full workbook includes
ages outside the paper's 18–25 description; this export filters to ages 18–25.

| TSV field | Meaning |
|---|---|
| `height_cm` | Standing stature, floor to vertex, in cm. |
| `shoulder_breadth_source_cm` | Source's directly recorded “Shoulder (bideltoid) breadth”, cm. The paper describes the measure as between acromiale points, despite calling it bideltoid. These definitions conflict; it cannot safely be asserted to equal biacromial breadth, skeletal shoulder-joint spacing, or scapula span. |
| `head_cm`, `neck_cm`, `torso_cm`, `arm_cm`, `leg_cm`, `pelvis_cm` | Blank: the workbook does not contain the requested matching measurements. Sitting height, arm segments, reach and hip/leg alternatives are not substituted. |

There are **no measured head heights, infants, or young children** in the
exports. Do not interpret them as evidence for newborn or extreme-height curves.
The paper and source show apparent data-entry/measurement anomalies; even the
remaining values (including unusually large shoulder widths) need independent
quality review. The column is retained under the source's name, not presented
as an exact skeleton landmark measurement.

## Reproduce and obtain more rows

With Python 3, run the following to reproduce the checked-in excerpt:

```sh
python3 tools/skeleton-source/openskeleton/27-export-measured-height-data.py
```

For the larger dataset, download **Table 2.xlsx** from the version 3 Mendeley
page above and install `openpyxl`, then run:

```sh
python3 tools/skeleton-source/openskeleton/27-export-measured-height-data.py --xlsx 'Table 2.xlsx'
```

The script checks the workbook SHA-256 and header names, reads the first sheet,
keeps M/F rows ages 18–25 with positive stature and a recorded shoulder value
between 15 and 70 cm, and writes both TSVs. The 15–70 cm screen is only an
obvious-error filter, not medical validation or a correction. People without a
usable shoulder value are omitted because they contribute no fitting target.
No duplicate removal or imputation is performed. The larger export has not been
verified here against the full workbook: the workbook was not downloadable into
this environment; only the online preview was available. Review it before fitting.

## Runtime warning

These exports do **not** change existing game formulas. The female pooled
runtime model and other historical coefficients still derive in part from the
previous mixed-population and proxy data, including a Korean-calibrated head
conversion. King's male formulas were fitted externally from the earlier TSVs,
which also contained proxies. Neither set should be treated as fitted to these
new measured data. See `POOLED_HEIGHT_MODELS.md` and `COMPARISON_SLIDERS.md`
for the historical implementation.
