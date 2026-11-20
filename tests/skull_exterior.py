#!/usr/bin/env python3
"""Exterior-only fit inputs must ignore geometry enclosed by the skull shell."""
from pathlib import Path
import sys

import numpy as np
import trimesh
from trimesh.proximity import closest_point

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/'tools/skeleton-source/openskeleton'))
from skull_exterior import exterior_faces

# Closed outer surface plus two substantially different internal surfaces.
# Give the interior outward normals to make sure occlusion, not just the
# normal-direction test, excludes it. Both variants remain inside the shell.
outer = trimesh.creation.icosphere(subdivisions=2, radius=0.2)
inner1 = trimesh.creation.icosphere(subdivisions=1, radius=0.08)
inner2 = inner1.copy()
inner2.vertices *= [1.4, 0.65, 1.1]
inner2.vertices += [0.02, -0.01, 0.015]
first = trimesh.util.concatenate([outer, inner1])
second = trimesh.util.concatenate([outer, inner2])
mask1, mask2 = exterior_faces(first), exterior_faces(second)
n = len(outer.faces)
assert mask1[:n].all() and mask2[:n].all()
assert not mask1[n:].any() and not mask2[n:].any()
assert np.array_equal(mask1, mask2)

# These are the exact triangles used for sampling and as the closest-surface
# target in the fitter. Their geometry and sample distances must not change
# when only the enclosed interior changes.
mesh1 = trimesh.Trimesh(vertices=first.vertices, faces=first.faces[mask1], process=False)
mesh2 = trimesh.Trimesh(vertices=second.vertices, faces=second.faces[mask2], process=False)
assert np.array_equal(mesh1.triangles, mesh2.triangles)
center1 = np.average(mesh1.triangles_center, axis=0, weights=mesh1.area_faces)
center2 = np.average(mesh2.triangles_center, axis=0, weights=mesh2.area_faces)
assert np.array_equal(center1, center2)
rng = np.random.default_rng(44)
selected = rng.choice(len(mesh1.faces), 256, p=mesh1.area_faces/mesh1.area)
points = mesh1.triangles_center[selected]
for normal in (np.array([1.,0,0]), np.array([1.,.15,-.08])):
    normal /= np.linalg.norm(normal)
    reflected = points - 2*((points-center1)@normal)[:,None]*normal
    d1 = closest_point(mesh1,reflected)[1]
    d2 = closest_point(mesh2,reflected)[1]
    assert np.array_equal(d1,d2)
print('Exterior skull selection: PASS (enclosed asymmetric interiors do not affect fit inputs)')
