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

## C room line view

Press V in the C room to replace both skeleton meshes with source-pose lines;
press V again to return to the mesh view. Pale lines approximate the supplied
bone shapes, pale dots represent the 24 cervical, thoracic and lumbar vertebrae,
cyan lines connect anatomical neighbours, and orange lines are inferred costal
cartilage (not present in the scans). Each palm has one outline for the seven
non-thumb carpals and metacarpals 2–5; the trapezium, thumb metacarpal, and all
finger bones stay separate. Each foot has one outline for the tarsals,
metatarsals and sesamoids; toes remain separate. These are simplified, planar
convex outlines estimated from the source surfaces, not exact bone boundaries.

Both models use the same explicit, connected table of 182 structures and 209
relationships. Internal links within each unified outline are hidden and
external links are retained, leaving 136 displayed shapes and 159 visible links. The table is based on OpenStax's
[thoracic cage](https://openstax.org/books/anatomy-and-physiology-2e/pages/7-4-the-thoracic-cage),
[upper limb](https://openstax.org/books/anatomy-and-physiology/pages/8-2-bones-of-the-upper-limb)
and [lower limb](https://openstax.org/books/anatomy-and-physiology-2e/pages/8-4-bones-of-the-lower-limb)
descriptions. Ribs 1–7 connect via inferred cartilage to the sternum, ribs 8–10
via the preceding rib, and ribs 11–12 have no anterior connection.

The source supplies surfaces, not joint landmarks. Line shapes and link endpoints
are estimates from each surface; the anatomical table chooses the pairs, never
proximity. The static graph does **not** visualize or validate the existing
27-group animation rig, and neither the graph nor the source registration is a
clinical certification. `Head` and `Jaw` are whole source structures, not
individual skull bones. Regenerate and check after rebuilding the OBJs with:

```sh
python3 tools/skeleton-source/openskeleton/04-line-graph.py
python3 tests/skeleton_line_graph.py
```
