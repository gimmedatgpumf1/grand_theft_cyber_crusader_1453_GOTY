# Skeleton source

`skeleton-male.obj` and `skeleton-female.obj` are the anatomical reference
bodies: decimated human skeletons with every bone kept — skull, jaw, teeth,
spine, ribs, sternum, scapulae, clavicles, pelvis, both arms (humerus, forearm,
carpals and all five fingers) and both legs (femur, patella, tibia/fibula,
tarsals, metatarsals and toes).

## Where they come from

Both are derived from the skeleton meshes of **open-anatomy-3d**
(https://github.com/MervinR07/open-anatomy-3d: `anatomy-male.glb` and
`anatomy-female.glb`), which are built from **BodyParts3D 4.0**, © The Database
Center for Life Science (DBCLS), published under **CC BY 4.0**.

Credit line, as required by the licence:

> Anatomy: BodyParts3D © The Database Center for Life Science (CC BY 4.0).
> Modified by open-anatomy-3d (https://github.com/MervinR07/open-anatomy-3d),
> CC BY 4.0. Further modified for this project: decimated, flattened into OBJ,
> part node names preserved.

## Nature of each body

`anatomy-male.glb` carries BodyParts3D's scanned male skeleton; the male OBJ
here is that data, decimated only.

`anatomy-female.glb` does **not** carry a scanned female skeleton. Upstream
produces her by taking the male skeleton and deforming it with a
thin-plate-spline fit to female surface landmarks (`pipeline/f_build.mjs`). The
female OBJ here is therefore warped male bone data, presented as the male's
counterpart rather than as female anatomy. Only her pelvic region comes from
female scan data (`VH_F_*`). See `VERIFICATION.md`.

## What was changed

The upstream bodies are ~136,000 triangles each (skeleton only) split across one
glTF node per bone at full anatomical resolution. Here they were:

1. read with the upstream node transforms baked into world space, so the meshes
   are in metres, feet at `y = 0` (male 1.706 m, female 1.608 m);
2. welded on position (0.01 mm) so the simplifier can close shared seams;
3. split into connected shells and simplified per shell — a hand is 46 separate
   bones and a jaw holds 32 teeth, so simplifying a whole part as one blob turns
   every phalanx into a spike;
4. trimmed in retry passes against an explicit per-bone triangle budget, with a
   floor of 12 triangles per shell so no bone disappears;
5. compacted to the vertices that survive and written as one OBJ per body, with
   the upstream part names kept as `o` groups.

| body | vertices | triangles |
| --- | --- | --- |
| male | 8,544 | 16,252 |
| female | 7,560 | 14,254 |

The per-part counts are in `skeleton-male.parts.json` and
`skeleton-female.parts.json`.

## Regenerating

`prepare.mjs` is the exact script that produced these files. It needs Node with
`@gltf-transform/core`, `@gltf-transform/extensions` and `meshoptimizer`, plus a
checkout of open-anatomy-3d:

```bash
npm i @gltf-transform/core @gltf-transform/extensions meshoptimizer
OPEN_ANATOMY=/path/to/open-anatomy-3d node tools/skeleton-source/prepare.mjs
SEXES=male OPEN_ANATOMY=... node tools/skeleton-source/prepare.mjs
```

The output is stable: the decimator is deterministic for a given input, so a
regenerated file differs only in floating point noise.
