# Skeleton source

`skeleton.obj` is the anatomical reference body: a decimated human skeleton with
skull, jaw, teeth, spine, ribs, sternum, scapulae, clavicles, pelvis, both arms
(humerus, forearm, full hand and every finger) and both legs (femur, patella,
tibia/fibula, foot and toes).

## Where it comes from

It is derived from the skeleton meshes of **open-anatomy-3d**
(https://github.com/MervinR07/open-anatomy-3d, `anatomy-male.glb`), which in turn
is built from **BodyParts3D 4.0**, © The Database Center for Life Science
(DBCLS), published under **CC BY 4.0**.

Credit line, as required by the licence:

> Anatomy: BodyParts3D © The Database Center for Life Science (CC BY 4.0).
> Modified by open-anatomy-3d (https://github.com/MervinR07/open-anatomy-3d),
> CC BY 4.0. Further modified for this project: decimated, flattened into a
> single OBJ, part node names preserved.

Changes made here are described below; the licence requires that they are
indicated, and they are.

## What was changed

The upstream body is ~136,000 triangles at full resolution and split across one
glTF node per bone. For this project it was:

1. read with the upstream node transforms baked into world space, so the mesh is
   in metres with the feet at `y = 0` and the figure 1.71 m tall;
2. welded on position (0.01 mm) so the decimator could collapse shared seams;
3. decimated per bone to an explicit triangle budget — the skull keeps its
   orbits, sinuses and jaw line, while the long bones are allowed to collapse to
   a handful of quads;
4. compacted to only the vertices that survive, and written as one OBJ with the
   upstream part names kept as `o` groups, so the next stage can bind bones to
   named parts instead of guessing from coordinates.

Result: 2,862 triangles, 1,816 vertices. The per-part counts are in
`skeleton.parts.json`.

## Regenerating

`prepare.mjs` is the exact script that produced the files. It needs Node with
`@gltf-transform/core`, `@gltf-transform/extensions`, `meshoptimizer`, and a
checkout of open-anatomy-3d at `OPEN_ANATOMY`:

```bash
npm i @gltf-transform/core @gltf-transform/extensions meshoptimizer
OPEN_ANATOMY=/path/to/open-anatomy-3d node tools/skeleton-source/prepare.mjs
```

The output is stable: the decimator is deterministic for a given input, so a
regenerated file should differ only in floating point noise.
