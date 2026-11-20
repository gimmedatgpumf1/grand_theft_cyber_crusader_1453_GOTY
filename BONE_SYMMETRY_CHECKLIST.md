# Blue bone symmetry checklist

Scope: the **blue comparison skeletons**, for both male and female models.
The white CT-derived originals are intentionally unchanged and are not expected
to be symmetric. A checked box means the listed blue mesh has been reflected
and its geometry tested; it does **not** mean the shape has been visually
approved, that its average is anatomically exact, or that every joint is clear.

## Arms and legs — yes, their blue meshes are symmetric

Each corresponding left/right pair has identical reflected vertices and
opposite triangle winding about the same upright body plane. All the listed
parts are included, not just the large bones.

- [x] Upper arms: humeri (1 pair).
- [x] Forearms: radii and ulnae (2 pairs).
- [x] Wrists and hands: all eight carpal bones, five metacarpals and fourteen
  finger/thumb phalanges (27 pairs).
- [x] Upper legs and knees: femora and patellae (2 pairs).
- [x] Lower legs: tibiae and fibulae (2 pairs).
- [x] Ankles and feet: seven tarsals, five metatarsals, fourteen toe phalanges
  and two foot sesamoids (28 pairs).

**Total:** 30 arm/hand pairs and 32 leg/foot pairs. These are visual comparison
meshes in their neutral comparison pose, not a claim that an independently
animated pose or ragdoll remains symmetric.

## Other paired blue bones

- [x] Shoulder girdle: scapulae and clavicles (2 pairs). Both blue pairs were
  already averaged and mirrored; the separate audit below confirms each pair.
- [x] Ribs: rib pairs 1–12 (12 pairs). The blue pairs were already averaged
  and mirrored; the separate rib audit below confirms each pair.
- [x] Pelvic halves / innominates (1 pair).

Together with the limbs, the source provides **77 pairs per sex**, and all 77
are mirrored in the blue comparison. The current pair averaging is a geometric
approximation; mirrored geometry alone does not prove the average looks right.

## Scapula and clavicle audit

Each scapula or clavicle is paired with its opposite-side counterpart, not
reflected onto itself. Both blue pairs were already averaged and mirrored about
the body midplane; neither geometry was changed in this audit. The original
white meshes remain untouched. Source asymmetry is an
approximate, bidirectional, area-weighted mean distance between one posed
white bone and its reflected opposite; it includes position and shape differences
and is not an anatomical landmark measurement.

| Pair | Male source distance (mm) | Female source distance (mm) | Blue pair |
|---|---:|---:|---|
| Scapulae | 6.157 | 4.707 | symmetric |
| Clavicles | 7.146 | 4.121 | symmetric |

`tests/bone_pair_symmetry.cpp` checks every vertex and triangle winding at 52
profiles per sex, including 50/170/250 cm and individual and combined slider
endpoints. Largest reflected-vertex error was below 0.000000001 mm. The separate
`tests/shoulder_girdle_mesh_contact.py` checks both sides at those heights,
single-slider extremes at 170 cm and combined extremes at all three heights.
In those tests, the scapulae and clavicles
are clear of same-side rib surfaces, and the selected clavicle/sternum,
clavicle/scapula and scapula/humerus surfaces do not intersect. At the three
heights the maximum gaps for those three pairs were, respectively, 2.498,
1.804 and 2.537 mm (male), and 3.676, 3.460 and 2.337 mm (female). No
additional disjoint self-crossings were detected relative to the left source
templates at those heights. These tests do not validate cartilage, anatomy,
articulated motion or visual quality.

## Rib-by-rib audit

Each rib is paired with its opposite-side rib; a single curved rib is not
reflected onto itself. All twelve blue pairs were already mirrored about the
body midplane. Selected pairs were moved equally outward along X after aligning
the spine, to maintain tested rib–vertebra clearances. Both white source ribs
remain untouched. The table gives an approximate, bidirectional,
area-weighted mean distance between the posed white rib and its reflected
opposite, in millimetres; it includes position differences as well as shape.
It is not an anatomical landmark measurement.

| Rib pair | Male source distance (mm) | Female source distance (mm) | Blue pair |
|---|---:|---:|---|
| 1 | 4.476 | 7.202 | symmetric |
| 2 | 5.532 | 7.411 | symmetric |
| 3 | 5.016 | 5.527 | symmetric |
| 4 | 5.723 | 7.030 | symmetric |
| 5 | 5.830 | 6.687 | symmetric |
| 6 | 5.278 | 4.669 | symmetric |
| 7 | 5.316 | 2.510 | symmetric |
| 8 | 8.140 | 3.595 | symmetric |
| 9 | 8.791 | 4.719 | symmetric |
| 10 | 6.749 | 6.292 | symmetric |
| 11 | 4.132 | 7.384 | symmetric |
| 12 | 3.436 | 11.000 | symmetric |

`tests/bone_pair_symmetry.cpp` checks every blue vertex and triangle winding at 52
profiles per sex, including 50/170/250 cm and individual and combined slider
endpoints. The largest reflected-vertex difference was below 0.000000001 mm.
`tests/rib_mesh_contact.py` finds adjacent blue ribs and the selected rib/Tn
and rib/T(n−1) surfaces clear at those three heights, selected 170 cm
single-slider extremes and combined extremes at all three heights. Male source
rib 8's left template has one disjoint triangle self-crossing, which is also
mirrored onto the right blue rib. The audit does not assess cartilage,
anatomical joint fit or visual quality.

## Central blue bones

- [x] Head / skull: fitted, averaged and mirrored in its own 3D voxel frame,
  then rigidly rotated and centered onto the common upright body plane.
- [x] Jaw: uses the same fitted skull frame and rigid adjustment; shares the
  skull's cranial scale. Head–jaw intersections remain in the blue meshes.
- [x] Sternum: averaged and mirrored about the common body midplane. The source
  labels the sternum as one mesh; the manubrium is not a separate structure and
  cannot be independently anatomically segmented or certified here.
- [x] Sacrum: averaged and mirrored about the common body midplane. The male
  source's off-centre coccyx tip is smoothly centred before mirroring, so it
  produces one central tip instead of two. The source provides the sacrum and
  coccyx as one mesh rather than separately segmented bones.
- [x] Cervical vertebrae C1–C7: each fitted independently, then rigidly aligned
  to the common plane.
- [x] Thoracic vertebrae T1–T12: each fitted independently, then rigidly aligned
  to the common plane.
- [x] Lumbar vertebrae L1–L5: each fitted independently, then rigidly aligned
  to the common plane.

The source-frame 3D fits still account for the scanned rotations before
averaging. Their blue reconstructed shapes are then rotated, not flattened, to
share the upright body plane. Each central bone is translated only along X
for centering; selected paired ribs also move along X for joint clearance.
White source geometry and poses are unchanged.

## Verification and remaining work

- [x] `tests/coccyx_symmetry.cpp` checks the male blue coccyx has one central
  tip and one connected contour at seven levels, across 52 size/slider profiles.
  It does not establish anatomical accuracy or visual approval.
- [x] `tests/whole_skeleton_symmetry.cpp` checks the triangle winding of all
  28 central structures and corresponding vertices and winding of all 77 pairs
  against one common plane, in 52 profiles per sex. It also verifies all 182
  white source meshes remain unchanged.
- [x] `tests/bone_symmetry_checklist.cpp` checks every vertex and triangle
  winding of all 77 paired meshes, for both sexes, over 52 profiles each:
  defaults, heights 50/170/250 cm, each dimension at its slider endpoints,
  and all dimensions at their lower or upper endpoints together. The largest
  observed reflected-vertex error was below 0.000000001 mm (floating-point
  rounding); this tests the mapped blue geometry, not anatomy or appearance.
- [x] `tests/skull_symmetry.cpp` checks reflected skull and jaw triangles,
  preservation of the source meshes and rigid 3D rotation of the skull frame.
- [x] `tests/skeleton_comparison.cpp` checks the axial sternum and sacrum,
  paired meshes and mapped slider profiles. The FCL audit separately tests
  selected contacts and disjoint self-crossings, not all possible joints.
- [x] `tests/vertebra_symmetry.cpp` checks every vertex and triangle of all 24
  vertebrae, both sexes, across 50/170/250 cm at individual and combined slider
  endpoints; it also verifies the original white meshes remain unchanged.
- [x] `tests/vertebra_mesh_contact.py` audits all 23 adjacent vertebra pairs at
  50/170/250 cm and selected single-slider extremes at 170 cm. Pairs that were
  clear in the prior blue at all three reference heights remain clear in those
  tests; overlaps present in the prior blue or source meshes still remain.
- [ ] Review the visual quality and anatomical plausibility of the averages,
  including the skull and jaw, with the result visible in the application.
- [ ] Audit all remaining joints and articulated motion; exact neutral mesh
  reflection does not prove joint clearance or symmetry during motion.
