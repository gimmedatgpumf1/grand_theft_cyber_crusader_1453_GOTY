"""Audit bone triangle collisions during a simplified knee-axis sweep.

Requires NumPy, SciPy and python-fcl. Input faces are MATLAB 1-based. This tests
intersections of the triangle surfaces, not soft tissue, full solid containment,
or true subject-specific joint kinematics. Angles are relative to the source
pose; this is not a physiological ROM test. It does not modify the meshes.
"""
from pathlib import Path
import json
import numpy as np
import fcl
from scipy.spatial.transform import Rotation

BASE = Path(__file__).resolve().parent.parent


def collision_object(vertices, faces):
    vertices = np.asarray(vertices, dtype=np.float64)
    faces = np.asarray(faces)
    if vertices.ndim != 2 or vertices.shape[1] != 3 or not len(vertices):
        raise ValueError('invalid source vertices')
    if faces.ndim != 2 or faces.shape[1] != 3 or not len(faces) or faces.dtype.kind not in 'iu':
        raise ValueError('invalid source faces')
    if faces.min() < 1 or faces.max() > len(vertices):
        raise ValueError('source faces must be MATLAB 1-based indices')
    model = fcl.BVHModel()
    model.beginModel(len(vertices), len(faces))
    model.addSubModel(vertices, np.asarray(faces, dtype=np.int32)-1)
    model.endModel()
    return fcl.CollisionObject(model)


def audit(store, side, knee, angles=range(0,121,5)):
    objects = {}
    for name in ('Femur', 'Tibia'):
        key = name+'_'+side
        objects[name] = collision_object(store['v_'+key], store['f_'+key])
    femur, tibia = objects['Femur'], objects['Tibia']
    center = np.asarray(knee['axis_origin_mm'], dtype=float)
    axis = np.asarray(knee['axis_medial_direction'], dtype=float) * (-1 if side == 'L' else 1)
    if not np.isfinite(center).all() or not np.isfinite(axis).all() or abs(np.linalg.norm(axis)-1) > 1e-6:
        raise ValueError('invalid knee frame')
    rows = []
    for angle in angles:
        rotation = Rotation.from_rotvec(axis*np.radians(angle)).as_matrix()
        tibia.setRotation(rotation)
        tibia.setTranslation(center-rotation@center)
        result = fcl.CollisionResult()
        contacts = fcl.collide(femur, tibia, fcl.CollisionRequest(num_max_contacts=10000, enable_contact=True), result)
        distance = fcl.distance(femur, tibia, fcl.DistanceRequest(), fcl.DistanceResult())
        rows.append({'relative_degrees': angle, 'intersecting_triangle_pairs_up_to_10000': int(contacts),
                     'surface_distance_mm': float(distance)})
    return rows


def main():
    for sex, path in (('male','male-N197925.npz'), ('female','skelly-out.npz')):
        fits = json.loads((BASE/f'skeleton-{sex}-knee-fit.json').read_text())['knees']
        with np.load(BASE/'openskeleton/data'/path) as source:
            for side in ('L','R'):
                rows = audit(source, side, fits[side])
                colliding = [r['relative_degrees'] for r in rows if r['intersecting_triangle_pairs_up_to_10000']]
                print(sex, side, 'source-pose surface gap', f"{rows[0]['surface_distance_mm']:.3f} mm;",
                      'intersections at relative degrees', colliding)


if __name__ == '__main__': main()
