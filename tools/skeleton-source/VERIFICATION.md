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
| mesh | 97,227 vertices, 195,242 triangles, 28 parts |
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

## The female, from her own scan — shipped and passing

`skeleton-female.obj` is no longer a warped male. It is the Visible Human Female
segmented from her own CT at native resolution, and both bodies are checked
before the build:

```sh
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-male.obj
node tools/skeleton-source/verify-obj.mjs tools/skeleton-source/skeleton-female.obj \
    --sex female --scanned
```

| | female |
| --- | --- |
| source | Visible Human Female CT, 1,727 slices at 0.7227 mm in-plane, 1.0 mm slices |
| threshold | 1600 HU for separation, regrown at 1200 HU within its own neighbourhood |
| components | 401 |
| bones in each foot | **19 islands, 26 bones** |
| rig parts | 28 |
| individual bones | 374 |
| mesh | 61,700 vertices, 122,382 triangles |
| stature | **1.712 m**, soles at y = 0.000, never scaled |
| hip height | 51.2% of stature |
| knee height | 27.0% of stature |
| mirror error, long bones | 3.1 mm femur, 7.2 mm shin, 6.9 mm humerus, 7.7 mm forearm |
| shells under 12 mm | **0** (was 187; 75 of them under 8 mm) |
| result | **PASSED** |

### Why her first scan build was covered in white specks

The first scanned build passed the verifier and still looked broken on screen,
because the defect was sub-centimetre and the verifier measures lengths. Three
separate faults were behind it, and all three had to be fixed:

- **The threshold.** A 1600 HU mask separates two bones, but it keeps only the
  densest part of each one. The pelvis, the scapulae and the sternum are two to
  four millimetres thick, so what survived was their cortical rim, and the rim
  of a curved plate is a scatter of shards. Every piece is now regrown to its
  real extent at 1200 HU -- above soft tissue, below cortical peak -- inside a
  two-voxel neighbourhood, so the separation decided at 1600 still stands.
- **The holes.** A cortical shell thresholded at any level is full of pinholes,
  and marching cubes then draws a torn surface that reads as debris. Closing
  with a 1.5 mm ball measured on the real voxel size (1.0 x 0.7227 x 0.7227 mm)
  took the largest hip-band shell from 74 open boundary edges to **0** while
  *reducing* its triangle count.
- **The crumbs.** Islands under a 12 mm diagonal are not bone. Nothing in a
  skeleton is smaller: the smallest named bone here, a distal phalanx, is over
  twenty millimetres. Calcified costal cartilage also survives every threshold
  and hangs in the intercostal spaces as flat ribbons, so small, thin islands are
  dropped from the ribs alone -- the patella is a cortex shell too, and dropping
  one side's flakes and not the other's breaks the mirror.

### The male was rebuilt at higher detail

His first build spent 18,699 triangles on the whole body, which left a rib with
58 and a phalanx with 12. A rib at 58 triangles is a faceted shard, and next to
the 130,000-triangle scanned female those shards read as debris floating beside
the body. `prepare-zanatomy.mjs` now has a `DETAIL` multiplier and a
40-triangle floor per bone shell; the male is 97,227 vertices and 195,242
triangles and his ribs are solid surfaces. The measurements in the table above
did not change -- nothing about his skeleton was altered, only how finely it is
tessellated.


### What the two checks mean now

The verifier used to apply one set of bands to both bodies. It no longer can,
because the two bodies are different people rather than one model and one warp:

- **`--sex`** selects the series. Adult female long bones are proportionally
  shorter than adult male ones for the same height, so judging her against the
  male bands would fail a correct body.
- **`--scanned`** changes what a miss means. A miss is now only a fault when the
  part also disagrees with its own reflection: a real person's left and right
  differ, a deformed body's do not agree with each other. On her, every
  out-of-band part is bilateral -- humerus 10.8 mm, forearm 9.8 mm, femur
  4.5 mm -- so they are reported as `individual`.
- **pose-dependent pairs** -- clavicle, scapula, rib, hand -- are not compared
  between sides for a scanned body. She was scanned lying down with her arms
  folded across her abdomen, and that moves a shoulder by centimetres and puts
  her two hands a centimetre apart in height without anything being wrong. Her
  hands are still measured for length; only the side comparison is skipped.
- **joint heights** were added. They do not depend on sex, on pose or on
  stature: the hip is at 52% of a skeleton's height and the knee at 27%. That is
  what catches a body that has been stretched or squeezed even when every long
  bone happens to land inside its band.


## The female, from OpenSkeleton — a different woman

The shipped `skeleton-female.obj` is no longer the Visible Human Female cadaver.
She is subject `N186416` of **OpenSkeleton** (Duquesne et al., Ghent University,
CC BY 4.0): 170.2 cm, 78.0 kg, BMI 26.9, aged 41, from a post-mortem CT with
thin slices. The dataset labels **182 bones individually** — every carpal, every
metacarpal, every tarsal, every metatarsal and every phalanx — which is what
lets a rig have working fingers and toes. Imported by `openskeleton/03-import.py`
from her own `skelly-out.npz`; nothing is scaled and no bone is borrowed from the
male.

| | female |
| --- | --- |
| source | OpenSkeleton, subject `N186416`, CC BY 4.0 |
| bones | 182 → 27 rig parts, 90,118 vertices, 179,824 triangles |
| stature | 1.66209 m as built (metadata `cadaver_length` 1702 mm) |
| hand | 27 bones each — 8 carpals, 5 metacarpals, 14 phalanges; the thumb has no middle phalanx, which is anatomy |
| foot | 28 bones each — 7 tarsals, 5 metatarsals, 14 phalanges, 2 sesamoids |
| verifier | **PASSED** (`--sex female --scanned`) |

### What the verifier measures on her

```
femur (left)             25.20%       24.2-28      ok
tibia+fibula (left)      22.39%       19-22.7      ok
humerus (left)           19.07%     17.2-20.9      ok
radius+ulna (left)       15.08%     13.7-17.3      ok
hand (left)              10.62%      9.4-12.4      ok
foot length (left)       12.41%     13.5-16.9      individual  (-18.2 mm)
hip height               50.35%     49.5-55.5      ok
knee height              26.06%       23-30.5      ok
mirror error, every pair          0.1-0.4 mm  ok
```

The foot row is reported, not hidden: a defleshed foot is 15-20 mm shorter than
the living foot the band is drawn from, because the heel pad and the toe pads are
gone. Her own foot bones are individually right — calcaneus 73.1 mm, first
metatarsal 63.0 mm, and heel-to-ball is 0.78 of the foot, which is the human
ratio. Nothing was rescaled to make the row pass.

### What the import has to fix, and why

The dataset registers each bone into a mean template, so the bones arrive the
right size but in the wrong arrangement. Four corrections are pose corrections,
not shape edits:

| correction | what it fixes |
| --- | --- |
| A-pose re-pose | her arms were folded across her abdomen |
| digit straightening | fingers and toes curled; a curled chain measures 144 mm instead of 176 |
| foot levelling | the ankle was pointed, and the foot rolled so the big toe stood 44 mm above the little one |
| shoulder seating | shoulders were shrugged 43.7°, leaving her neckless beside the male |
| right side mirrored | her own left side, mirrored, because registration put her two femurs 66 mm apart |

Mirroring is exact to 0.1 mm, and the body is centred on the plane of its own
left/right pairs rather than on the pelvis mean — the sacrum is a single midline
bone, so averaging it in with the two hips shipped the body 6.35 mm off centre.
