# Blue bones. Fit the body.

## View and fit the actual skeleton in HTML

Open `skeleton_fit.html` through a local web server. The page reads the real
`standard_blue_male.obj` or `standard_blue_female.obj` mesh and its matching
JSON metadata from this folder. For example, from the repository root, run
`python3 -m http.server 8000`, then visit
`http://localhost:8000/export/skeleton_fit.html`. The preview renders the
original bone meshes—not a block model. Choose sex and height (0.50–2.50 m),
and drag to rotate. Each body region follows its own formula fit, not one
whole-body scale. The pelvis ring stays intact, so fitted torso and leg sizes
can differ slightly from their targets.

Spawn up to four bodies to compare them side by side. Adjust each selected
body's height, sex, world position, and orientation independently. Presets
cover standing, sitting, squatting, kneeling, hands-and-knees, elbows-and-knees,
prone, supine, and raised arms.
The joint editor exposes separate neck, shoulder, elbow, wrist, hip, knee, and
ankle controls on all three axes. Joint markers are optional (off initially)
and appear only on the selected body. Right-drag or left-drag to orbit,
Shift-drag to pan, and use the wheel or the visible zoom
controls to change camera distance. Resizing a skeleton does not auto-zoom the
camera. **Fit all bodies** is an explicit framing action. The reach check
compares the target surface gap from the shoulder/hip against measured full
limb length and separately reports whether the current endpoint is in contact.

Download the selected body's fitted, posed OBJ and fit JSON from the page. The
OBJ retains the bone objects and triangles; keep `standard_blue.mtl` beside it
in an OBJ viewer. The JSON records height, mesh bounds, formula and fitted
measurements, fit parameters, joint pivots, joint angles, and world transform.
At either source model's default height, the HTML preview uses the existing OBJ
vertices directly. Units are **m**; axes are **X right, Y up, Z forward**.

## Open the ready-made model

Pick one OBJ:

- `standard_blue_male.obj` — profile height 1.722 m.
- `standard_blue_female.obj` — profile height 1.594 m.

Keep `standard_blue.mtl` beside the OBJ. Open the OBJ in a program that reads
Wavefront OBJ. Units: **m**. Axes: **X right, Y up, Z forward**. Do not turn
the axes. The ready-made OBJ is static. It has no animation rig.

## Make a new body size

Do **not** make a bigger or smaller copy with one whole-body factor. That does
not change human proportions.

Use the fitter. From the repository root, run:

```
./export/rescale.sh male 1.80
```

Use `male` or `female`. Height is in metres. Minimum is 0.5 m. Example:

```
./export/rescale.sh female 1.65
```

The script builds the native fitter, then makes a new OBJ and JSON in `export/`.
It starts from the registered source skeleton—not from the already-fitted OBJ.
It runs the real body fit and T-pose step. It does not just multiply every
vertex by one number. Keep `standard_blue.mtl` beside the new OBJ.

This command needs the full repository, its source skeleton data, a C++20
compiler, and Bash. The `export/` folder by itself is not enough to make a new
fit. The ready-made OBJ files still work on their own.

## The body-size formulas

`scale.mjs` returns the seven formula targets. The fitter uses the same formulas.
Input `H` is height in metres. Every answer is in metres. These are body spans
and widths, not seven bone lengths. They overlap. Do not add them together.

| Target | Measure |
|---|---|
| `head` | Head top Y to Jaw bottom Y. |
| `neck` | Mean C7 Y to mean C1 neck-contact Y. |
| `torso` | Pelvis bottom Y to C7 Y. |
| `arm` | Mean left/right straight 3D span: inner Humerus to tip of `HandDistP3`. |
| `leg` | Mean left/right hip-centre height above the floor. |
| `hip` | Full X width of both pelvis meshes. |
| `shoulder` | Full X width of both scapula meshes. |

### Male

```
head     = 0.0561*H + 0.12179
neck     = 0.0193*H + 0.074136
torso    = 0.3536*H + 0.044647
arm      = 0.4515*H + 0.014121
leg      = 0.5553*H - 0.065708
hip      = 0.076737 * exp(0.75*H)
shoulder = 0.10719 * exp(0.77*H)
```

### Female

```
head     = 0.0568*H + 0.11832
neck     = 0.0213*H + 0.071832
torso    = 0.3446*H + 0.045758
arm      = 0.4734*H - 0.013187
leg      = 0.568*H - 0.069588
hip      = 0.067179 * exp(0.89*H)
shoulder = 0.1087 * exp(0.75*H)
```

`exp(x)` means e to the power x. In JavaScript, use `Math.exp(x)`.

```js
import { comparisonTargetsM } from './scale.mjs';
const targets = comparisonTargetsM('male', 1.80);
```

`scale.mjs` gives target numbers. `rescale.sh` makes the matching mesh.
The output JSON has `target_measurements_m.requested` (formula targets) and
`target_measurements_m.fitted` (sizes the mesh fit actually reached). Read
those numbers. Do not assume every target can fit exactly.

## Why some fitted sizes differ

The pelvis ring stays one piece. The fitter will not bend it to force every
number. At the default heights, torso and leg each miss their formula target
by the same amount:

| Body | Formula torso | Fitted torso | Formula leg | Fitted leg |
|---|---:|---:|---:|---:|
| Male, 1.722 m | 0.653546 | 0.645592 | 0.890519 | 0.882564 |
| Female, 1.594 m | 0.595050 | 0.580008 | 0.835804 | 0.820761 |

All values are metres. New output JSON tells you the new requested and fitted
values. Use `fitted` to describe the mesh that was actually made.

## What the fitter calculates

The ready-made OBJ is already fitted. For a new height, `rescale.sh` loads the
source skeleton and runs these calculations again. All values below use m.
`source_` means the saved source landmark. `target` means the formula result.

```
height_scale = H / (source_head_top_y - source_floor_y)
head_scale   = fitted_head / (source_head_top_y - source_jaw_bottom_y)
hip_scale    = fitted_hip / source_hip_width_x
shoulder_scale = target_shoulder / source_shoulder_width_x
```

So `height_scale` uses source head-top minus source floor. It does **not** use
the ready-made OBJ height as its divisor.

The pelvis ring stays one piece. First the fitter finds the target Y landmarks:

```
neck_contact_target = H - (source_head_top_y - source_neck_contact_y) * head_scale
C1_join_target      = H - (source_head_top_y - source_C1_join_y) * head_scale
C7_target           = neck_contact_target - fitted_neck
ring_height         = (source_pelvis_top_y - source_pelvis_bottom_y) * hip_scale
hip_offset          = (source_hip_center_y - source_pelvis_bottom_y) * hip_scale
epsilon             = 0.000001 * H
```

Then it splits the space below C7 between torso and leg. This is the exact
starting point and clamp:

```
pelvis_bottom_target = clamp(
  ((C7_target - target_torso) + (target_leg - hip_offset)) / 2,
  epsilon,
  C7_target - ring_height - epsilon
)
pelvis_top_target = pelvis_bottom_target + ring_height
```

The seven source Y stops are floor, pelvis bottom, pelvis top, C7, C1 join,
neck contact, and head top. Their target stops are 0, pelvis bottom target,
pelvis top target, C7 target, C1 join target, neck contact target, and H.
Between stops, Y moves by straight-line interpolation. The pelvis top-to-bottom
space uses the same `hip_scale` in X, Y, and Z.

If the head, neck, and hip requests cannot fit in order, the fitter blends
those three targets toward their source-height-scaled sizes. It picks the
largest safe blend with 48 bisection steps. It then calculates the fitted
torso and leg from the final map. That is why requested and fitted numbers can
differ.

For the shoulders and arms:

```
shoulder_target_y = mapY(mean_source_shoulder_y)
e = max(0.000000001, (source_head_top_y - source_floor_y) * 0.0000001)
shoulder_vertical_scale = (mapY(y+e) - mapY(y-e)) / (2*e)
upper_arm_scale = shoulder_scale * arm_multiplier
```

`arm_scale` is not a simple length ratio. The fitter tests the full 3D left and
right start-to-end distances after the shoulder and body maps. It finds the
`arm_scale` whose mean distance reaches the arm target, using 50 bisection
steps. If the fixed upper arm alone is too long, it also shortens the upper arm
with a separate 50-step fit. The engine handles that edge case.

## Which pieces get which fit

The exact object names are in `obj-groups.tsv`. These are the fit branches:

- `Head`, `Jaw`, `C1`: one shared head transform.
- `C2`–`C7`, `T1`–`T12`, `L1`–`L5`: body map plus each vertebra's own surface
  plane fit. It keeps the mesh from being cut at a height-map stop.
- Ribs and `Sternum`: body map. Paired ribs get their source seam offsets.
- `Pelvis_L`, `Pelvis_R`, `Sacrum`: one pelvis-ring fit; the ring is not bent.
- `Scapula_L/R`, `Clavicula_L/R`: shared shoulder frame.
- `Humerus_L/R`: shoulder frame, then upper-arm span branch.
- `Radius_L/R` and all hand pieces: arm branch around the elbow frame.
  `Radius_L/R` each include the fused Ulna shape.
- Leg and foot pieces: body map plus special shin/foot-height fit.
  `Tibia_L/R` each include the fused Fibula shape.

The head-side C1 join and neck-side C2 use their matching source landmarks and
separate fit branches. C7/T1 and L5/Sacrum are mapped by the body fit; the
vertebrae use local surface planes. This OBJ has no rig-joint records, so do
not treat a mesh seam as an animation pivot.

The exporter calls the native comparison fitter, then applies its T-pose
matrices. It writes the resulting vertices. Do not apply the fit or pose a
second time.

## Find a mesh piece

Each OBJ `o` name matches its `g` name. Names are exact and case-sensitive.
`obj-groups.tsv` lists every name for both skeletons and maps it to a category.
Use the TSV to find the exact object.

| Category | Pieces inside |
|---|---|
| `head` | `Head`, `Jaw`, `C1` |
| `neck` | `C2` through `C7` |
| `torso` | `T1` through `T12`, `L1` through `L5`, ribs, `Sternum` |
| `hip` | `Pelvis_L`, `Pelvis_R`, `Sacrum` |
| `shoulder` | scapulae and clavicles |
| `arm` | humeri, radius meshes, hand pieces |
| `leg` | femurs, patellae, tibia meshes, foot pieces |

`Radius_L/R` each show Radius plus Ulna. `Tibia_L/R` each show Tibia plus
Fibula. Those pairs are fused in the OBJ. TSV lists every exact object name.

## Units and JSON

Every position and size is in **m**. No other unit. Each JSON gives the model
name, default height, axis directions, object/vertex/triangle counts, final
mesh bounds, and requested/fitted formula measurements. Baseline JSON also
records its source landmarks and saved fit values. Bounds are the exact
`[X, Y, Z]` minimum and maximum coordinates of that OBJ.

The OBJ has separate mesh pieces, but no skin weights, moving joints, joint
pivots, or animation. It is a fitted static body. For motion, add a rig in a
separate tool.
