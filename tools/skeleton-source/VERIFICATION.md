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

## Why Z-Anatomy cannot fix her

Z-Anatomy's models **are** BodyParts3D (its readme says "models derived from
'BodyParts3D'"). BodyParts3D's own database description says it is "a
three-dimensional whole-body model for an adult human **male**". The lineage is
male-only from top to bottom, so a different file from the same family cannot
supply a female.

What that leaves, checked and exhausted:

| source | has a female skeleton? |
| --- | --- |
| Z-Anatomy / BodyParts3D | no — single adult male |
| open-anatomy-3d female | no — TPS warp of the male |
| Human Reference Atlas v1.10 (united-female) | no — skin, organs, pelvis and legs only; no skull, ribs or arms |
| Visible Human Female | not published as per-bone surface meshes on a reachable host |

The room caption says so on screen: `FEMALE 160.8 CM // WARPED MALE, NOT A SCAN`.

## Consequence

The male is a trustworthy anatomical reference and is ready for rigging at
1.696 m.

The female is not female anatomy and must not be rigged as if she were. Options,
in order of how much they can be trusted:

1. **A female scan or segmented CT.** The only route that is both honest and
   correct. Everything else in the rig would fall into place around it.
2. **A real female pelvis plus a female-stature body proportioned from the male
   data** — defensible only if every substituted part is labelled, because it is
   a reconstruction, not a scan.
3. Keep the male-only room until (1) or (2) is decided.
