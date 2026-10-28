# Skeleton source

`skeleton-male.obj` is the anatomical reference body: a real human skeleton with
every bone kept — skull, jaw, teeth, ossicles, all 24 vertebrae, ribs and costal
cartilages, sternum, scapulae, clavicles, pelvis and sacrum, both arms (humerus,
radius, ulna, all eight carpals, five metacarpals, every finger phalanx) and both
legs (femur, patella, tibia, fibula, tarsals, metatarsals, every toe phalanx).

`skeleton-female.obj` is **not** a female skeleton. Read the last section before
using it for anything.

## Where they come from

The male comes from **Z-Anatomy** (https://github.com/LluisV/Z-Anatomy), the
libre 3D atlas of anatomy, published under **CC BY-SA 4.0**. The models in that
project are themselves derived from **BodyParts3D**, © The Database Center for
Life Science (DBCLS), **CC BY-SA 2.1 Japan**, and the Z-Anatomy readme says so.

The file used is `Assets/Models/1.0 Models/SkeletalSystem100.fbx` from the
sample project (https://github.com/LluisV/Z-Anatomy-Sample), a binary FBX 7400
holding 1,851 named meshes, 721,832 vertices and 1,069,941 triangles.

Credit lines, as the licences require:

> Anatomy: BodyParts3D © The Database Center for Life Science (CC BY-SA 2.1
> Japan). Z-Anatomy — the libre 3D atlas of anatomy — CC BY-SA 4.0.
> Further modified for this project: filtered to the skeletal system, mirrored
> to a complete body, grouped into 28 rig parts, decimated, written as OBJ.

The female is still the old `open-anatomy-3d` mesh and carries its own credit
line, given in the section at the end.

This asset — the modified skeleton and any derivative of it — has to stay under
CC BY-SA 4.0 when it is redistributed. That is a share-alike obligation inherited
from the source, and it is the price of using real anatomy.

## What the transform does, and what it does not

Exactly two operations are applied to the source coordinates:

1. **× 0.01**, because Z-Anatomy is in centimetres and the game is in metres.
2. **a translation in y**, so that the lowest bone vertex sits on `y = 0`.

There is no scale-to-fit, no normalisation, and no target height anywhere in the
pipeline. The output height is whatever the source body is: **1.6958 m**. If a
future rig wants a different stature it must come from different source data,
never from a scale factor.

## What was changed

The FBX stores each bone as its own object, in a half-body layout: left-hand
bones carry a `.l` suffix and most have **no right-hand twin** in the file. Muscle
markings (`.i`, `.j`, `.o1l`, `.e1r`, …) and the cross-section helpers are
sub-meshes of bones and are dropped. Then:

1. skeletal objects only — 177 of them, the rest of the file is muscle
   attachment geometry;
2. every `.l` object whose `.r` twin is absent is mirrored through `x = 0` with
   reversed winding — 99 of them, giving 272 bones in a complete body;
3. per-object transforms (`Lcl Translation`/`Rotation`/`Scaling`, which include a
   −1/3 uniform scale, and the node hierarchy) are composed into world space;
4. bones are grouped into the 28 parts the renderer and rig already use
   (`skull`, `femur_l`, `hand_r`, …), keeping the source name of every bone in
   `skeleton-male.parts.json`;
5. each *bone* is welded and simplified on its own towards the part's triangle
   budget, with a floor of 12 triangles per bone so no phalanx disappears.

| body | parts | bones | vertices | triangles | height |
| --- | --- | --- | --- | --- | --- |
| male | 28 | 272 | 9,739 | 18,699 | 1.696 m |
| female | 28 | — | 7,560 | 14,254 | 1.608 m |

Mirroring is the one compromise: the right half of this skeleton is an exact
mirror of the left, so it is symmetric to the micron. Real people are not — a
left and right femur differ by a few millimetres. The alternative in this file
was no right arm at all.

## Regenerating

```bash
npm i meshoptimizer --prefix tools/skeleton-source
node tools/skeleton-source/prepare-zanatomy.mjs /path/to/SkeletalSystem100.fbx \
     tools/skeleton-source/skeleton-male.obj
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-male.obj
```

`prepare.mjs` (the older, open-anatomy-3d route) still regenerates the female
file; `verify.mjs` reproduces the measurements of that body against its source.

## The female is not female anatomy

`anatomy-female.glb` upstream does not contain a scanned female skeleton. It is
built by taking the male skeleton and deforming it with a thin-plate-spline fit
to female *surface* landmarks (`pipeline/f_build.mjs`): only the illum, femur and
fifth lumbar vertebra come from female scan data (`VH_F_*`). The result has a
plausible pelvis and male proportions everywhere else, and it loses left/right
symmetry in the process — her scapulae differ by 17.8 mm.

Z-Anatomy cannot replace her: its models are BodyParts3D, and BodyParts3D is "a
three-dimensional whole-body model for an adult human male" (DBCLS database
description). There is no female body in that lineage at all.

`VERIFICATION.md` has the measurements and `verify-obj.mjs` reproduces them from
the shipped file.
