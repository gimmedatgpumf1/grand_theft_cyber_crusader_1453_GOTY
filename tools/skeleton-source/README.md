# Skeleton source

The test room uses `skeleton-male.obj` (Z-Anatomy) and `skeleton-female.obj`
(OpenSkeleton subject N186416). See `ATTRIBUTION.md` for sources and licences.

The female OBJ contains all **182 labels present in the supplied source** as
OBJ `g` groups. The renderer uses 27 `o` rig groups: it does not yet animate or
select all 182 bones independently. The label-to-rig-group table and per-bone
counts are recorded in `skeleton-female.parts.json`. Matching source labels is
not proof that each label is anatomically correct or that the source is complete.

Her current pose is the source's **registered coordinates**, not a reconstructed
CT scan pose. The importer applies one common axis/unit conversion and one common
translation; it does not repose, mirror or individually move bones. Each named
bone is independently simplified, as before, to an overall budget of roughly
180,000 triangles. The test room holds her still in that pose; the male still
animates. The restored source has visibly asymmetric, disconnected-looking
areas. These are not validated as correct anatomy and are not automatically fixed.

Rebuild the female asset from the committed source (requires Python, NumPy and
`fast-simplification`):

```bash
python3 tools/skeleton-source/openskeleton/03-import.py
python3 tests/female_source_pose.py
```

The source check independently verifies the 182 source names, their rig mappings,
OBJ face ranges and each simplified vertex's proximity to its corresponding
source bone under a single common transform. It does not certify source labels,
anatomical placement or the scanner's original patient pose. The source is
registered template geometry, not an untouched CT reconstruction.

The standing-pose osteometric verifier remains available:

```bash
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-male.obj
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-female.obj --sex female --scanned
```

The female **does not pass** this standing/symmetry-based check in her restored
source arrangement. It reports eight failures: foot proportions and large
left/right differences. Some checks are pose-dependent; the failures do not
prove the precise cause. The normal test suite checks female source fidelity
instead, not a false anatomical pass. `VERIFICATION.md` has the details.

The male's maxillae, nasal bones and vomer were previously omitted by the
importer's name filter. They are restored from the Z-Anatomy FBX; the right
maxilla and nasal bone are mirrored from the left because the FBX supplies only
those left-hand meshes. They remain part of the existing skull rig group, not
independently animated bones. The OBJ is simplified, and this restoration is
not a clinical assessment of the source skull. The other 27 rig-group meshes
are unchanged. `tests/male_skull_restoration.py` checks source-derived landmarks
on actual exported skull faces, and `tests/male_skull_classification.mjs` checks
the source names used by the importer.

The `reference-*.png` images predate these restorations and are not evidence of
the current assets. The male can be regenerated with `prepare-zanatomy.mjs`;
its source FBX is not included here.
