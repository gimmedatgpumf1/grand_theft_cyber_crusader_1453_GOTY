# Verification

Everything below is produced by the scripts in this directory, from the files
that are actually shipped. Nothing here is an assertion about what a pipeline
"should" have done.

Two checkers, two questions:

| script | question | needs |
| --- | --- | --- |
| `verify-obj.mjs` | is the *shipped OBJ* anatomically sane? | nothing but the file |
| `verify.mjs` | did the export from open-anatomy-3d move anything? | that checkout + glTF deps |

```bash
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-male.obj
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-female.obj
```

`verify-obj.mjs` exits non-zero on a missing part, a mirrored-part error over
3 mm, or a long bone more than 10 mm outside its reference band. A smaller
proportional miss is reported as `off` and tolerated, because the reference
bands are population limits and a skeleton is one individual.

## The shipped male — passes

Source: Z-Anatomy `SkeletalSystem100.fbx` (CC BY-SA 4.0, derived from
BodyParts3D), assembled through its node transforms, filtered to the 177
skeletal objects, 99 of them mirrored to complete the body, grouped into 28 rig
parts, each bone simplified on its own.

| check | result |
| --- | --- |
| source objects kept | 177 skeletal of 1,851 meshes (rest is muscle attachment geometry) |
| bones in the shipped file | 272 (177 + 99 mirrored, 1 already had a right twin) |
| height | source crown 170.55 cm, lowest bone 0.71 cm → **169.58 cm of bone** |
| shipped height | 1.6958 m, lowest vertex at y = 0.0002 m |
| mesh | 9,739 vertices, 18,699 triangles, 28 parts |
| femur | 26.87% / 26.77% of stature (band 25.5–27.5%) |
| tibia + fibula | 22.29% / 22.27% (band 21.0–24.2%) |
| humerus | 18.77% / 18.80% (band 17.6–19.6%) |
| radius + ulna | 15.90% / 15.79% (band 14.2–16.2%) |
| hand | 10.79% / 10.71% (band 10.0–11.8%) |
| foot | 13.49% / 13.49% (band 14.0–16.2%) — **8.7 mm short** |
| mirror error | worst pair 1.8 mm (femur), most pairs under 1 mm |

Two honest notes:

* The foot is short by 8.7 mm against the population band. It is continuous —
  calcaneus, talus, navicular, cuneiforms, metatarsals and every phalanx meet —
  so it is the source individual's build, not a break in the mesh.
* The right side is a mirror of the left. The FBX stores only the left half of
  paired bones. Symmetry error is therefore near zero by construction, and real
  left/right asymmetry is not represented.

The scan's own proportions do not match the old body that used to be shipped
here (that one had a 22.1 cm foot and a 30.6 cm humerus). Those numbers came
from decimating BodyParts3D through a different pipeline; this one is the
curated atlas at full resolution.

## The shipped female — fails, and always did

She is still the mesh from `open-anatomy-3d` (`anatomy-female.glb`), unchanged,
because no female alternative has been found. Current measurement:

| check | result |
| --- | --- |
| height | 1.6081 m |
| femur | 25.24% / 25.20% (band 25.5–27.5%) — 4.4 mm and 4.9 mm short |
| tibia + fibula | 25.51% / 25.51% (21.0–24.2%) — 33 mm long |
| humerus | 15.52% / 15.92% (17.6–19.6%) — 33.4 mm and 26.9 mm short |
| radius + ulna | 17.13% / 16.02% (14.2–16.2%) — 15 mm long on the left only |
| foot | 11.00% / 11.10% (14.0–16.2%) — 48 mm short |
| mirror error | **scapula 17.8 mm, humerus 13.6 mm**, ribs 6.2 mm, clavicle 4.1 mm, femur 3.2 mm |
| verdict | **12 FAILED** |

### Why

Upstream does not have a female skeleton. `pipeline/f_build.mjs` builds her by
taking the **male** BodyParts3D skeleton and pushing it through a
thin-plate-spline warp fitted to female surface landmarks:

```js
for (const p of maleParts) { ... warpPoint(tps, p.pos[i], ...) ... }
```

A TPS warp is a smooth global deformation. That is why her long bones land at
the wrong fractions of stature and why her left and right scapulae no longer
match: the landmark set that drives the warp is not symmetric. The only female
scan data inside her is the pelvic region (`VH_F_femur_*`,
`VH_F_ilium_compact_bone_*`, `VH_F_lumbar_vertebra_5`), which is why the pelvis
is the one part that looks right.

Her import was faithful — `verify.mjs` confirms 7,560 of 7,560 vertices are
present verbatim with 0 moved, and the height is preserved to 2 mm. Faithful
copying of a deformed source is still a deformed body.

## A real female source has been found

The Visible Human Female — the NLM cadaver dataset, a 59-year-old woman — has a
full-body CT in a reachable mirror,
https://github.com/COMODO-research/Comodo_VisibleHuman: 1,727 slices of
670 x 673 at 0.7227 x 0.7227 x 1.0 mm, CC BY 4.0. Her bones segment out of it,
and so do his: the same repository holds the Visible Human Male at 1,873 slices
of 552 x 520 at 0.9375 x 0.9375 x 1.0 mm.

Her first build downsampled the volume to 1.4 x 1.4 x 2.0 mm before
thresholding. That build was rejected, correctly: the cartilage gap at a joint
is one to two millimetres, so at 2.0 mm slice spacing the partial-volume average
weld it shut. Her arm came out as one 20,675-voxel blob, her feet as 65 fused
fragments between them, and 96.8 million voxels failed to reach any label.

The rebuild does not downsample. See the next section.

## Why the atlas cannot fix her

Z-Anatomy's models **are** BodyParts3D (its readme says "models derived from
'BodyParts3D'"). BodyParts3D's own database description says it is "a
three-dimensional whole-body model for an adult human **male**". The lineage is
male-only from top to bottom, so a different file from the same family cannot
supply a female — only a different dataset can, and the Visible Human Female is
the one that does.

## Consequence

The male is a trustworthy anatomical reference and is ready for rigging at
1.696 m.

The female is not female anatomy yet and must not be rigged as if she were. Her
own bones now segment at native resolution, and what is missing is a way to cut
the joints her scan fused — her hands rest on her iliac crests, so the arm and
the pelvis are one component. Until that is cut, the shipped
`skeleton-female.obj` remains the warped male mesh and the test room says so on
screen: `FEMALE 160.8 CM // WARPED MALE, NOT A SCAN`.

## The Visible Human rebuild — measured, and not yet shipped

`vhf/` segments both cadavers from their own CTs at native resolution. Nothing
in this section is shipped as `skeleton-*.obj` yet, so nothing here is claimed
about the game's current skeleton; these are the measurements of the pipeline's
output, recorded so the next step starts from facts.

Female, Visible Human Female CT, 1,727 slices at 0.7227 mm in-plane:

| check | result |
| --- | --- |
| threshold | 1600 HU |
| components ≥ 60 voxels | 437 |
| components ≥ 1,000 voxels | 128 |
| components ≥ 100,000 voxels | 6 |
| bones resolved in each foot | **29 left, 29 right** |
| largest foot bones | 47,550 / 27,061 / 16,108 voxels (calcaneus, talus, then midfoot) |
| mesh | 62,224 vertices, 128,815 triangles, 26 parts |
| stature | 1.713 m, soles at y = 0.000 |
| `verify-obj.mjs` | **20 failed** — see below |

The foot count is the anatomical proof that native resolution works: a human
foot has 26 bones, and 29 components means the tarsals, five metatarsals and all
the phalanges came apart, with three extra fragments where a sesamoid or a
cuneiform split. The previous downsampled pipeline produced 65 fused fragments
for both feet together.

`verify-obj.mjs` fails on this mesh for two reasons, both real:

- **the arms** — her hands rest on her iliac crests, so at 1600 HU the arm and
  the pelvis are a single component, and a straight-armed atlas cannot assign
  the elbow or wrist on a 90-degree bent arm. `hand_l`/`hand_r` are missing and
  the humerus measures 8.8–13.6% of stature against a 17.6–19.6% band.
- **the femur** — nearest-axis assignment gives the femoral axis the hip
  endplate, so the femur part measures 32.6–33.8% against a 25.5–27.5% band.

The legs below the knee, the whole trunk and the skull do pass: tibia+fibula
22.87% and 22.61% (band 21.0–24.2%), rib mirror error 4.3 mm, scapula mirror
error 6.1 mm. Replacing the shipped file before the arms are separated would
trade one failing body for another, which is why it has not been done.
