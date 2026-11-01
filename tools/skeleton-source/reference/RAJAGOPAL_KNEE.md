# Reference knee kinematics

`tests/rajagopal_knee_cases.txt` contains numerical fixtures generated with
OpenSim 4.6 from the bilateral knee joints in
[Rajagopal et al.'s full-body model](https://nmbl.stanford.edu/wp-content/uploads/07505900.pdf).
The exact model copy is
[stanfordnmbl/coupled-exo-sim, commit de55d59962450f0d8a64005db42cb72c01ffe849](https://github.com/stanfordnmbl/coupled-exo-sim/blob/de55d59962450f0d8a64005db42cb72c01ffe849/Rajagopal2015.osim),
SHA-256 `3d646de18f3014343dc4d1f7c879e3d50d3f17f55ceceb92f93f32b26b76e869`.
Regenerate the fixtures with
`python3 tools/skeleton-source/reference/generate_rajagopal_knee_cases.py`
(OpenSim 4.6 and network access required). The original model itself is not
included here.

Unlike a fixed hinge, the reference knee combines three coordinate-driven
rotations and two coordinate-driven translations. `CalibratedSpatialJoint`
reproduces these reference-model transforms in the open-chain solver, with
OpenSim comparison tests for both sides. It does **not** fit this trajectory to
either source subject, determine their neutral or actual range of motion, or
provide cartilage contact, knee ligaments or patellar motion. The two source
models therefore do not yet use these reference curves.
