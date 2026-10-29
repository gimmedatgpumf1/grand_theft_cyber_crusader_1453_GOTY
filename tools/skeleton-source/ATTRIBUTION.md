# Skeleton source

`skeleton-male.obj` is the anatomical reference body: a real human skeleton with
the selected bones — skull, jaw, teeth, ossicles, all 24 vertebrae, ribs and costal
cartilages, sternum, scapulae, clavicles, pelvis and sacrum, both arms (humerus,
radius, ulna, all eight carpals, five metacarpals, every finger phalanx) and both
legs (femur, patella, tibia, fibula, tarsals, metatarsals, every toe phalanx).

`skeleton-female.obj` comes from **OpenSkeleton** — the full-body CT bone
segmentations published by Duquesne, Destryker, Segers and Audenaert (Ghent
University) from the New Mexico Decedent Image Database — under **CC BY 4.0**,
subject `N186416`. She is a different person from the male and a different body
from the older Visible Human Female build kept in `vhf/`. Her source labels and
registered pose are checked by `tests/female_source_pose.py`; anatomical
correctness is not certified.

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

The current female comes from OpenSkeleton; the Visible Human data mentioned
below describes an earlier, replaced asset.

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

1. selected skeletal objects only; muscle attachment geometry, helpers, nasal
   cartilage and laryngeal cartilages are excluded (the hyoid bone remains);
2. every `.l` object whose `.r` twin is absent is mirrored through `x = 0` with
   reversed winding — 98 of them, giving 266 named objects after mirroring;
3. per-object transforms (`Lcl Translation`/`Rotation`/`Scaling`, which include a
   −1/3 uniform scale, and the node hierarchy) are composed into world space;
4. bones are grouped into the 28 parts the renderer and rig already use
   (`skull`, `femur_l`, `hand_r`, …), keeping the source name of every selected
   bone in `skeleton-male.parts.json`. Maxillae, nasal bones and vomer were
   previously excluded by a name-filter bug and have now been restored;
5. each rig part is welded, then its disconnected mesh shells are simplified
   separately towards the part's triangle budget, with a 40-triangle floor per
   shell. This is simplification, not a guarantee of anatomical completeness.

The following old low-poly export counts are historical, not the current assets:

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

`prepare.mjs` and `verify.mjs` are historical tools for the earlier
open-anatomy-3d female, not the current OpenSkeleton asset.

## The old female, kept for the record

This is the body that was replaced, and why.

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

## The replacement source: the Visible Human cadavers

Both sexes are being replaced with skeletons segmented from real medical scans,
because a warped male is not a female and an atlas of one anonymous male is not
either body. The scans are the **NLM Visible Human Project** datasets, published
by the U.S. National Library of Medicine, redistributed as DICOM by the COMODO
research group (https://github.com/COMODO-research/Comodo_VisibleHuman) under
the licence in their `LICENSE_Visible_Human_data`, **CC BY 4.0**.

Credit line:

> Cadaver CT data: Visible Human Project, U.S. National Library of Medicine.
> DICOM redistribution: COMODO (Comodo_VisibleHuman), CC BY 4.0.
> Segmented, simplified and re-oriented for this project; see `vhf/README.md`.

Unlike Z-Anatomy's share-alike, CC BY 4.0 only asks for attribution, so the
skeleton that comes out of this route carries no copyleft. That is a side
effect, not the reason for the change: the reason is that these are two
individuals, so her pelvis, her rib cage, her skull and her limb proportions are
hers, and his are his.

The atlas is still used here, but only to answer *which* bone a voxel belongs
to. No vertex, no proportion and no surface in the result comes from it.

`vhf/` holds the pipeline and its evidence. When it replaces
`skeleton-female.obj` and `skeleton-male.obj`, the Z-Anatomy and
open-anatomy-3d sections above become history rather than provenance.
