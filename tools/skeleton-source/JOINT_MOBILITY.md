# Joint movement reference for the OpenSkeleton graph

The table below classifies **every one of the 209 relationships** in
`04-line-graph.py`; `openskeleton/joint_mobility.py` assigns the class from the
actual source labels, including each finger and toe on both sides. The graph
is not an exhaustive list of every human articulation: the source does not
separately label the individual skull bones, teeth, hyoid or ear ossicles,
and several real joints contain multiple surfaces and coupled motions. These are
biological descriptions, **not implemented runtime animation constraints**.
Individual joint centres and axes are not supplied by OpenSkeleton. Movement
ranges vary with person, position, loading and measurement. The figures below
are approximate clinical reference values measured from anatomical neutral,
not a guarantee, an injury threshold, or Euler-angle limits relative to the
registered cadaver pose. Combined rotations are coupled, not independent boxes.

| Class | Allowed and restricted motion |
| --- | --- |
| Temporomandibular | Hinges and glides for opening/closing, protrusion/retraction and side-to-side; not a free ball joint. |
| Atlanto-occipital | Primarily nodding flexion/extension, small lateral bending; limited axial rotation. |
| Atlanto-axial | Predominantly axial rotation; limited flexion/extension, not a free ball joint. |
| Intervertebral | Small coupled bending and rotation at each level, not free rotation; total cervical reference flexion/extension ~45° each, side bending ~45° each, rotation ~80° each; thoracolumbar total flexion ~90°, extension ~30°, side bending/rotation ~30° each. These totals cannot be applied to each vertebra. |
| Sacroiliac / pubic symphysis | Very limited coupled motion/glide; neither is a freely rotating ball joint. |
| Costovertebral / costal cartilage | Small coupled rib rotations/glides and elastic cartilage motion with breathing, not unrestricted motion; cartilage links are inferred, not scanned bone joints. |
| Sternoclavicular / acromioclavicular | Sternoclavicular saddle movement combines elevation/depression, protraction/retraction and axial rotation; acromioclavicular small glides/rotations. Scapular movement is coordinated; it is not a simple rigid rotation of the entire shoulder girdle. |
| Shoulder ball-and-socket | Flexion/extension, abduction/adduction, and internal/external rotation. Whole-shoulder clinical reference ~180° flexion/abduction, 40–60° extension, 70° internal and 90° external rotation; full elevation includes scapular movement, not 180° at the glenohumeral joint alone. |
| Elbow hinge | Flexion/extension, roughly 0–140/150° flexion; side-to-side movement is strongly restricted. Pronation/supination belongs to the radioulnar joints, not the elbow hinge. |
| Radioulnar pivot | Coupled forearm pronation/supination ~80–90° each; radius rotates relative to ulna, not a free hinge between shafts. The V-view forearm plane merges both and cannot show this internal rotation. |
| Wrist condyloid | Flexion ~80°, extension ~70°, radial deviation ~20°, ulnar deviation ~30°; no independent unrestricted axial rotation. |
| Carpal glide | Small coupled intercarpal/carpometacarpal glides; 2–5 palm members are merged in V view and cannot move independently there. |
| Thumb CMC saddle | Flexion/extension, abduction/adduction and coupled opposition/reposition; not a simple single-axis hinge. |
| Thumb MCP | Primarily flexion/extension, approximately 0–50° flexion, with limited side motion; not a ball joint. |
| Finger MCP condyloid | Flexion about 90°, extension about 30–45°, abduction about 20°; spreading is constrained when flexed. No unrestricted axial spin. |
| Finger PIP / DIP hinges | Mainly flexion/extension, PIP ~0–100°, DIP flexion ~90° (some extension up to ~20°); not side-bending or free twisting. |
| Thumb IP hinge | Primarily flexion/extension, approximately 0–80° flexion, with variable extension. |
| Hip ball-and-socket | Flexion ~120°, extension ~20°, abduction ~40°, adduction ~20°, internal/external rotation ~45° each; more constrained than shoulder and coupled by posture. |
| Knee modified hinge | Primarily flexion/extension, ~0–135° flexion; some coupled axial rotation when flexed, very limited in full extension. Patella glides with knee movement and is not an independently rotating ball. |
| Tibiofibular | Limited coupled glides; distal syndesmosis is not a free hinge. Tibia and fibula form one rigid V-view plane. |
| Ankle hinge | Primarily dorsiflexion ~20° and plantarflexion ~50°; inversion/eversion is mainly through adjacent foot joints rather than a free ankle ball. |
| Tarsal glide | Limited coupled intertarsal/tarsometatarsal motion including subtalar inversion/eversion; not independent ball joints. The V-view foot volume merges these members. |
| Great-toe MTP | Flexion ~45°, extension ~70°, limited abduction/adduction; not unrestricted spin. |
| Lesser-toe MTP | Flexion/extension ~40° each and limited abduction/adduction; not unrestricted spin. |
| Toe IP hinge | Primarily flexion/extension; not free side-to-side movement or twisting. |
| Sesamoid glide | Small constrained movement within tendon/under the metatarsal head, not an independent ball joint. |

Sources: [OpenStax synovial joints](https://openstax.org/books/anatomy-and-physiology-2e/pages/9-4-synovial-joints),
[movements](https://openstax.org/books/anatomy-and-physiology-2e/pages/9-5-types-of-body-movements),
[selected joints, knee and shoulder](https://openstax.org/books/anatomy-and-physiology-2e/pages/9-6-anatomy-of-selected-synovial-joints),
[Physiopedia reference ranges](https://www.physio-pedia.com/Range_of_Motion_Normative_Values),
[Hand Surgery Primer active hand ranges](https://learn.handsurgeryprimer.org/taxonomy/term/303),
[Kenhub MTP joints](https://www.kenhub.com/en/library/anatomy/metatarsophalangeal-mtp-joints).

Shoulder elevation is coupled to scapular and clavicular motion; the relative
contributions vary through the motion, so a fixed 2:1 ratio is an approximation.
See [McGraw Hill normative ranges](https://fadavispt.mhmedical.com/content.aspx?bookid=1964&sectionid=147190212),
[scapular motion study](https://www.jstage.jst.go.jp/article/jpfsm/7/1/7_65/_pdf/-char/en),
and [goniometry references including toes](https://highered.mheducation.com/sites/0071474013/student_view0/chapter8/goniometry.html).

## Model and coordinate-system research

The [AAH OpenSim arm and hand model](https://github.com/nishbo/ms_arm_and_hand)
is MIT-licensed and offers articulated shoulder, forearm and finger reference
frames and coordinated mechanisms. It is not automatically fitted to either
OpenSkeleton subject, and its documentation says it is an inverse-kinematics
model without dynamic properties; it also fixes some carpal joints. It cannot
by itself meet the all-joints requirement. The
[ISB-style knee reference-frame study](https://pmc.ncbi.nlm.nih.gov/articles/PMC2866785/)
uses bone-specific condyle fitting, not a nearest-centroid pivot.

The source graph and labels alone do not provide the joint axes, neutral joint
frames, secondary constraints, bone contacts, cartilage or ligaments needed to
turn those classes into subject-specific motion. In particular, a tree of
independent hinge rotations does not preserve the multiple articulations in
the wrist, foot, thoracic cage or pelvis. Neither the runtime graph nor the
coarse renderer mesh groups currently solve those constraints.

## Reference-model solver (not yet a subject rig)

`native/client/skeleton_joint.h` implements bounded ordered rotations between
two **supplied, calibrated** joint frames. A reproducible OpenSim 4.6 fixture
exercises all 32 joints of the AAH bilateral finger/thumb reference models,
including both thumb CMC stages, against 160 independently computed parent–child
transforms and 50 full finger/thumb-chain poses. It clamps the AAH
model's own coordinate ranges and rejects non-finite values. Source and license are in `reference/AAH_LICENSE.txt`.
`native/client/skeleton_coupling.h` evaluates precomputed piecewise-cubic
coordinate couplings. A further fixture verifies all 26 AAH bilateral
shoulder-girdle and wrist couplers at 234 OpenSim-generated sample values,
including endpoint extrapolation. It does not claim the AAH rhythm is an
individual subject's measured rhythm.

These are *reference-model* results, not tests of joint frames fitted to either
OpenSkeleton subject. The separate subject-specific femoral-head estimates in
`openskeleton/README.md` provide hip pivot positions and an approximate
femoral epicondylar line for each knee, not calibrated individual motion limits. The room can consume a supplied calibrated tree, but no subject-specific
joint rig is installed, and V-view remains static. The solver does not
implement gliding, cartilage, contacts or closed loops.
AAH's simple joint bounds are a model, not universal clinical hard limits.

Subject fitting must not use a geometry ICP score as a substitute for checking
joint frames. As an exploratory example, rigid ICP of the reference right
index proximal/middle phalanges to the male model yielded surface RMS errors
of 2.28 and 1.58 mm, but the transferred PIP parent and child reference centres
differed by **35.57 mm**. That demonstrates an end-orientation ambiguity, not
a clinically usable calibration. The [PIP CT and motion study](https://pmc.ncbi.nlm.nih.gov/articles/PMC3835607/)
found a moving finite helical axis, showing even a fixed hinge is an
approximation. Do not ship a guessed joint frame as medically validated.


## Open-chain solver and source-mesh frame path

`native/client/skeleton_kinematic_tree.h` composes supplied bounded joints in a
parent-ordered open chain, captures a bind pose without scaling mesh geometry,
and applies a complete mapping to the 182 independent source batches. The room
caches the scene and updates only matrices after a pose revision. Tests compare
the tree with the same 50 OpenSim-calculated AAH whole-finger poses and use a
synthetic hinge to verify both source bodies through the Vulkan frame path.
The synthetic hinge is a plumbing test, not a subject fit. **No subject rig or
fitted joint range has yet been installed in the room.** Closed-loop anatomical
constraints, bone collisions, cartilage and V-view attachment remain unsolved.

## Simple reference bend angles

`native/client/skeleton_rom.h` contains the reference flexion/extension values
above in degrees, and `setReferenceBend()` accepts a joint and a requested bend
in degrees, clamps it, then updates the corresponding source-bone transform.
The values are reference ranges measured from anatomical neutral, not measured
individual limits. The caller must supply a rig whose first coordinate uses
that neutral and the documented sign convention. These ranges **cannot** be
applied as offsets from the original CT pose. No complete neutral-calibrated
source rig is installed in the room yet, so this does not make every joint move.
The test that applies them to source meshes uses a synthetic neutral frame.

Whole-shoulder flexion is about 180° with scapular movement; the simple
reference for glenohumeral flexion alone is 120° (and extension 60°), as in
[the shoulder-joint reference](https://books.byui.edu/the_introduction_and_application_of_kinesiology_biomechanics_and_assessment/chapter_7).
For the lesser toes, the reference PIP range is 0–35° flexion and DIP is 30°
extension to 60° flexion ([OrthopaedicsOne](https://www.orthopaedicsone.com/orthopaedicsone-articles-lesser-toe-joints-passive-range-of-motion/)).
Great toe IP flexion is about 50° ([range-of-motion reference](https://physical-therapy.us/range-of-motion-rom/)).
For unspecified joints, no numeric range is invented. Other axes, coupled
movement, subject neutral and individual limits still require calibration.
