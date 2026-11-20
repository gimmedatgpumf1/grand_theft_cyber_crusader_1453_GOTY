# MakeHuman feet — work in progress

The standing CT reference also checks each rendered V-view phalanx shaft, not
just the line between toe centres. Outlying shafts are turned as rigid source
bones and their IP hinges are moved with them; distant unchanged segments are
brought closer at their visible surfaces. A small female left third-MTP turn
separates adjacent rays. The blue comparison retains its independently fitted
standing reference and retargeted little-toe skin rather than copying those CT
corrections directly. This is visual registration, not anatomical ground
truth: some blue joints and bent skin still need refinement. Original-pose
restore retains the prior source/comparison appearance.

The blue skeletons use the **original-resolution MakeHuman foot cut** in the
room. Width and heel-to-toe length are fitted separately, then the whole mesh
is rotated and positioned relative to the heel and the five distal toes. A
sequence of sex-specific whole-mesh scales, rotations and translations was
calibrated against blue foot-bone surfaces in the default standing and selected
toe-bend poses. The refinements exclude tibia and fibula. Corrections scale
with the foot and are mirrored on the right. A female-only adjustment shortens
the foot by 5% along the heel-to-toe axis and repositions it around the standing
bones. The male fit is unchanged; there is no per-frame optimizer.

Individual toe-length ratios are applied to the female skin after the whole-foot
fit. Original MakeHuman bone weights and a smooth root-to-tip ramp localize the
deformation, while the connecting foot stays in place. The female great, second
and third toes were shortened independently for the standing bones. A localized
female-only adjustment then draws the original four toe webs toward the
metatarsals, with a small sideways correction to the second web. It leaves the
toe tips nearly fixed and preserves the mesh topology and skin owners. The female
little-toe middle and distal bones have a separate rigid reference adjustment;
the joints are recentered and the little-toe skin is retargeted and lengthened.
Other individually selected phalanges also have rigid resting adjustments;
blue fourth and little toes and the female third toes have separate placements.
CT mesh vertices are not modified, and restoring the original pose removes the
reference adjustment. The
small-bone directions are approximate, not clinically validated. Two bone influences
animate the skin, and the preview is 50% opaque. Original triangles and smooth
vertex shading are retained; there is no simplification, plane fit or procedural
nail overlay.

Each MTP and IP joint was inspected in both sexes and on both sides, at rest
and with its individual control bent. The blue bones had particularly visible
separation during motion because each segment used a different rotation centre.
Their motion now carries a shared, estimated contact point through each toe
chain; the ragdoll uses the same contact estimate. The underlying white CT surfaces are unchanged, and original-pose restore
retains its former skin reference. These contacts are geometric estimates, not anatomical landmarks.

This remains an approximate fit: the source meshes and B-view lines use
estimated bone landmarks, not anatomical joint surfaces. Resting phalanges were
reviewed individually in top, oblique and side Vulkan views. Some joints may
still overlap or look poorly aligned, including the little toes. Some bent and ragdoll poses expose
bone through the skin, and the ragdoll can distort the toes. Further work is
needed on the resting joints, skin and dynamics; this is not clinical validation.
The MakeHuman source and its CC0 license are in `tools/human-source/`.

`inspect-toes.cpp` is an off-screen **visualization** utility; it does not
choose bone corrections. It can display one digit's B lines (`isolated`), the
white scan (`solid`), blue scan (`bluebones`) or blue scan with skin (`blue`).
Run it from the repository root after linking the native client objects:
`inspect-toes OUTPUT female L 5 isolated`. The `ORIGINAL=1` environment
variable requests the original-pose view.

## Manual toe bone editor

In the skeleton room press **T**. The isolated workspace opens on the white B
source, one foot at a time. Pick male/female, left/right, toe 1–5 and PROX,
MID or DIST (the big toe has no MID). With WHITE and B selected, use the
ROTATE XYZ and MOVE XYZ +/- controls to position that individual bone. The
steps cycle between STEP (5 degrees / 1 mm), FINE (1 degree / 0.2 mm), and
MICRO (0.1 degrees / 0.02 mm). Descendant bones are counter-moved, so the
selected bone can be adjusted independently.

TOP/SIDE, right-drag orbit and wheel zoom let you inspect from different angles.
ALL BONES / ONE TOE / ONE BONE changes isolation. H hides/shows the controls.
STAND previews your source edits on the standing figure; the selected source
foot's edits are also reflected to both blue feet. Select BLUE and LEFT/RIGHT to
inspect each blue foot. Male and female need separate edits. Press T to return
to the room. The source CT mesh vertices are unchanged. B restores the original
source view; EDITS reapplies your current B edits to that view.

UNDO and RESET remove adjustments; SAVE and LOAD use `toe-bone-edits.txt` in
the game root. Save after undo/reset to retain it. Edits on the B source are
applied to standing when the workspace closes or STAND is selected.

Drag the green start and red end handles onto the actual surface of each bone.
Dim handles are unconfirmed placeholders; bright handles have been placed on a
triangle. Orbit to reach the end faces, and use ONE BONE if handles overlap.
You can mark either the original or manually adjusted white B bone: marks are
stored in the original bone's mesh coordinates. When adjacent marked bones are
visible, selecting a child displays the distance between its green point and its
parent's red point. The B view remains unchanged while marking. In STAND, the
marked surface points join adjacent bones and the marked source shaft directions
are transported into the standing foot. The blue reference uses both marked
source feet, reflected and averaged, for its individual bone directions and
surface points. A single marked side still works and is mirrored. The primary
hinge direction retains its existing flexion sense while being made perpendicular
to the marked parent shaft; a shaft alone does not specify rotation around itself.

SAVE AXES/LOAD AXES use `toe-bone-axes.txt`, separately from pose SAVE; RESET
AXIS clears the selected bone's marks. If no local axes file exists, the editor
starts with the supplied marks in `tools/foot/user-toe-axes.txt`. A local file
normally takes precedence and is not overwritten on startup. The exact earlier
38-entry left-only supplied save is upgraded in memory with the newly supplied
right-side marks; other local edits remain untouched. The supplied file has
both endpoints for all 19 bones on both feet of each sex. The white kinematic
pivot and each ragdoll anchor lie on the marked surfaces. Joints without both
points still use their previous approximate pivots. Blue points are transferred
onto the averaged blue surface, which can differ from the white surface; check
alignment and contact visually.

For blue geometry, the comparison pipeline combines the left scan with the
reflected right scan and then mirrors that averaged left-side mesh to the
right; it does not simply copy one unmodified white foot.

## Earlier fitting experiment (not the in-game foot)

![Earlier distorted fitting and reduction experiment](progress.png)

The following historical steps reproduce a **different, deformed and reduced**
mesh. These offsets and the results of its sampled-pose checks do not apply to
the current foot preview.

## Reproducing the offline data

Install NumPy, SciPy, trimesh, rtree, networkx, python-fcl and
fast-simplification. Compile `export-fit.cpp` and `export-pose.cpp` against the
native client objects (C++20) and run from the repository root. Replace the
uppercase names below with directories/files outside the repository:

1. Extract and fit full resolution at the default pose:

   ```
   python3 tools/foot/extract-makehuman-foot.py
   export-fit FIT
   python3 tools/foot/fit-bone-clearance.py FIT/m_L
   python3 tools/foot/fit-bone-clearance.py FIT/f_L
   ```

2. Recompile `export-pose` after regenerating headers. Export the fitted high
   mesh **without** prior pose offsets, fit the pose set, and write offsets:

   ```
   export-pose HIGH_POSES --base-only --sweep
   python3 tools/foot/fit-poses.py HIGH_POSES m MALE_BASIC --ignore-ankle --small-only --basic-only
   python3 tools/foot/fit-poses.py HIGH_POSES f FEMALE_BASIC --ignore-ankle --small-only --basic-only
   python3 tools/foot/fit-poses.py HIGH_POSES m MALE_TOES --ignore-ankle --small-only --initial MALE_BASIC
   python3 tools/foot/fit-poses.py HIGH_POSES f FEMALE_TOES --ignore-ankle --small-only --initial FEMALE_BASIC
   python3 tools/foot/fit-poses.py HIGH_POSES m MALE_HIGH_OFFSETS --initial MALE_TOES
   python3 tools/foot/fit-poses.py HIGH_POSES f FEMALE_HIGH_OFFSETS --initial FEMALE_TOES
   python3 tools/foot/apply-pose-correction.py FIT/m_L HIGH_POSES/m0_L-rest MALE_HIGH_OFFSETS
   python3 tools/foot/apply-pose-correction.py FIT/f_L HIGH_POSES/f0_L-rest FEMALE_HIGH_OFFSETS
   ```

3. Reduce those fitted meshes, compile `export-pose` again, and fit the reduced
   meshes with high-resolution pose offsets enabled but reduced offsets disabled:

   ```
   python3 tools/foot/simplify-fitted-foot.py FIT/m_L --fitted skin-pose-fitted.obj
   python3 tools/foot/simplify-fitted-foot.py FIT/f_L --fitted skin-pose-fitted.obj
   export-pose LOW_POSES --reduced --base-only --sweep
   python3 tools/foot/fit-poses.py LOW_POSES m MALE_LOW_OFFSETS
   python3 tools/foot/fit-poses.py LOW_POSES f FEMALE_LOW_OFFSETS
   python3 tools/foot/apply-reduced-pose-correction.py LOW_POSES/m0_L-rest MALE_LOW_OFFSETS
   python3 tools/foot/apply-reduced-pose-correction.py LOW_POSES/f0_L-rest FEMALE_LOW_OFFSETS
   ```

4. Recompile the exporter and verify **fresh** exports with all offsets applied:

   ```
   export-pose HIGH_FINAL --sweep
   export-pose LOW_FINAL --reduced --sweep
   python3 tools/foot/check-clearance.py HIGH_FINAL
   python3 tools/foot/check-clearance.py LOW_FINAL
   ```

`render-progress.cpp` renders both sexes, both sides, rest/bent poses and four
angles; run `render-progress OUTPUT [--reduced]` after compiling. The body source
is `tools/human-source/`, its license is `tools/human-source/CC0.txt`, and
`NOTICE.md` contains attribution.
