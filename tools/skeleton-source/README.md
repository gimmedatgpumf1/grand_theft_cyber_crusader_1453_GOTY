Pinned low-poly anatomical skeleton, 2862 triangles.

`skeleton.obj` - the body, feet at y = 0, 1.71 m tall, facing +z, one `o` group
per bone so a rig can bind by name instead of guessing from coordinates.

Groups: skull, mandible, teeth, spine_cervical, spine_thoracic, spine_lumbar,
ribs_l/r, sternum, scapula_l/r, clavicle_l/r, pelvis, humerus_l/r,
forearm_l/r, hand_l/r, femur_l/r, patella_l/r, lower_leg_l/r, foot_l/r.

`skeleton.parts.json` - per-group triangle counts and the bounding box.
`prepare.mjs` - the decimation script that produced the OBJ from
open-anatomy-3d; rerunning it reproduces the file byte for byte.
`ATTRIBUTION.md` - provenance and the required CC BY 4.0 credit line.
