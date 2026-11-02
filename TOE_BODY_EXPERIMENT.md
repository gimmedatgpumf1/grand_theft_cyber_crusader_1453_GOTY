# Bone-first toe experiment — not integrated

This is an **unfinished experiment**, not a replacement for the rejected toes.
The game does not render these meshes. The appearance remains paw-like in the
offline render, the polygon count is too high for the desired style, and the
posed geometry is not collision-clean. Do not use it as a finished body.

The experiment builds a left forefoot surface from the mapped comparison
phalanx geometry, not a generic foot mesh. It generates a closed volumetric
plate for each nail and experiments with binding those parts to the existing
source toe joints. The white source models and existing blue comparison
rendering are unchanged. No right-foot surface, in-game animation, GPU cache,
profile-slider sweep, or clinical/visual approval is supplied.

## Reproducible checks

`tests/toe_body_geometry.cpp` is included in the native regression script. It
checks closed, consistently wound topology and normalized weights for both
sexes at 50, 170 and 250 cm. It checks that all 28 source toe joints per sex
change their corresponding comparison bone and that left-toe flesh and nails
move. It does **not** test collisions, surface quality, clinical anatomy, a
right-foot mesh, or the full movement ranges.

`tests/toe_body_export.cpp` exports left skin, nails and mapped phalanx meshes
in metres. Build it from the repository root after the Bullet build:

```sh
g++ -std=c++20 -O2 -pthread -Isrc -Inative -Inative/vendor/bullet/src \
  tests/toe_body_export.cpp native/client/{skeleton_room,skeleton_body,skeleton_ragdoll,raster,hud,ui_layer}.cpp \
  build/obj/bullet/libbullet.a -o /tmp/toe-body-export
/tmp/toe-body-export /tmp/toe-rest 170
/tmp/toe-body-export /tmp/toe-bent 170 2 0
python3 tests/toe_body_contact.py /tmp/toe-rest
python3 tests/toe_body_contact.py /tmp/toe-bent
```

The last two commands require NumPy, trimesh, rtree and python-fcl. The
contact diagnostic intentionally returns nonzero for any contact or bone
outside the skin, including floor contact. The exporter arguments are height
in cm, toe number 1–5, and joint 0=MTP, 1=first IP, 2=second IP (the great toe
has only joints 0 and 1). The selected joint is moved by 10 degrees within its
existing range.

At 170 cm, both rest skins and nails are closed and winding-consistent, and
there are no nail/skin triangle contacts. The male fifth and female third
distal bones nevertheless each have a vertex on the floor/skin boundary; the
female bone has seven reported skin contact pairs. There is no positive
clearance at those points. At the same height, a 10-degree second-toe MTP
bend produced 53 male nail/skin contact pairs and an exposed distal-bone
vertex. Other individually sampled joint bends also produced nail contacts or
exposed bone vertices. These are known failures, not accepted exceptions.
The static male/female skins have 17,138/11,734 triangles respectively; nails
add 960 triangles per foot. They have not been optimized or tested on a GPU.

The source skeleton itself contains inherited intersections documented in
`BONE_SYMMETRY_CHECKLIST.md`; these tests do not certify its clinical accuracy.
Real Windows MSVC and the user's graphics hardware have not been tested.

## Anatomical context, not a fitted dataset

The toe webs approximately lie around the shafts of the proximal phalanges,
while the MTP joints lie further proximally; see the
[lesser MTP surface anatomy description](https://www.orthopaedicsone.com/orthopaedicsone-articles-the-lesser-metatarsal-phalangeal-joints-surface-anatomy/).
Nails sit dorsally over the distal toes with proximal and lateral grooves; see
[nail anatomy](https://musculoskeletalkey.com/nails/). These descriptions do
not supply subject-specific skin thickness, toe contours, or deformation
weights. The field dimensions are experimental approximations, not medical
measurements. Forefoot metatarsal-pad measurements must not be presented as
measurements of tissue over the toe bones.
