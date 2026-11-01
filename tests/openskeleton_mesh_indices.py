"""Reject MATLAB/NumPy face-index mistakes in the overlap audit."""
import importlib.util
from pathlib import Path
import sys

try:
    import numpy as np
    import trimesh
except ImportError:
    print('OpenSkeleton mesh indices: SKIP (numpy or trimesh not installed)')
    raise SystemExit(0)

path = Path(__file__).resolve().parent.parent / 'tools/skeleton-source/openskeleton/source_mesh.py'
spec = importlib.util.spec_from_file_location('source_mesh', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
original = trimesh.creation.box(extents=(2, 2, 2))
faces = original.faces + 1
mesh = module.source_mesh(original.vertices, faces)
assert np.array_equal(mesh.faces, original.faces)
assert mesh.is_watertight and mesh.is_winding_consistent
try:
    mesh.contains(np.array([[0., 0., 0.]]))
except ModuleNotFoundError:
    print('OpenSkeleton mesh indices: PASS (ray backend unavailable)')
else:
    assert mesh.contains(np.array([[0., 0., 0.], [2., 0., 0.]])).tolist() == [True, False]
for bad in (faces-1, faces+len(original.vertices)):
    try:
        module.source_mesh(original.vertices, bad)
    except ValueError:
        pass
    else:
        raise AssertionError('accepted out-of-range face indices')
print('OpenSkeleton mesh indices: PASS (1-based faces, closed cube)')
