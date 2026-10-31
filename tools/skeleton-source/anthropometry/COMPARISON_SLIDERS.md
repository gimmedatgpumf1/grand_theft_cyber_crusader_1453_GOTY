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
height cutoffs: buttons may go beyond them. Positive finite dimensions are required. When the surface targets cannot all
fit the skeletal landmark order, the vertical dimensions are continuously
blended toward the original body's proportions rather than reversing bones.
The panel says **Partial fit** and displays the fitted proxy values, which then
differ from the requested formula. If a fit cannot be produced at all, the
previous shape is kept. Reset returns the original posed height and formula
targets.

## What is matched

The default height is the *visible posed mesh* from floor to skull top, so each
blue copy has the same visible height as its neighbouring original. This is
about **171.77 cm male** and **166.16 cm female**, not the recorded 170.0 and
170.2 cm cadaver lengths. All seven formula targets are evaluated at that
visible height. Changing height intentionally breaks the same-height comparison
until reset. The tested geometric proxies on the blue mesh are:

| Slider | Matched skeletal proxy | Surface formula being applied |
|---|---|---|
| Head | skull top to bottom of jaw, vertical | vertex–menton; Korean source provenance unverified |
| Neck | posterior C7 to mean left/right C1–skull contact midpoint, vertical | C7–tragion, ANSUR-derived |
| Torso | lowest pelvis to posterior C7, vertical | cervicale–crotch, ANSUR-derived |
| Arm | mean left/right 3D chord, medial humerus extremum to middle distal finger extremum | acromion–stylion plus stylion–middle fingertip |
| Leg | mean fitted femoral-head centre to floor, vertical | trochanterion–floor |
| Hip width | leftmost-to-rightmost pelvis | NHANES surface hip breadth |
| Shoulder | leftmost-to-rightmost scapula | NHANES biacromial breadth |

The test reconstructs these points from actual source vertices and T-pose
matrices and checks the values against the targets. The leg proxy is not the
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

The source T-pose is captured once. A continuous monotone vertical field maps
the ordered floor, pelvis, mean hip, C7, lowest C1 point, mean C1 contact and
skull landmarks while preserving total height. Between the pelvis and C7,
monotone cubic segments meet smoothly at the hip instead of placing a sharp
slope change through the pelvic bones. The same field is applied to the pelvic
ring, sacrum and neighbouring bones to avoid separating their surfaces. The skull, jaw and C1 receive
the same **uniform 3D scale and translation**; their shapes and relative
positions are preserved. The torso field joins that transform at the bottom of
C1. The jaw may overlap C7 in height because it is anterior to C7: forcing the
jaw above C7 was an incorrect constraint and caused the previous partial fit at
50 cm. The shared skull-to-jaw height determines the cranial scale, and the
desired neck and overall height determine the C7 position. The actual mean of
the left/right hip centres is solved after deformation. Width transitions
continuously between pelvis and scapula, and both arms share a length factor
about their fitted shoulder centres.

At 50 cm both copies now match all seven nominal targets without a partial fit.
The original child/adult formulas and their continuous blends are unchanged.
The regression suite checks all seven against the source models in a 0.5 cm
sweep from 50 to 1000 cm, measures the actual proxies at representative heights,
and checks continuity around the curve boundaries. An integer sweep from 1 to
1000 cm also accepted every height: default inputs fit exactly from 15 cm male
and 8 cm female upward. Below those heights, independently extrapolated targets
can become geometrically incompatible with the fixed total height and ordered
skeletal landmarks. Extreme independent slider multipliers can likewise be
incompatible. In those cases a binary search blends the four vertical targets
toward uniform source scaling; the controls show **Partial fit** and the fitted
values. This is a rendering deformation: outside the skull/jaw/C1 assembly,
individual bones can change shape. In particular, smoothing removes the sharp
pelvic kink but cannot make the pelvis rigid while simultaneously enforcing
incompatible bone-proxy distances; the arm field is only anchored at the
estimated shoulder centre. For example, a best-affine fit to the female left
pelvic mesh had a maximum vertex residual of about 18.3 mm at the default height
before smoothing, and 12.7 mm afterwards; at 50 cm it decreased from 34.6 mm
to 29.5 mm. These residuals quantify remaining non-affine warping, not anatomical
accuracy. It does **not** certify joint contact, preserve all rigid bones,
implement a new anatomical rig or guarantee collision-free motion.
The blue figures are not simulated. The original white skeletons remain unchanged.

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
