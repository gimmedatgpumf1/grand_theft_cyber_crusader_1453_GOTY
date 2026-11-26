# Blue skeletons. Use them.

This pack has two static skeleton meshes. Pick one:

- `standard_blue_male.obj` — 1.722 m tall.
- `standard_blue_female.obj` — 1.594 m tall.

Keep `standard_blue.mtl` beside the OBJ. Open the OBJ in a program that reads
Wavefront OBJ. Tell it: **1 unit = 1 m**. Axes: **X right, Y up, Z forward**.
Do not turn the axes.

## Make it taller or shorter

This pack only changes the whole skeleton size. It does not change body
proportions. It does not bend bones.

1. Read `default_height_m` in the matching JSON file.
2. Pick the new height in metres.
3. Work out: `new height / default height`.
4. Scale X, Y, and Z by that same number.
5. Scale from origin `(0, 0, 0)`. Keep the floor on Y = 0.

Example: male skeleton, new height 1.80 m:

```
1.80 / 1.722 = 1.045296...
```

Use that scale on all three axes. Do not scale each bone by itself.

## What this pack cannot do

No custom proportions. No target formulas. No `scale.mjs`. Those were removed.
This pack has no pre-fit source meshes and no instructions to rebuild the fit.

OBJ has mesh pieces, but no rig. No skin weights. No joint tree. No animation.
No joint pivots or attachment points. Use the OBJ as a fixed model. Build a
separate rig if you need it to move.

## Floor fix

Both OBJ files now touch the floor at exactly **Y = 0 m**. The female mesh had
a small gap. The whole female mesh was uniformly scaled by `1.000421815759`
and moved down to the floor. Its height stays 1.594 m. No bone was scaled by
itself. The male mesh was also set to an exact zero floor; its height stays
1.722 m.

## Find a mesh piece

Each OBJ object has an `o` name and a matching `g` name. Names are exact and
case-sensitive. `obj-groups.tsv` lists every object for both skeletons. It
maps each exact name to one category. Use that file to find a piece.

| Category | Pieces inside |
|---|---|
| `head` | Head, Jaw, C1 |
| `neck` | C2 through C7 |
| `torso` | T1 through T12, L1 through L5, ribs, Sternum |
| `hip` | Pelvis_L, Pelvis_R, Sacrum |
| `shoulder` | Scapula_L/R, Clavicula_L/R |
| `arm` | Humerus_L/R, Radius_L/R, hand pieces |
| `leg` | Femur_L/R, Patella_L/R, Tibia_L/R, foot pieces |

The TSV gives every full name. `Radius_L/R` each show Radius plus Ulna.
`Tibia_L/R` each show Tibia plus Fibula. Ulna and Fibula are fused into those
meshes. They are not extra OBJ objects.

## JSON: exact rules

Open `standard_blue_male.json` or `standard_blue_female.json`. Both follow
this JSON Schema. No extra fields:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "additionalProperties": false,
  "required": ["name", "license", "default_height_m", "units", "axes", "format", "objects", "vertices", "triangles", "mesh_bounds_m"],
  "properties": {
    "name": {"enum": ["standard_blue_male", "standard_blue_female"]},
    "license": {"const": "CC BY 4.0"},
    "default_height_m": {"type": "number", "exclusiveMinimum": 0},
    "units": {"const": "m"},
    "axes": {
      "type": "object",
      "additionalProperties": false,
      "required": ["x", "y", "z"],
      "properties": {
        "x": {"const": "right"},
        "y": {"const": "up"},
        "z": {"const": "forward"}
      }
    },
    "format": {"const": "OBJ + MTL"},
    "objects": {"type": "integer", "minimum": 1},
    "vertices": {"type": "integer", "minimum": 1},
    "triangles": {"type": "integer", "minimum": 1},
    "mesh_bounds_m": {
      "type": "object",
      "additionalProperties": false,
      "required": ["min", "max"],
      "properties": {
        "min": {"$ref": "#/$defs/vector3"},
        "max": {"$ref": "#/$defs/vector3"}
      }
    }
  },
  "$defs": {
    "vector3": {
      "type": "array",
      "minItems": 3,
      "maxItems": 3,
      "items": {"type": "number"}
    }
  }
}
```

All heights and bounds are in metres. `default_height_m` equals max Y minus
min Y. `mesh_bounds_m` gives the exact bounds of the matching OBJ.

## Exact bounds

| File | X min | Y min | Z min | X max | Y max | Z max |
|---|---:|---:|---:|---:|---:|---:|
| Male | -0.940563335850 | 0 | -0.072302952115 | 0.940563335850 | 1.722 | 0.228575514880 |
| Female | -0.885949259927 | 0 | -0.073756497598 | 0.885949259927 | 1.594 | 0.188323057949 |

Use the JSON numbers for full precision. These bounds describe the files as
they are now.