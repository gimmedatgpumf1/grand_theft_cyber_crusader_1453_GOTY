# Head values in the whole-body TSVs

`MALE_HEIGHT_DATA.tsv` and `FEMALE_HEIGHT_DATA.tsv` retain the original eight
columns and every original row. `head_proxy_cm` is now calculated from **that
same row's measured head circumference** (centimetres), using King's formulas:

- Male: `0.5119 * head_circumference_cm - 7.4526`
- Female: `0.4539 * head_circumference_cm - 4.5274`

The values are **estimates**, not individual measured head heights. They are not
the previous Korean-average ratios. A blank means no circumference was present;
no value is invented. The name `head_proxy_cm` remains to preserve the existing
schema and distinguish estimates from direct measurements. The other seven
columns are unchanged, including the other pre-existing child body proxies.
The old runtime skeleton coefficients and formulas are also unchanged.

The TSV has no subject-ID column. The script links each row through the earlier
[21-column export at commit 38935c1](https://github.com/gimmedatgpumf1/grand_theft_cyber_crusader_1453_GOTY/commit/38935c141595338ae59b2407dd89c9d1b1546083)
using row order, then checks the subject ID, sex, stature and circumference
against the corresponding NHANES III or ANSUR II individual source CSV. It does
not join people based on height alone. No subjects from different surveys are
joined; the TSVs do continue to contain rows from multiple US surveys, as before.
These samples are **not verified Indo-European cohorts**.

| Sex | Total rows | Estimated from infant NHANES | Child NHANES | Adult ANSUR II | Total nonblank head estimates |
|---|---:|---:|---:|---:|---:|
| Male | 18,506 | 1,536 | 2,638 | 4,082 | 8,256 |
| Female | 17,644 | 1,518 | 2,727 | 1,986 | 6,231 |

NHANES III adult rows have no head circumference in the source export and thus
remain blank in the head column. Infant height is recumbent length; other
height values are standing stature. The formulas were supplied using the
separate Snyder paired-measurement files, which cover circumference 46.0–60.5
cm for males and 45.2–59.0 cm for females. Applying them outside those ranges,
including to many infants, is **extrapolation**, not validated infant head
height. No clipping or alternate formula is applied.

Source anomaly retained, not corrected: NHANES III infant subject 22183 has
reported recumbent length 39.1 cm and head circumference 40.8 cm, which exceeds
the reported length. Its formula output is 13.991720 cm. The row should not be
used uncritically; its underlying values may be erroneous.

To reproduce from the current repository and its history:

```sh
python3 tools/skeleton-source/openskeleton/28-update-whole-body-head.py
```

The original paired Snyder measurements remain available separately in
`MALE_HEAD_MEASUREMENTS_PAIRED.tsv` and `FEMALE_HEAD_MEASUREMENTS_PAIRED.tsv`;
their `head_height_cm` values are direct measurements, not these estimates.
