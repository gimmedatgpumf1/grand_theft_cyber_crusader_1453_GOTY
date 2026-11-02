# Offline toe visual study — unfinished

This tool is for shaping and looking at a new surface around the existing blue
comparison phalanges. It is **not included in the game**, and its output is not
a finished replacement. The current study still has an artificial cut at the
rear, imperfect toe separations and a nail/skin transition that needs visual
work. The bottom-up body and individual toe animation remain unfinished.

The inputs are the male/female blue OBJ exports produced by
`tests/skeleton_cranial_export.cpp` in `--all` mode. They are individual mapped
comparison bones, not a generic foot mesh. The sculpting fields and nail plates
are generated from those bones; their soft-tissue dimensions are artistic
estimates, not subject-specific anatomical measurements. The original white
skeleton is not modified.

With Python packages NumPy, SciPy, scikit-image, trimesh, rtree and Pillow
installed, and a directory of blue exports at 170 cm:

```text
python tools/toe_visual_study.py <bone-exports> <output-directory> --sex female
python tools/toe_visual_study.py <bone-exports> <other-output-directory> --sex male
```

Each output directory receives a skin OBJ, five separately closed 3D nail
OBJs, and dorsal, side and plantar PNGs. The study renderer uses a per-pixel
depth buffer so overlapping triangles are not ordered by their centroids. Nail
vertex colors are a visual study only and are not integrated into the game.
Use an output directory outside the repository. `--scale` is the export height divided by 170; different heights
are experimental and not visually approved. The images use an offline software
illustration, not the game renderer or the user's graphics hardware. They do
not show deformation, joint behavior, skin/bone fit, collisions, or the full
body. The user-supplied dorsal and plantar photographs remain the visual
reference; no generic character or foot mesh is used.
