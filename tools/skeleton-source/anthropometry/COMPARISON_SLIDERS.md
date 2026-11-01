# Blue comparison bodies and sliders

The C test room now has a blue, static T-pose copy alongside each original:
blue male, original male, original female, blue female from left to right.
The copies are constructed from both sides of the originals' initial T-pose
meshes and do not participate in the R ragdoll or the originals' subsequent
joint changes. V still changes the
originals to the existing anatomical inspection shapes; the copies stay blue
meshes. The physics and source geometry of the originals are not scaled.

Press **P** to open or hide the comparison controls. Press **Esc** to release
the cursor, drag a track or click its minus/plus buttons, and click outside the
panel to resume looking. Select male or female; **Reset** affects that copy.
For a very short/narrow window the footer switches between the two groups of
four sliders. Values shown are centimetres. Each size slider is centered on
the sex-specific formula at the current height and ranges from that value
minus 1.5 sample SD to plus 1.5 sample SD. The SDs are calculated separately for
male and female from **all nonblank values** of each whole-body TSV column;
see `BODY_SIZE_STANDARD_DEVIATIONS.tsv`. These are pooled over all heights,
not SDs conditional on height. The head column contains circumference-based
estimates and some other columns include historical proxies, so the ranges are
not validated biological percentiles. Source cohorts are mixed US populations,
not verified Indo-European. See `WHOLE_BODY_HEAD_DATA.md`.

Minus/plus adjusts size by 0.1 SD and height by 1 cm. Height cannot be set
below 50 cm; the plus button can go above the track's 230 cm upper end. If
the formula value minus 1.5 SD is non-positive, the slider has a small positive
floor: negative or zero dimensions cannot be rendered. The listed values are **fitted skeletal
proxies**. When a rigid pelvic ring cannot meet both requested torso and leg
proxies, both controls still move the copy; the fit is projected to the closest
pair of achievable values with equal weight. On a wide panel the footer shows
fitted/requested values. The ±1.5 SD bounds apply to requested slider values;
the fitted skeletal torso or leg can fall outside them when the intact pelvic
ring cannot satisfy both requests. At very short arm requests, the arm's fixed
endpoint offsets can also make the fitted chord longer than requested. Fitted
values do not impose an additional slider restriction. Reset restores the
default height and formula requests for the selected sex. Height changes keep
the current SD offsets where feasible.

To regenerate the SDs (sample SD, `n-1` denominator) from the original rows:

```sh
python3 tools/skeleton-source/openskeleton/29-comparison-sd.py
```

The blue copies request the seven sex-specific equations supplied by King, with
height `h` and each result in cm. Male:

| Head | Neck | Torso | Arm | Leg | Hip | Shoulder |
|---|---|---|---|---|---|---|
| `0.0561*h+12.179` | `0.0193*h+7.4136` | `0.3536*h+4.4647` | `0.4515*h+1.4121` | `0.5553*h-6.5708` | `7.6737*exp(0.0075*h)` | `10.719*exp(0.0077*h)` |

Female:

| Head | Neck | Torso | Arm | Leg | Hip | Shoulder |
|---|---|---|---|---|---|---|
| `0.0568*h+11.832` | `0.0213*h+7.1832` | `0.3446*h+4.5758` | `0.4734*h-1.3187` | `0.568*h-6.9588` | `6.7179*exp(0.0089*h)` | `10.87*exp(0.0075*h)` |

These are used directly as requested targets before applying the sliders. The
male and female leg equations become non-positive at or below approximately
11.83 and 12.25 cm respectively, and the exponentials eventually overflow or
produce widths incompatible with the pose: such inputs cannot be displayed and
the prior shape is preserved. The minimum stature is 50 cm. For
example at 170 cm the rigid ring projects the requested torso/leg values of
64.58/87.83 cm to approximately 63.70/86.96 cm. At 50 cm it projects
22.14/21.19 cm to approximately 17.33/16.38 cm. The other five targets match
at these two heights; the head target is approximately 21.72 cm at 170 cm and
14.98 cm at 50 cm. For the female copy at 170 cm, requested torso/leg values
63.16/89.60 cm are fitted as 62.09/88.54 cm; at 50 cm, 21.81/21.44 cm are
fitted as 17.07/16.70 cm. The other five female targets match at these heights.
All are visual skeletal proxies, not clinical validation.

## What is measured

The blue copies default to **172.2 cm male** and **159.4 cm female**, measured
from floor to skull top. All seven formula targets are evaluated at the selected
height. Reset returns each copy to its sex-specific default. The unscaled
originals have visible posed heights of approximately 172.68 cm male and
166.26 cm female, distinct from their recorded cadaver lengths of 170.0 and
170.2 cm. The tested geometric proxies on the blue mesh are:

| Slider | Fitted skeletal proxy | Surface dimension represented in the source data |
|---|---|---|
| Head | skull top to bottom of jaw, vertical | vertex–menton; user-supplied target |
| Neck | posterior C7 to mean left/right C1–skull contact midpoint, vertical | C7–tragion, ANSUR-derived |
| Torso | lowest pelvis to posterior C7, vertical | cervicale–crotch, ANSUR-derived |
| Arm | mean left/right 3D chord, medial humerus extremum to middle distal finger extremum | acromion–stylion plus stylion–middle fingertip |
| Leg | mean fitted femoral-head centre to floor, vertical | trochanterion–floor |
| Hip width | leftmost-to-rightmost pelvis | NHANES surface hip breadth |
| Shoulder | leftmost-to-rightmost scapula | NHANES biacromial breadth |

The test reconstructs these points from the actual blue meshes and joint
estimates and checks them against the displayed fitted values. The leg proxy is not the
previous hip–knee–ankle chain length. The female femoral-head proxy is not the
surface trochanterion; the C1 contact is not tragion; bone extremities do not
include skin. Thus matching these numbers is a **visual approximation**, not a
medical or population-average skeleton and not evidence that the original
subjects were abnormal. Hand and foot dimensions have no separate fitting yet.

## Method and limits

The previous fit put a vertical-field knot through the skull. At the default
male C1 contact, its local Y scale changed from **1.383 below** to **0.970
above** the knot, while X and Z stayed at 1.0; it stretched the jaw and lower
face differently from the cranium. The corrected fit applies one uniform scale to skull, jaw and C1.
The regression test checks every vertex of all three structures against one
shared similarity transform, including after slider changes.

`tests/skull_mesh_contact.py` also compares the actual source and blue triangle
surfaces with FCL. The registered T-pose already has head–jaw intersections in
both originals and head–C1 intersections in the female original. Averaging the
blue skull and jaw changes those surfaces but does not fix the existing
intersections. At default height FCL reports 96/90 source/blue male head–jaw
contacts, 65/0 female head–jaw contacts, and 112/130 female head–C1 contacts.
The female blue head–jaw gap is approximately 0.004 mm; the male head–C1 gap
is approximately 0.034 mm in the source and 0.475 mm in the blue copy. Counts
depend on the mesh and collision algorithm. The test also
checks for disjoint triangle self-crossings, C1/C2 contact and unwanted
jaw/C2, jaw/C7 or head/C7 intersections over the tested height range.
The audit also checks the pelvic ring, hips and sacrum/L5 at default, 50, 100, 170 and 250 cm.
Source pelvis/femur intersections are no longer reproduced in the audited blue
copies: the two symmetric pelvic halves, sacrum and femoral surfaces have small
gaps without triangle crossings in those cases. The source sacrum and L5 still
intersect, as do the blue copies. These tests do not establish containment or
clinical correctness.

The white reference T-pose levels the two hip centres but preserves the
individual source legs. Their bottoms differ by approximately 20.6 mm for the
male and 2.3 mm for the female, so one white foot can hover. **B** restores the
original source pose. White geometry, scale and ragdoll physics are unchanged.

The blue copy instead starts with an arithmetic average of the two posed
hip-to-floor lengths, and both blue feet touch the floor. For every paired
structure, the right surface is reflected into the left frame. Because the two
sides do not share a vertex correspondence, their centroids are averaged and a
nearest-vertex displacement is smoothed across the left mesh before it is
mirrored. Triangle orientation is guarded during this estimate, and a
non-adjacent triangle-crossing guard protects the thin scapula. Below the
pelvis the horizontal and depth scales stay constant rather than tapering with
height, preserving straight vertical joint-centre lines. This is an
approximate bilateral *mesh* mean, not a vertex-by-vertex anatomical mean or a
population average. Both rendered sides have identical reflected triangles.
The central sacrum and sternum are averaged, clipped and mirrored with a welded
seam. The skull uses a separate midplane fitted **only to its own source mesh**;
hip and other body landmarks do not determine it. The same skull-derived plane
is used for the jaw, with no separate jaw-centroid alignment. The complete
three-axis skull frame follows the head's rigid T-pose rotation. The surfaces
are averaged and mirrored in that frame, then returned through its inverse;
they are not forced upright or re-centred on the hips. The skull, jaw and C1
retain their shared cranial scale transform.
`45-fit-skull-midplane.py` fits reflected skull surfaces against the actual
triangles, with a robust cap, and checks the estimated orbit centres
independently. On held-out surface samples the mean reflection-to-surface
error is 1.04 mm male and 1.11 mm female; the estimated orbit midpoint is
0.38 mm and 1.06 mm from the fitted plane respectively. This is a geometric
estimate, not proof of an ideal anatomical midline. The skull and jaw triangle
meshes are mirrored about that plane. Each source vertex is averaged with the
nearest point on the opposite triangle surface, without global smoothing; only
local displacement that would reverse a face is reduced. This is still an
approximation because nearest points are not guaranteed to be anatomically
corresponding landmarks. Remaining central bones retain their source shapes,
so exact whole-body symmetry is not yet claimed.

The pelvic halves and sacrum then use one uniform 3D transform at each slider
setting, maintaining the fitted ring. Source-specific fits for the sacrum, hip/leg and scapula were made to these
two source meshes to avoid crossings or excessive gaps at selected surfaces;
the sacrum fit includes a symmetric width adjustment, and pubic spacing uses
the mean source-side measurement. The values are not biological reference data and
need recalibration and triangle audits if the source meshes or poses change.
In particular, the averaging/registration does **not** certify every joint or
remove the source sacrum/L5, head/jaw or female head/C1 overlaps. The test
checks the actual blue pair symmetry, feet, pelvic similarity and original
white restore; the FCL audit checks selected actual triangles.

The source-specific fits use FCL triangle distances on the posed, averaged
surfaces. The symmetric sacrum search varied its width by up to 5% and its
vertical/depth position by up to 12 mm, minimizing the gap to the innominates
while retaining sacrum/L5 contact and penalizing movement. Hip, knee and scapula
registrations similarly minimized their selected joint gaps and movement;
they were not obtained from a population sample or clinical joint limits.
The regression runs the pelvic and selected limb/shoulder triangle checks for
both sexes at default, 50, 100, 170 and 250 cm and at the ends of the arm,
hip and shoulder tracks at 50, 170 and 250 cm. It also compares disjoint
triangle self-crossings in all 77 paired left templates and the two axial
reconstructions against the original source meshes. A non-intersection result
for these surfaces is not a containment or whole-body joint test.

The skull, jaw and C1 also share a uniform transform. The torso field meets it
at the bottom of C1. The jaw may overlap C7 in height because it is anterior
to C7: forcing the jaw above C7 was an incorrect constraint. The common
skull-to-jaw height determines the cranial scale, and the desired neck and
overall height determine C7. Width transitions continuously between pelvis and
scapula. The scapula, clavicle and upper arm use a common affine shoulder
frame to avoid a vertical-field kink at their joint. The arm slider scales the
upper-arm shaft beyond the protected shoulder head; forearms and hands share a
length factor anchored at the averaged elbow contact.

The height targets use the sex-specific equations above without an age-group
crossfade. A 50–250 cm sweep tests 1,092 axis-extreme blue-mesh samples per sex
at 0.1 cm height steps, including their motion rate; a second sweep checks
every mesh vertex at 0.5 cm steps.
Selected triangle contacts are audited at 15 heights.

The requested surface torso and leg distances generally **cannot both be
represented by the chosen bony proxies with the intact adult pelvic ring**.
The fit projects those requests and displays fitted rather than requested
values. If extreme multipliers make the pelvic ring impossible to place below
C7, conflicting requests are blended toward source scaling. Invalid or incompatible extreme inputs preserve the previous shape; any
extrapolations are not measurements.

This is still a rendering deformation outside the skull/jaw/C1 and pelvic
assembly. It does **not** certify all joint contacts, preserve every bone,
implement a new anatomical rig or guarantee collision-free motion. The blue
figures are not simulated. The original white skeletons remain unchanged.

These formulas and visual deformations are not a skeletal growth model:
a 50 cm copy is a warped adult skeleton, **not** an infant skeleton. Unmatched
landmarks, extrapolation and independent CT registrations limit interpretation.
See `POOLED_HEIGHT_MODELS.md` and `HAND_FOOT_COMPARISON.md`. No new clinical
validation is claimed.

The original and room GPU geometry remain cached when a slider changes; only
changed blue meshes are rebuilt and their batch revisions updated. The UI is
GPU geometry in the same Vulkan frame, with no per-frame CPU pixel readback.
The test suite includes target measurements, interaction, cache invalidation,
continuity at transition heights, software/line paths, and an optional real-ICD
Vulkan pixel test. A software ICD is not a physical-GPU performance test;
300 FPS and Windows/MSVC remain unverified here.
