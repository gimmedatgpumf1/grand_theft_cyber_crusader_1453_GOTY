"""Construct meshes from OpenSkeleton's MATLAB-style 1-based face arrays."""
import numpy as np
import trimesh


def source_mesh(vertices, source_faces):
    vertices = np.asarray(vertices)
    faces = np.asarray(source_faces)
    if vertices.ndim != 2 or vertices.shape[1] != 3 or len(vertices) == 0:
        raise ValueError('invalid source vertices')
    if faces.ndim != 2 or faces.shape[1] != 3 or len(faces) == 0 or faces.dtype.kind not in 'iu':
        raise ValueError('invalid source faces')
    if faces.min() < 1 or faces.max() > len(vertices):
        raise ValueError('source faces must be MATLAB 1-based indices')
    return trimesh.Trimesh(vertices=vertices, faces=faces.astype(np.int64) - 1, process=False)
