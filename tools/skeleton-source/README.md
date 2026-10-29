# Skeleton source

The C room uses two CT-derived, registered OpenSkeleton subjects from the
[Zenodo dataset](https://zenodo.org/records/22833546), CC BY 4.0:

- Male `N197925`: `skeleton-male.obj`.
- Female `N186416`: `skeleton-female.obj`.

The included NPZ and JSON files contain the extracted source vertices and
faces for each subject. Each OBJ retains all 182 source structure names in
`g` groups and 27 runtime `o` groups. The rigid runtime groups are not 182
independently animated bones. Each source structure is simplified separately
into an overall budget near 180,000 triangles. No bones are mirrored, scaled
or repositioned individually. A single coordinate conversion and translation
places each body in the game. The female is held still; the male uses the
existing group-based idle animation.

Both source files contain *registered* meshes, not necessarily the original CT
patient pose. Our source-fidelity tests check names, face indices and the
proximity of every simplified vertex to its corresponding source structure;
they do not certify the anatomy, registration, assembly or source labels.
The male source has 182 labels including `Head` and `Jaw` as whole structures;
separate teeth or skull-bone labels are not provided in this dataset.

## Rebuild and check

Python, NumPy and `fast-simplification` are required for regeneration:

```sh
python3 tools/skeleton-source/openskeleton/03-import.py
python3 tools/skeleton-source/openskeleton/03-import.py --male
python3 tests/openskeleton_source_pose.py female
python3 tests/openskeleton_source_pose.py male
```

See `ATTRIBUTION.md` for attribution and `openskeleton/README.md` for the
original source files and extraction. The reference PNGs are historical and do
not show these current models. The Z-Anatomy importer remains for historical
reproducibility; its output is not the currently shipped male.
