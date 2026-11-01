# Paired stature, head height and head circumference

The data are now separated by sex:

- `MALE_HEAD_MEASUREMENTS_PAIRED.tsv`: 640 source sex-code-1 records.
- `FEMALE_HEAD_MEASUREMENTS_PAIRED.tsv`: 614 source sex-code-2 records.

Each row contains three measurements from the **same person** in one survey.
There are no averages, predictions or circumference-to-height conversions. The
only conversion is millimetres to centimetres. These two files preserve all
1,254 records in the earlier combined `HEAD_MEASUREMENTS_PAIRED.tsv`; the
combined file was removed so it cannot be mistaken for a single-sex dataset.

| Column | Measurement |
|---|---|
| `height_cm` | Standing floor-to-vertex stature. |
| `head_height_cm` | Menton (bottom of chin) to vertex, perpendicular to the Frankfurt plane; head upright, jaws closed. This is a living-person surface measure, not skull-bone height. |
| `head_circumference_cm` | Head circumference, not head height. |

Source: Snyder et al. (1977), *Anthropometry of Infants, Children, and Youths
to Age 18 for Product Safety Design*, UM-HSRI-77-17. Data obtained from the
[PIPER transcription of the UMTRI archive](https://github.com/chptx/piper/blob/7961ab0a207dd857470ff207139ece46a1a54931/share/octave/anthropoPerso/dataBase/Dataset_Snyder_1977/snyder.csv).
PIPER credits Christophe Lecomte, University of Southampton, and UCBL-Ifsttar;
its formatted data is licensed [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
See also [their measurement definitions](https://github.com/chptx/piper/blob/7961ab0a207dd857470ff207139ece46a1a54931/share/octave/anthropoPerso/dataBase/Dataset_Snyder_1977/Reference_snyder_withBMI.csv)
and [the survey report](https://mreed.umtri.umich.edu/mreed/downloads/anthro/child/Snyder_1977_Child.pdf).
These files are selected, unit-converted extracts and do not imply endorsement
by the authors or PIPER.

## Important limitations

- The study sampled children in the United States in the 1970s. It is a **mixed
  US population**, not a verified European or Indo-European cohort; individual
  race codes have not been independently decoded here. It may include
  participants of ancestries the user did not request. Do not use these files as
  an ethnicity-specific reference.
- Sex codes are from the individual source records. PIPER's documentation maps
  1 to male and 2 to female based on age/stature relationships, rather than an
  original codebook. As an additional cross-check, an independently labelled
  [CRAN lgrdata Snyder subset](https://github.com/cran/lgrdata/blob/master/data/anthropometry.rda)
  agreed for 1,023 records matched in the same order by age in months, stature,
  and foot length: 508 code 1/male and 515 code 2/female, with no disagreement.
  This supports the mapping but is not an original survey codebook.
- Stature across both files ranges 81.3–189.6 cm. In the source, positive ages
  range 2.021–19.246 years and one exported record has age coded zero/unknown.
  No infant head heights are included. The survey originally collected infants
  separately; this public extract does not pair infant length and these head
  measurements.
- Measurements were divided among subject subgroups, so rows without **all
  three** positive measurements are excluded. Other records, duplicate value
  triples, and potential source anomalies are retained. Repeated value triples
  are different source subject IDs, not generated duplicates.
- No claim of linearity, fitted formula, or extrapolation is made. The older
  `MALE_HEIGHT_DATA.tsv` and `FEMALE_HEIGHT_DATA.tsv` are separate historical
  files, still containing the previously disclosed proxies.

## Reproduction

Download the pinned `snyder.csv` above (SHA-256
`4387d090698f0d015b817dc5992c4881df1473e3bf5ab7a7562ab1c812535ebf`),
then run from the repository root:

```sh
python3 tools/skeleton-source/openskeleton/27-export-head-measurements.py path/to/snyder.csv
```

Output SHA-256:

- Male: `66a8bd9c1d83180b5c5f83b59ad569a31270b1621354d5dbcfdc066b00e79073`
- Female: `71c2fdadb4a6d53d64c94ec9e2a9742eb80b816585cfd4c8769fe8c1b0ab50f2`
