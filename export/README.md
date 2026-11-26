# Blue skeletons: do this

## 1. Open one

Keep these files side by side:

- `standard_blue_male.obj`
- `standard_blue_female.obj`
- `standard_blue.mtl`
- `standard_blue_male.json`
- `standard_blue_female.json`
- `obj-groups.tsv`
- `scale.mjs`

Pick one OBJ. Open it in a 3D program that reads Wavefront OBJ. Keep the MTL
beside it so the blue material can load. If the program asks for units, choose
metres: **1 OBJ unit = 1 m**. Do not flip the axes: **X right, Y up, Z forward**.
The OBJ already has its final vertex positions. Do not apply the comparison
transforms a second time.

The male profile height is **1.722 m**. The female profile height is
**1.594 m**. These are the heights used to make the exports. The JSON files
show the exact mesh bounds. Female bottom Y is `0.000672 m`, not exactly zero.

## 2. Find an object

Every OBJ object has an `o` name and a matching `g` name. Names are case-
sensitive. Both skeletons use these same 178 names. The list below is the
full name-to-category map. `obj-groups.tsv` repeats it, one row per object and
sex, and tells what fused geometry each object shows.

- `head`: `Head`, `Jaw`, `C1`
- `neck`: `C2`, `C3`, `C4`, `C5`, `C6`, `C7`
- `torso`: `T1`, `T2`, `T3`, `T4`, `T5`, `T6`, `T7`, `T8`, `T9`, `T10`, `T11`, `T12`, `L1`, `L2`, `L3`, `L4`, `L5`, `Sternum`, `Rib1_L`, `Rib2_L`, `Rib3_L`, `Rib4_L`, `Rib5_L`, `Rib6_L`, `Rib7_L`, `Rib8_L`, `Rib9_L`, `Rib10_L`, `Rib11_L`, `Rib12_L`, `Rib1_R`, `Rib2_R`, `Rib3_R`, `Rib4_R`, `Rib5_R`, `Rib6_R`, `Rib7_R`, `Rib8_R`, `Rib9_R`, `Rib10_R`, `Rib11_R`, `Rib12_R`
- `hip`: `Pelvis_L`, `Pelvis_R`, `Sacrum`
- `shoulder`: `Scapula_L`, `Scapula_R`, `Clavicula_L`, `Clavicula_R`
- `arm`: `Humerus_L`, `Humerus_R`, `Radius_L`, `Radius_R`, `HandCapitatum_L`, `HandDistP1_L`, `HandDistP2_L`, `HandDistP3_L`, `HandDistP4_L`, `HandDistP5_L`, `HandHamata_L`, `HandLuna_L`, `HandMeta1_L`, `HandMeta2_L`, `HandMeta3_L`, `HandMeta4_L`, `HandMeta5_L`, `HandMidP2_L`, `HandMidP3_L`, `HandMidP4_L`, `HandMidP5_L`, `HandPisiform_L`, `HandProxP1_L`, `HandProxP2_L`, `HandProxP3_L`, `HandProxP4_L`, `HandProxP5_L`, `HandScaphoid_L`, `HandTrapezium_L`, `HandTrapezoideum_L`, `HandTriquetrum_L`, `HandCapitatum_R`, `HandDistP1_R`, `HandDistP2_R`, `HandDistP3_R`, `HandDistP4_R`, `HandDistP5_R`, `HandHamata_R`, `HandLuna_R`, `HandMeta1_R`, `HandMeta2_R`, `HandMeta3_R`, `HandMeta4_R`, `HandMeta5_R`, `HandMidP2_R`, `HandMidP3_R`, `HandMidP4_R`, `HandMidP5_R`, `HandPisiform_R`, `HandProxP1_R`, `HandProxP2_R`, `HandProxP3_R`, `HandProxP4_R`, `HandProxP5_R`, `HandScaphoid_R`, `HandTrapezium_R`, `HandTrapezoideum_R`, `HandTriquetrum_R`
- `leg`: `Femur_L`, `Femur_R`, `Patella_L`, `Patella_R`, `Tibia_L`, `Tibia_R`, `Cal_L`, `Cub_L`, `FootDistP1_L`, `FootDistP2_L`, `FootDistP3_L`, `FootDistP4_L`, `FootDistP5_L`, `FootMeta1_L`, `FootMeta2_L`, `FootMeta3_L`, `FootMeta4_L`, `FootMeta5_L`, `FootMidP2_L`, `FootMidP3_L`, `FootMidP4_L`, `FootMidP5_L`, `FootProxP1_L`, `FootProxP2_L`, `FootProxP3_L`, `FootProxP4_L`, `FootProxP5_L`, `FootSesaLat_L`, `FootSesaMed_L`, `InterCun_L`, `LatCun_L`, `MedCun_L`, `Nav_L`, `Tal_L`, `Cal_R`, `Cub_R`, `FootDistP1_R`, `FootDistP2_R`, `FootDistP3_R`, `FootDistP4_R`, `FootDistP5_R`, `FootMeta1_R`, `FootMeta2_R`, `FootMeta3_R`, `FootMeta4_R`, `FootMeta5_R`, `FootMidP2_R`, `FootMidP3_R`, `FootMidP4_R`, `FootMidP5_R`, `FootProxP1_R`, `FootProxP2_R`, `FootProxP3_R`, `FootProxP4_R`, `FootProxP5_R`, `FootSesaLat_R`, `FootSesaMed_R`, `InterCun_R`, `LatCun_R`, `MedCun_R`, `Nav_R`, `Tal_R`

`Radius_L` and `Radius_R` each show Radius plus Ulna. `Tibia_L` and `Tibia_R`
each show Tibia plus Fibula. Ulna and Fibula are fused into those shown
meshes; they are not extra OBJ objects.

## 3. Change only the whole height

For a simple taller or shorter copy, use one scale on X, Y, and Z. Keep the
origin fixed. Use:

```
scale = wanted_height_m / default_height_m
```

Use `1.722` for male `default_height_m`; use `1.594` for female. Multiply each
vertex's X, Y, and Z by that same scale. This keeps the shape and all gaps the
same. It does not change the skeleton's proportions.

For example, male at `1.80 m`:

```
scale = 1.80 / 1.722 = 1.045296...
```

If your program has a height field, enter the wanted height and keep uniform
scaling on. For 1.80 m, type `1.80`, not `180` or `1800`.

## 4. Read the proportion targets

`scale.mjs` calculates seven target measurements for a height `H` in metres.
It only gives numbers. It does **not** resize the OBJ. Do not scale each
category by its returned number; these measurements overlap and are not seven
separate bone lengths.

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

## 5. Landmarks and transform notes

Open the matching JSON file. All coordinates end in `_m` because they are in
metres. An XYZ point is `[X, Y, Z]`.

- `source_landmarks_m` gives the recorded source landmarks: floor, head top,
jaw bottom, C1 join, pelvis bottom and top, C7, neck contact, scapula bottom
and top, hip width, shoulder width, and body center X.
- `vertical_map_source_y_m` and `vertical_map_target_y_m` are matching lists.
Their order is: floor, pelvis bottom, pelvis top, C7, C1 join, neck contact,
head top. The target list gives the Y stops used by the body-height fit.
- `transform_parameters` gives the saved height, head, hip, shoulder, arm, and
upper-arm scale factors, plus shoulder target Y and shoulder vertical scale.
These are the values used for that export, not instructions to run them again.
- `arm_points_m` gives left and right shoulder, elbow, start, and end points.
Copy the XYZ values from JSON when you need those exact export landmarks.

Here is what each measurement means. These are the rules used for this export:

| Name | Measure it this way |
|---|---|
| `head` | Head top Y minus Jaw bottom Y. |
| `neck` | Mean left/right C1 neck-contact Y minus mean left/right C7 Y. |
| `torso` | Target C7 Y minus target pelvis-bottom Y. |
| `arm` | Mean left/right 3D straight-line distance from the inner Humerus point to the outer tip of `HandDistP3`. |
| `leg` | Mean left/right hip-centre Y above the floor. |
| `hip` | Full X width of `Pelvis_L` and `Pelvis_R` together. |
| `shoulder` | Full X width of `Scapula_L` and `Scapula_R` together. |

Landmark rules: floor Y is the lowest Y vertex in the source mesh. Head top
is the highest Y vertex in `Head`; Jaw bottom is the lowest in `Jaw`; C1 join
is the lowest in `C1`. Pelvis and scapula top/bottom are the highest and lowest
Y over the named left/right meshes. Hip and shoulder widths are the greatest
X minus the least X over those same mesh pairs. `center_x` is the mean X of
the two hip centres. C7 and neck-contact points come from the left/right
skeleton landmark records after the T-pose transform; the saved Y/Z contact
values are their means. The JSON gives the actual coordinates for both bodies.

Arm point rules: shoulder points are the averaged left/right shoulder landmarks;
the right point mirrors the left around `center_x`. Elbow points use averaged
elbow landmarks. Elbow X is moved to the closest Radius/Humerus surface pair;
the right point mirrors the left. `left_start` is the least-X point on
`Humerus_L`; `right_start` is the greatest-X point on `Humerus_R`.
`left_end` is the greatest-X point on `HandDistP3_L`; `right_end` is the
least-X point on `HandDistP3_R`. These are fit landmarks, not rig joints.

The fit uses one map for the body. It does not move each object as a rigid
block. For a source Y between two listed stops, the body Y rule is:

```
Ynew = targetY[i] + (Ysource - sourceY[i])
       * (targetY[i+1] - targetY[i]) / (sourceY[i+1] - sourceY[i])
```

Past the first or last stop, extend that end's line. For the head group
(`Head`, `Jaw`, `C1`), the shared rule is:

```
Xnew = (Xsource - center_x) * head_scale
Ynew = H - (head_top_y - Ysource) * head_scale
Znew = neck_contact_z * height_scale
       + (Zsource - neck_contact_z) * head_scale
```

For other body parts, use these rules. `scale_for_Y` means the scale for the
source-Y band listed below.

```
Xnew = (Xsource - center_x) * scale_for_Y
Ynew = the seven-stop rule above
Znew = neck_contact_z * height_scale
       + (Zsource - neck_contact_z) * depth_scale_for_Y
```

X bands: pelvis and below use `hip_scale`; pelvis top to scapula bottom blends
from `hip_scale` to `shoulder_scale`; scapula bottom to top uses
`shoulder_scale`; scapula top to C1 join blends from `shoulder_scale` to
`head_scale`; above C1 join uses `head_scale`. Z bands: pelvis and below use
`hip_scale`; pelvis top to scapula bottom blends from `hip_scale` to
`height_scale`; through the scapula band use `height_scale`; scapula top to C1
join blends from `height_scale` to `head_scale`; above C1 join use
`head_scale`.

Arms and shoulder parts use one shoulder frame. Shoulder and elbow points are
in `arm_points_m`. Left side is `+1`; right side is `-1`.

```
shoulderY = mean(left_shoulder.y, right_shoulder.y)
boundary = shoulderX + side * abs(startX - shoulderX)
upperX(x) = (boundary - center_x) * shoulder_scale
            + (x - boundary) * upper_arm_scale
```

For the humerus, use `upperX(x)` past the boundary; on the shoulder side use
`(x - center_x) * shoulder_scale`. For forearm and hand, use:

```
Xnew = upperX(elbowX) + (Xsource - elbowX) * arm_scale
Ynew = shoulder_target_y + (Ysource - shoulderY) * shoulder_vertical_scale
Znew = neck_contact_z * height_scale
       + (Zsource - neck_contact_z) * height_scale
```

The scapula and clavicle use the shoulder scale for X and the same shoulder
Y/Z frame. The fit mirrors paired arm points around `center_x`.

These rules explain the comparison fit; they are not a recipe for editing this
OBJ by hand. The exporter also fits vertebra surfaces and foot height, then
applies the T-pose. To make a new proportion fit, use a tool that implements
the full skeleton fit. `scale.mjs` alone only returns target numbers.

## 6. Pivots and joints: important

These are static OBJ meshes. OBJ here has **no rig, no skin weights, no
animation, no joint hierarchy, and no saved pivot objects**. The JSON arm
points are measured coordinates; they are **not** joint attachments or usable
rig pivots. The shared shoulder and elbow frames describe how the export fit
keeps parts lined up; they do not create a rig in the OBJ.

So: use the OBJ as a fixed model, or build a rig separately in your 3D
program. For a rig, set and test real joint pivots there. Do not assume the
JSON points attach the meshes. This export does not include exact per-bone
joint attachment records.

## Baseline numbers

All values below are in metres. The JSON files hold more digits.

| Measure | Male | Female |
|---|---:|---:|
| Profile height | 1.722 | 1.594 |
| Final X min / max | -0.940563 / 0.940563 | -0.885576 / 0.885576 |
| Final Y min / max | about 0 / 1.722000 | 0.000672 / 1.594000 |
| Final Z min / max | -0.072303 / 0.228576 | -0.073725 / 0.188244 |
| Source floor Y | 0.080375 | 0.079192 |
| Source Head top Y | 1.801084 | 1.741136 |
| Source Jaw bottom Y | 1.589284 | 1.533621 |
| Source C1 join Y | 1.629787 | 1.574023 |
| Source pelvis bottom / top Y | 0.899242 / 1.119840 | 0.834421 / 1.039149 |
| Source C7 Y | 1.553619 | 1.481725 |
| Source neck contact Y / Z | 1.649626 / 0.043218 | 1.597264 / 0.038295 |
| Source hip width X | 0.273490 | 0.295275 |
| Source shoulder width X | 0.379641 | 0.359937 |

| Target | Male asked | Male fitted | Female asked | Female fitted |
|---|---:|---:|---:|---:|
| Head | 0.218394 | 0.218394 | 0.208859 | 0.208859 |
| Neck | 0.107371 | 0.107371 | 0.105784 | 0.105784 |
| Torso | 0.653546 | 0.645592 | 0.595050 | 0.580008 |
| Arm | 0.791604 | 0.791604 | 0.741413 | 0.741413 |
| Leg | 0.890519 | 0.882564 | 0.835804 | 0.820761 |
| Hip width | 0.279188 | 0.279188 | 0.277555 | 0.277555 |
| Shoulder width | 0.403648 | 0.403648 | 0.359276 | 0.359276 |

Torso and leg asked values do not match the fitted values. The pelvis ring
stays together, so the fit changes those two results. Use the `fitted` numbers
when you mean the size in these OBJ files. Do not say every asked target was
hit exactly.
