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
