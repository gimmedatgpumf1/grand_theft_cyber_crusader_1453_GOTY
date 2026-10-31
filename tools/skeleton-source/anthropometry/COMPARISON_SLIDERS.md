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

The source T-pose is captured once. A continuous piecewise-linear vertical field
maps the ordered floor, pelvis, mean hip, C7, jaw, mean C1 contact and skull
landmarks to the overlapping targets while preserving total height. The actual
left/right hip and C1 means are solved after deformation, not assumed equal to
the transform of their mean. Width transitions continuously between pelvis and
scapula, and the left and right arms share a length factor about their own fitted
shoulder centres. The original C7 location is kept near its scaled position,
with an interior margin to keep the landmark order feasible. At incompatible
target combinations a binary search finds the closest feasible blend of the four
vertical targets toward uniform source scaling. This is a rendering
deformation: individual bones can change shape, and the separate arm field is
only anchored at the estimated shoulder centre. It does **not** certify joint
contact, preserve all rigid bones, implement a new anatomical rig or guarantee
collision-free motion. The blue figures are not simulated.

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
