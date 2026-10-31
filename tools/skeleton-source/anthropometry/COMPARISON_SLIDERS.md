# Blue comparison bodies and sliders

The C test room now has a blue, static T-pose copy alongside each original:
blue male, original male, original female, blue female from left to right.
The copies use the originals' initial T-pose meshes but do not participate in
the R ragdoll or the originals' subsequent joint changes. V still changes the
originals to the existing anatomical inspection shapes; the copies stay blue
meshes. The physics and source geometry of the originals are not scaled.

Press **P** to open or hide the comparison controls. Press **Esc** to release
the cursor, drag a track or click its minus/plus buttons, and click outside the
panel to resume looking. Select male or female; **Reset** affects that copy.
For a very short/narrow window the footer switches between the two groups of
four sliders. Values shown are centimetres. Head, neck, torso, arm, leg, hip
and shoulder sliders are multipliers of the appropriate sex-specific
height-based surface estimates. Minus/plus adjusts them by 2 percentage points;
the height buttons change the target by 1 cm. Tracks are convenient windows
(40–230 cm for height, 50–150% for other dimensions), **not** minimum/maximum
height cutoffs: buttons may go beyond them. Positive finite dimensions are
required. The listed values are the **fitted skeletal proxies**. When a rigid
pelvic ring cannot meet both requested torso and leg proxies, both controls
still move the copy; the fit is projected to the closest pair of achievable
values with equal weight. On a wide panel the footer shows fitted/requested
values for those dimensions. If a fit cannot be produced at all, the previous
shape is kept. Reset restores the original posed height and formula requests.

## What is measured

The default height is the *visible posed mesh* from floor to skull top, so each
blue copy has the same visible height as its neighbouring original. This is
about **171.77 cm male** and **166.16 cm female**, not the recorded 170.0 and
170.2 cm cadaver lengths. All seven formula targets are evaluated at that
visible height. Changing height intentionally breaks the same-height comparison
until reset. The tested geometric proxies on the blue mesh are:

| Slider | Fitted skeletal proxy | Surface formula being requested |
|---|---|---|
| Head | skull top to bottom of jaw, vertical | vertex–menton; Korean source provenance unverified |
| Neck | posterior C7 to mean left/right C1–skull contact midpoint, vertical | C7–tragion, ANSUR-derived |
| Torso | lowest pelvis to posterior C7, vertical | cervicale–crotch, ANSUR-derived |
| Arm | mean left/right 3D chord, medial humerus extremum to middle distal finger extremum | acromion–stylion plus stylion–middle fingertip |
| Leg | mean fitted femoral-head centre to floor, vertical | trochanterion–floor |
| Hip width | leftmost-to-rightmost pelvis | NHANES surface hip breadth |
| Shoulder | leftmost-to-rightmost scapula | NHANES biacromial breadth |

The test reconstructs these points from actual source vertices and T-pose
matrices and checks the values against the displayed fitted values. The leg proxy is not the
previous hip–knee–ankle chain length. The female femoral-head proxy is not the
surface trochanterion; the C1 contact is not tragion; bone extremities do not
include skin. Thus matching these numbers is a **visual approximation**, not a
medical or population-average skeleton and not evidence that the original
subjects were abnormal. Hand and foot dimensions have no separate fitting yet.

## Method and limits

The previous fit put a vertical-field knot through the skull. At the default
male C1 contact, its local Y scale changed from **1.383 below** to **0.970
above** the knot, while X and Z stayed at 1.0; it stretched the jaw and lower
face differently from the cranium. The corrected default applies a uniform
scale of about **1.088 male** and **1.045 female** to skull, jaw and C1.
The regression test checks every vertex of all three structures against one
shared similarity transform, including after slider changes.

`tests/skull_mesh_contact.py` also compares the actual source and blue triangle
surfaces with FCL. The registered T-pose already has head–jaw intersections in
both originals and head–C1 intersections in the female original; the blue copies
retain these, rather than hiding them by moving the original jaw or neck. In this
static audit, FCL reports 96 male and 65 female head–jaw contacts, and 112 female
head–C1 contacts; male head–C1 surfaces are 0.026 mm apart. Counts depend on the
mesh and collision algorithm. At 50 cm, FCL also checks that C1/C2 contact
remains and no new jaw/C2, jaw/C7 or head/C7 surface intersections appear.
The audit also checks the pelvic ring, hips and sacrum/L5 at default and 50 cm.
The registered source already has left pelvis/femur triangle intersections in
both sexes and right pelvis/femur intersections in the female; they remain in
the blue copies. This does not test containment or establish the correct
biological placement, and **does not fix those pre-existing overlaps**.

The source T-pose is captured once. The left and right pelvis and the sacrum
now share **one uniform 3D scale and translation**, so the entire pelvic ring
retains its source shape and internal relationships. The neighbouring fields
meet that transform at the top and bottom of the ring; the femoral-head areas
use the same field. The test checks every vertex of all three pelvic structures
against the common similarity transform, including after slider changes. At
50 cm the female left pelvis previously had up to 29.5 mm of non-affine
residual; the common similarity transform removes that deformation.

The skull, jaw and C1 also share a uniform transform. The torso field meets it
at the bottom of C1. The jaw may overlap C7 in height because it is anterior
to C7: forcing the jaw above C7 was an incorrect constraint. The common
skull-to-jaw height determines the cranial scale, and the desired neck and
overall height determine C7. Width transitions continuously between pelvis and
scapula, and both arms share a length factor about their shoulder centres.

The original child/adult surface formulas and their continuous blends are
unchanged. The request and fitted shape are continuous at the formula joins.
However, the requested surface torso and leg distances generally **cannot both
be represented by the chosen bony proxies with the intact adult pelvic ring**.
The fit therefore changes the pelvic height to minimise their equally weighted
squared deviations and displays actual fitted values rather than pretending
that both targets were met. At 50 cm, male requested/fitted torso is
23.30/21.20 cm and leg is 22.80/20.70 cm; female requested/fitted torso is
22.59/19.78 cm and leg is 22.93/20.13 cm. This is a limitation of applying
surface references to these bone landmarks, not a child/adult formula jump.
If other extreme multipliers make the pelvic ring impossible to place below
C7, the conflicting requests are continuously blended toward source scaling.
A sweep accepts all integer heights 1–1000 cm, but the default torso and leg
proxies differ from their requested values throughout that sweep.

This is still a rendering deformation outside the skull/jaw/C1 and pelvic
assembly. It does **not** certify all joint contacts, preserve every bone,
implement a new anatomical rig or guarantee collision-free motion. The blue
figures are not simulated. The original white skeletons remain unchanged.

The existing child/adult surface curves, neonatal bridges and bounded
continuations are used. They are not a skeletal growth model: for example, a
50 cm copy is a warped adult skeleton, **not** an infant skeleton. Unmatched
landmarks, extrapolation, independent CT registrations and unverified Korean
head-data provenance limit interpretation. See `SEVEN_DIMENSION_COMPARISON.md`,
`CHILD_ADULT_TRANSITION.md`, and `HAND_FOOT_COMPARISON.md` for data and audit
caveats. No new clinical validation is claimed.

The original and room GPU geometry remain cached when a slider changes; only
changed blue meshes are rebuilt and their batch revisions updated. The UI is
GPU geometry in the same Vulkan frame, with no per-frame CPU pixel readback.
The test suite includes target measurements, interaction, cache invalidation,
continuity at transition heights, software/line paths, and an optional real-ICD
Vulkan pixel test. A software ICD is not a physical-GPU performance test;
300 FPS and Windows/MSVC remain unverified here.
