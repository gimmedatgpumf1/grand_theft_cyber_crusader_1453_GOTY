# Paired stature, head height and head circumference

`HEAD_MEASUREMENTS_PAIRED.tsv` contains **1,254 individual records** from one
survey. Each row contains three measurements taken from the same person; they
are not averages, predictions, circumference conversions or synthetic points.
The only conversion is millimetres to centimetres (divide by 10).

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
This file is a selected, unit-converted extract of their data and does not imply
endorsement by the authors or PIPER.

## Important limitations

- The study sampled children in the United States in the 1970s. It is a **mixed
  US sample**, not a verified European or Indo-European cohort; individual race
  codes have not been independently decoded here. It may include participants
  of ancestries the user did not request. Do not use this file as an
  ethnicity-specific reference.
- The source uses coded sex; PIPER interprets 1 as male and 2 as female based
  on age/stature relationships rather than a verified codebook. Those codes are
  not included in this three-column export. The 1,254 exported rows comprise
  640 code-1 and 614 code-2 records.
- Stature ranges 81.3–189.6 cm. In the source, positive ages range 2.021–19.246
  years and one exported record has age coded zero/unknown. No infant head
  heights are included. The survey originally collected infants separately;
  this public extract does not pair infant length and these head measurements.
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

The exported TSV has SHA-256
`6f5f0ff43bd3622b510f733523f2d765cc1455fd1ef5c19863e892361c62fe95`.
