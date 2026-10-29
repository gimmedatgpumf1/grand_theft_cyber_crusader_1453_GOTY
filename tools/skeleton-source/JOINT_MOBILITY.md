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
