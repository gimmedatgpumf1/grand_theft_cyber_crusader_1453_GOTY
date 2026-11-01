# Hand, foot and vertebral bone measurements

These are additional **measured skeletal references**, not surface proxies, and do not implement or validate scaling in the game. They do not contain individual stature measurements, so none establishes how these bones change with stature. The cohorts, landmarks and image methods differ; do not combine their averages into an individual or present a ratio of group averages as a mean individual ratio. Age and stature are not interchangeable.

## Bilateral hand bones: Egyptian radiographs

[El Morsi and Al Hawary, *Journal of Forensic and Legal Medicine*, doi:10.1016/j.jflm.2012.04.020](https://doi.org/10.1016/j.jflm.2012.04.020), [readable source, Table 2](https://www.researchgate.net/publication/233875490_Sex_determination_by_the_length_of_metacarpals_and_phalanges_X-ray_study_on_Egyptian_population): 100 Egyptian hospital radiograph patients (50 men, 50 women); reported male ages 17–53, female 17–65. Skeletally immature and visibly pathological bones were excluded. Both hands were radiographed; **base midpoint to tip midpoint** on each metacarpal and phalanx. Hand positioning and 2D projection can affect lengths.

`HAND_EGYPTIAN_XRAY_LENGTHS.tsv` transcribes 76 published sex- and side-specific mean/SD lengths in mm. Each side group contains 50 subjects, *not* 100 independent people; right and left belong to the same people. No middle phalanx is listed for the thumb. Examples:

| Bone | Side | Male mean ± SD mm | Female mean ± SD mm |
|---|---|---:|---:|
| Middle-finger proximal phalanx | right | 46.72 ± 2.77 | 42.76 ± 2.59 |
| Middle-finger proximal phalanx | left | 46.98 ± 2.79 | 42.94 ± 2.60 |
| Index metacarpal | right | 71.58 ± 3.93 | 65.34 ± 3.43 |
| Thumb distal phalanx | right | 23.14 ± 1.81 | 20.32 ± 1.46 |

These are reference distributions in one selected population, **not** bone-to-stature ratios, not joint-centre distances, and not validation of the meshes' current bone lengths. The paper reports some implausible ranges for a few female entries; those ranges were not transcribed. Its text and side-comparison table also disagree on significance of one comparison; we do not reproduce those inference claims.

## Forefoot bones: Indian radiographs

[Patil et al., *Journal of Clinical and Diagnostic Research* (2017), doi:10.7860/JCDR/2017/27887.10589](https://www.jcdr.net/ReadXMLFile.aspx?id=10589), Tables 2–7: 60 healthy adults sampled at a teaching institute in Karnataka, India; 33 men aged 21–36 and 27 women aged 21–37. Anteroposterior radiographs of **both** feet: metatarsal length is distal head to midpoint of basal articular surface; fifth metatarsal excludes the styloid. Phalanx lengths are between midpoints of articular surfaces. Radiographic lengths can differ from maximum bone lengths.

- `FOOT_INDIAN_XRAY_LENGTHS.tsv`: 38 sex-specific length mean/SD summaries, mm. The article does not clearly specify how the bilateral measurements were aggregated in each table; side is therefore `unspecified_bilateral_summary`, not left, right or independent feet. `subjects_n` is people, **not** a claim of independent observations per table cell.
- `FOOT_INDIAN_XRAY_CORRELATIONS.tsv`: 72 published within-class correlations, showing co-variation of the five metatarsals or of the phalanges of the same type. They are **not** length ratios, stature correlations or across-study correlations. The paper includes questionable range entries and p-values; we have not transcribed those and cannot independently verify its correlation computation or bilateral aggregation. No standard error is inferred from n.

For example second metatarsal mean ± SD: male **67.61 ± 3.49 mm**, female **62.46 ± 3.45 mm**; fifth proximal toe phalanx: male **21.3 ± 1.54 mm**, female **19.3 ± 1.31 mm**. The published within-sample correlation between second and third metatarsal lengths is 0.850 male and 0.903 female; high correlation does **not** prove a constant ratio, and cannot support extrapolation to different heights or pediatric feet.

## Thoracic and lumbar vertebral bodies: German CT cohort

[AI-based CT assessment of 3117 vertebrae, *Scientific Reports* (2025), doi:10.1038/s41598-025-05091-0](https://pmc.ncbi.nlm.nih.gov/articles/PMC12218285/), Tables 2 and 4: retrospective Heidelberg clinical CT cohort, 262 adults (129 male, 133 female), 20–54 years; visually pathological or abnormal thoracolumbar spines excluded. 3,117 vertebrae initially assessed; 2,925 retained after incomplete/incorrect segmentations excluded. Coverage varies by scan; sex-specific **cohort n is not the number of vertebrae observed at each level**. Population ethnicity is not specified.

`SPINE_ADULT_CT_LEVELS.tsv` gives 34 sex/level rows T1–T12 and L1–L5: the table's published mean vertebral height and mean individual anterior/posterior and central/posterior body-height ratios. Ha is anterior, Hp posterior and Hc central, measured in the midsagittal vertebral body—not the total length of the whole vertebra or an inter-joint spacing. Table 2 does not clearly label which of Ha/Hc/Hp its aggregate mean refers to, so the file deliberately calls it `published_mean_height_mm` rather than choosing a landmark. No SD is supplied for these table-2 means or table-4 ratios; leave SD unknown. Disc heights and cervical vertebrae are absent.

For example, the L5 mean individual anterior/posterior ratio is **1.20 male**, **1.19 female**; T1 is **0.93** and **0.94**. This describes vertebral body shape rather than a stature scaling formula. The difference between sexes in mean shape is not proof that individual anatomy is the same. The paper's reported cohort mean ages differ slightly between text and Table 1; the age range is retained rather than reconciling them.

## What remains unresolved

- No individual hand, foot or vertebral length paired with stature in the above tables; do not turn an age or cohort mean into a height fit. Do not compare the TSV values to PCA mesh spans as if the landmarks were equivalent.
- Paired child crown–chin/stature data still need access and verification. A secondary project's [NIST-derived age summaries](https://github.com/samchon/automovie/blob/27daf1cc134cefd450324e77f482552735722fa2/packages/human/docs/sources/snyder-1977.md) report paired values, but cannot substitute for verified original records and are not incorporated as primary data here.
- The [Carman pediatric CT measurements](https://simtk.org/frs/?group_id=2210) remain unavailable here. An [independent fetch script](https://github.com/ghassenzaara/AutoImplants/blob/HEAD/src/fetch/fetch_paed_ssm.sh) reports that the SimTK download requires an account; its authenticated download was not verified by that project, and we have not obtained the archive or verified its licence.
- Missing direct pediatric/sex/height-stratified hand, foot, spine and individual rib measurements, very short/tall adult coverage, and geometry/joint-centre validation remain essential. For the present project see [the research checklist](SCALING_DATA_CHECKLIST.md).

Recreate the four transcriptions with `python3 tools/skeleton-source/openskeleton/21-small-bone-references.py`. No source patient records or article images are redistributed. Data summaries retain attribution; review source permissions before redistribution or commercial use.
