# Blue bone symmetry checklist

Scope: the **blue comparison skeletons**, for both male and female models.
The white CT-derived originals are intentionally unchanged and are not expected
to be symmetric. A checked box means the listed blue mesh has been reflected
and its geometry tested; it does **not** mean the shape has been visually
approved, that its average is anatomically exact, or that every joint is clear.

## Arms and legs — yes, their blue meshes are symmetric

Each corresponding left/right pair has identical reflected vertices and
opposite triangle winding. All the listed parts are included, not just the
large bones.

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

- [x] Shoulder girdle: scapulae and clavicles (2 pairs).
- [x] Ribs: rib pairs 1–12 (12 pairs).
- [x] Pelvic halves / innominates (1 pair).

Together with the limbs, the source provides **77 pairs per sex**, and all 77
are mirrored in the blue comparison. The current pair averaging is a geometric
approximation; mirrored geometry alone does not prove the average looks right.

## Central blue bones

- [x] Head / skull: symmetric about its own 3D, voxel-fitted skull plane.
- [x] Jaw: mirrored about the same skull-derived plane and shares the skull's
  cranial transform. Head–jaw intersections remain in the blue meshes.
- [x] Sternum: averaged and mirrored about the body midplane.
- [x] Sacrum: averaged and mirrored about the body midplane.
- [x] Cervical vertebrae C1–C7: each fitted, averaged and mirrored about its own 3D plane.
- [x] Thoracic vertebrae T1–T12: each fitted, averaged and mirrored about its own 3D plane.
- [x] Lumbar vertebrae L1–L5: each fitted, averaged and mirrored about its own 3D plane.

Each of the 24 vertebrae is symmetric individually. Their independently fitted
planes and positions preserve the source spine's pose, so they need not coincide
with one another or with the independently fitted skull plane. This does not
establish symmetry of the **entire** assembled skeleton about one common plane.

## Verification and remaining work

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
- [ ] Audit assembled alignment and joints across the whole body and during
  motion; exact neutral mesh reflection does not prove those are symmetric.
