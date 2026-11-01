"""Select an outward-visible subset of a skull triangle surface.

The source is not a watertight anatomical segmentation. Outward orientation
and two visibility rays exclude enclosed interior and occluded surfaces, but
this conservative geometric test can omit exterior concavities and cannot
prove anatomical classification near holes. It returns actual source faces,
not a convex hull or a fitted model.
"""
import numpy as np
from trimesh.ray.ray_pyembree import RayMeshIntersector


def exterior_faces(mesh):
    if not len(mesh.faces):
        raise ValueError('Empty skull surface')
    # All enclosed interior points lie inside the external bounding box, so
    # moving them cannot change this reference point in a closed shell.
    lo, hi = mesh.vertices.min(axis=0), mesh.vertices.max(axis=0)
    center = (lo+hi)*0.5
    points = mesh.triangles_center
    normal = mesh.face_normals
    radial = points-center
    length = np.linalg.norm(radial, axis=1)
    if np.any(length < 1e-12):
        raise ValueError('Skull face lies at the reference center')
    radial = radial/length[:, None]
    outward = np.einsum('ij,ij->i', normal, radial) > 0
    eps = 1e-5  # metres; move just outside each source face
    ray = RayMeshIntersector(mesh)
    clear_normal = ~ray.intersects_any(points+normal*eps, normal)
    clear_radial = ~ray.intersects_any(points+radial*eps, radial)
    mask = outward & clear_normal & clear_radial & (mesh.area_faces > 0)
    if not np.any(mask):
        raise ValueError('No visible exterior skull triangles')
    return mask
