# Skeleton source

Two pinned anatomical skeletons, male and female, decimated to a polycount the
game can afford but keeping every bone: skull, mandible and teeth, the whole
spine, ribs, sternum, scapulae, clavicles, pelvis, both arms (humerus, forearm,
carpals, and all five fingers per hand) and both legs (femur, patella,
tibia/fibula, tarsals, metatarsals and toes).

| file | parts | vertices | triangles | height |
| --- | --- | --- | --- | --- |
| `skeleton-male.obj` | 28 | 8,544 | 16,252 | 1.706 m |
| `skeleton-female.obj` | 28 | 7,560 | 14,254 | 1.608 m |

## Canonical height

Measured from the files, not assumed: bone-only height is **170.6 cm** (male)
and **160.8 cm** (female), feet on `y = 0`. Those are the numbers the room
caption prints, read out of the loaded meshes at render time.

Both are the scans' own size and **nothing in this repo scales them**. Living
stature is roughly 2-3 cm more than bone-only height, because articular
cartilage, intervertebral discs and the soft tissue of scalp and heel pad are
not bone: the male reads as a ~173 cm man, the female as a ~163 cm woman. That
is consistent with the source data being adult scans at 1:1, and there is no
evidence of any rescale upstream -- the pelvis fit in
`pipeline/f_build.mjs` is a *similarity* transform (rotation + uniform scale +
translation) that only seats the female pelvis on the male skeleton, and the
male skeleton itself is untouched BodyParts3D geometry.

A rig that needs a taller body gets a taller body, never a scaled one.

Units are metres, feet on `y = 0`, facing +z. Each bone is its own `o` group
with the upstream node name (`skull`, `femur_l`, `hand_r`, `ribs_l`, …), so the
rigging stage binds by name instead of guessing from coordinates.
`skeleton-*.parts.json` carries the per-group triangle counts and the bounding
box.

`reference-*.png` are renders of the two files from this repo's own rasterizer:
front and three-quarter, for eyeballing the decimation.

## Provenance

Derived from the skeleton meshes of **open-anatomy-3d**
(`anatomy-male.glb`, `anatomy-female.glb`), which are built from **BodyParts3D
4.0**, © The Database Center for Life Science (DBCLS), **CC BY 4.0**. The
required credit line and the list of changes are in `ATTRIBUTION.md`.

## Regenerating

```bash
npm i @gltf-transform/core @gltf-transform/extensions meshoptimizer
OPEN_ANATOMY=/path/to/open-anatomy-3d node tools/skeleton-source/prepare.mjs
SEXES=male node tools/skeleton-source/prepare.mjs   # one body only
```

`prepare.mjs` bakes the glTF node transforms into world space, welds coincident
vertices, then simplifies each connected shell separately. That last detail
matters: a hand is 46 separate bones and a jaw holds 32 teeth, so treating a
part as one blob shrank every phalanx to a spike. Per-shell simplification plus
a retry pass against the part's triangle budget keeps fingers and toes
recognisable.
