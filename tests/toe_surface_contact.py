#!/usr/bin/env python3
"""Optional FCL audit of exported toes (requires numpy, trimesh, rtree, python-fcl).

Run toe_surface_export first. Bone vertices at exactly the comparison floor
are allowed to touch the sole: the inherited skeleton has no space below them.
This is a geometric check, not evidence of anatomical or visual accuracy.
"""
import itertools
import pathlib
import sys

import numpy as np
import trimesh

root = pathlib.Path(sys.argv[1])
floor_contacts = []
for sex in ("male", "female"):
    for side in ("L", "R"):
        skins = []
        for digit in range(1, 6):
            stem = root / f"{sex}_{digit}_{side}"
            skin = trimesh.load(str(stem) + "_skin.obj", process=False)
            # Trimesh's fixed numerical tolerances are unreliable for tiny
            # triangles in metres at the 50 cm profile. Work in millimetres.
            skin.apply_scale(1000)
            assert skin.is_watertight and skin.is_winding_consistent, stem
            assert np.isfinite(skin.vertices).all() and skin.area_faces.min() > 1e-10, stem
            assert skin.vertices[:, 1].min() >= -1e-7, stem
            manager = trimesh.collision.CollisionManager()
            manager.add_object("skin", skin)
            nails = trimesh.load(str(stem) + "_nail.obj", process=False)
            nails.apply_scale(1000)
            assert np.isfinite(nails.vertices).all() and nails.area_faces.min() > 1e-10, stem
            assert not manager.in_collision_single(nails), (stem, "nail cuts skin")
            for segment in ("FootProxP", "FootMidP", "FootDistP"):
                path = pathlib.Path(str(stem) + f"_{segment}.obj")
                if not path.exists():
                    assert digit == 1 and segment == "FootMidP"
                    continue
                bone = trimesh.load(path, process=False)
                bone.apply_scale(1000)
                outside = bone.vertices[~skin.contains(bone.vertices)]
                if len(outside):
                    _, distance, _ = trimesh.proximity.closest_point(skin, outside)
                    assert np.all(np.abs(outside[:, 1]) < 1e-6) and np.all(distance < 1e-5), (path, outside, distance)
                hit = manager.in_collision_single(bone)
                if hit:
                    # Triangles can touch at a bone vertex on the floor. With
                    # no vertical room for tissue, this is not full clearance.
                    assert len(outside) and np.all(np.abs(outside[:, 1]) < 1e-6), path
                    floor_contacts.append(str(path.name))
            skins.append(skin)
        for (a, left), (b, right) in itertools.combinations(enumerate(skins, 1), 2):
            manager = trimesh.collision.CollisionManager()
            manager.add_object("left", left)
            assert not manager.in_collision_single(right), (sex, side, a, b, "toes intersect")
print(f"Toe surface FCL: PASS; inherited bone-to-floor contact on {floor_contacts}")
