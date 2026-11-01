# Skeleton attribution

Both current room skeletons use **OpenSkeleton: Open, Sex-Specific Full-Body
Skeletal dataset for Statistical Shape Modeling**, by Kate Duquesne, BioCAT,
Thibau Destryker, Veerle Segers and Emmanuel Audenaert, Ghent University:
[10.5281/zenodo.22833546](https://doi.org/10.5281/zenodo.22833546),
**CC BY 4.0**. The authors derived the dataset from the New Mexico Decedent
Image Database. The current subjects are N197925 (male) and N186416 (female).

The repository includes one extracted registered mesh set per subject, not the
entire 2.5 GB combined dataset. The game assets convert axes and millimetres to
metres, translate each whole subject, simplify each structure, and group them
for the game. These modifications are not clinical validation. Registered
shapes and source labels are not guaranteed to reconstruct a subject's original
patient pose or an anatomically correct assembly. See `README.md` and
`VERIFICATION.md` for checks and limitations.

## Historical male source

The former male was based on Z-Anatomy, derived from **BodyParts3D**, © The
Database Center for Life Science, CC BY-SA 2.1 Japan, and Z-Anatomy,
CC BY-SA 4.0. The old importer and historical reference images may still be
present for reproducibility; that male model is no longer used in the room.
