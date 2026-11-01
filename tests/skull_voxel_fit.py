#!/usr/bin/env python3
"""Known 3D plane, translation, voxel match, and enclosed-interior invariance."""
from pathlib import Path
import sys
import numpy as np
import trimesh
from scipy.spatial.transform import Rotation

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/'tools/skeleton-source/openskeleton'))
from skull_exterior import exterior_faces
from skull_voxel_fit import voxel_surface, register_voxels

# A non-spherical, bilaterally symmetric lumpy shell avoids making the plane
# ambiguous; the known plane is x=0. All axes are rotated and translated.
shell = trimesh.creation.icosphere(subdivisions=3, radius=1)
v = shell.vertices.copy()
v[:, 0] *= .068*(1 + .15*v[:, 1] + .10*v[:, 2])
v[:, 1] *= .096
v[:, 2] *= .12
shell.vertices = v
rotation = Rotation.from_euler('xyz', [38, -44, 57], degrees=True).as_matrix()
translation = np.array([.21, 1.65, -.14])
shell.vertices = shell.vertices @ rotation.T + translation
true_normal = rotation[:, 0]
inside = trimesh.creation.icosphere(subdivisions=1, radius=.018)
inside.vertices = inside.vertices @ rotation.T + translation + rotation @ np.array([.02, .005, -.004])
combined = trimesh.util.concatenate([shell, inside])
mask = exterior_faces(combined)
assert mask[:len(shell.faces)].all() and not mask[len(shell.faces):].any()
outer = trimesh.Trimesh(vertices=combined.vertices, faces=combined.faces[mask], process=False)
pitch = .002
occupied, origin = voxel_surface(outer, pitch)
# Start noticeably tilted away from the correct plane in both angular axes.
initial = true_normal + .13*rotation[:, 1] - .11*rotation[:, 2]
initial /= np.linalg.norm(initial)
plane, normal, before, after = register_voxels(occupied, origin, pitch, initial)
angle = np.degrees(np.arccos(np.clip(abs(normal@true_normal), 0, 1)))
offset = abs((plane-translation)@true_normal)
assert after < before*.8, (before, after)
assert angle < 1.0 and offset < .001, (angle, offset)
# Changing the inside changes neither selected exterior nor voxel fit inputs.
inside.vertices += rotation @ np.array([-.025, .006, .008])
other = trimesh.util.concatenate([shell, inside])
other_mask = exterior_faces(other)
other_outer = trimesh.Trimesh(vertices=other.vertices, faces=other.faces[other_mask], process=False)
other_occupied, other_origin = voxel_surface(other_outer, pitch)
assert np.array_equal(occupied, other_occupied) and np.array_equal(origin, other_origin)
print(f'3D voxel mirror registration: PASS (rotated plane error {angle:.3f}°, offset {offset*1000:.3f} mm; interior ignored)')

# Re-voxelize both actual selected skull surfaces after a rigid 3D rotation,
# not merely the already-fitted plane. Voxel-grid orientation introduces small
# quantization changes, so check a sub-millimetre/sub-degree tolerance.
import importlib
import re
fitter = importlib.import_module('45-fit-skull-midplane')
text = (ROOT/'native/client/skull_midplane.h').read_text()
values = [np.array([float(n) for n in s.split(',')]) for s in re.findall(r'Vec3\{([^{}]+)\}', text)[:4]]
assert len(values) == 4
recorded = ((values[0], values[2]), (values[1], values[3]))
for sex, (expected_origin, expected_normal) in zip(('male', 'female'), recorded):
    vertices, faces = fitter.head(ROOT/f'tools/skeleton-source/skeleton-{sex}.obj')
    full = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
    selected = exterior_faces(full)
    moved = trimesh.Trimesh(vertices=vertices@rotation.T+translation, faces=faces[selected], process=False)
    grid, grid_origin = voxel_surface(moved, fitter.PITCH)
    result_origin, result_normal, start_loss, loss = register_voxels(grid, grid_origin, fitter.PITCH, rotation[:, 0])
    target_normal = rotation@expected_normal
    target_origin = rotation@expected_origin+translation
    angle = np.degrees(np.arccos(np.clip(abs(result_normal@target_normal), 0, 1)))
    offset = abs((result_origin-target_origin)@target_normal)
    assert loss < start_loss and angle < .5 and offset < .0007, (sex, angle, offset, start_loss, loss)
    print(f'{sex} rotated real skull voxel fit: PASS ({angle:.3f}°, {offset*1000:.3f} mm)')
