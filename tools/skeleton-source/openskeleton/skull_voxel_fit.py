"""3D reflection-plane registration of a voxelized outer skull surface.

Distances are measured in a 3D Euclidean distance field of occupied surface
voxels. A plane has two orientation degrees of freedom and one offset. The
initial axis is a broad left/right direction, not a body landmark. The input
is already an outer-surface triangle subset, not the enclosed skull contents.
"""
import numpy as np
from scipy.ndimage import distance_transform_edt, gaussian_filter, map_coordinates
from scipy.optimize import minimize


def voxel_surface(mesh, pitch):
    # subdivide voxelization includes unreferenced vertices unless removed;
    # these can belong to the interior triangles excluded from the fit.
    mesh = mesh.copy()
    mesh.remove_unreferenced_vertices()
    voxels = mesh.voxelized(pitch, method='subdivide')
    occupied = np.asarray(voxels.matrix, dtype=bool)
    if not occupied.any():
        raise ValueError('Empty skull surface voxelization')
    # Padding permits reflection outside the original volume. Distances are
    # capped at 6 mm below, so padding larger than the cap is sufficient.
    pad = int(np.ceil(.01 / pitch)) + 2
    occupied = np.pad(occupied, pad)
    origin = voxels.transform[:3, 3] - pad*pitch
    return occupied, origin


def register_voxels(occupied, origin, pitch, axis=np.array([1., 0., 0.])):
    """Return plane origin, normal and objective before/after registration."""
    coords = np.argwhere(occupied)
    points = origin + pitch*coords
    center = points.mean(axis=0)
    relative = points-center
    axis = np.asarray(axis, dtype=float)
    axis /= np.linalg.norm(axis)
    # A stable orthonormal basis lets the solver rotate in BOTH directions.
    helper = np.eye(3)[np.argmin(np.abs(axis))]
    tangent1 = np.cross(axis, helper); tangent1 /= np.linalg.norm(tangent1)
    tangent2 = np.cross(axis, tangent1)
    cap = .006
    # Smooth the squared distance field at sub-voxel scale to reduce grid-axis
    # locking. Occupied voxels contribute equally, independent of mesh density.
    distance = distance_transform_edt(~occupied)*pitch
    field = gaussian_filter(np.minimum(distance*distance, cap*cap), .7)
    max_index = np.array(occupied.shape)-1

    def normal(params):
        n = axis + params[0]*tangent1 + params[1]*tangent2
        return n/np.linalg.norm(n)

    def objective(params):
        n = normal(params)
        reflected = points - 2*(relative@n-params[2])[:, None]*n
        index = (reflected-origin)/pitch
        valid = np.all((index >= 0) & (index <= max_index), axis=1)
        values = np.full(len(points), cap*cap)
        values[valid] = map_coordinates(field, index[valid].T, order=1, mode='nearest', prefilter=False)
        return float(values.mean())

    # Multiple starts cover a broad range around the known source left/right
    # direction; Powell jointly searches the two 3D rotations and plane offset.
    bounds = [(-.5, .5), (-.5, .5), (-.025, .025)]
    starts = ([0,0,0], [.22,0,0], [-.22,0,0], [0,.22,0], [0,-.22,0])
    solutions = [minimize(objective, start, method='Powell', bounds=bounds,
                          options={'xtol':1e-5, 'ftol':1e-8, 'maxiter':120}) for start in starts]
    best = min(solutions, key=lambda result: result.fun)
    if not best.success:
        raise RuntimeError(f'Voxel registration failed: {best.message}')
    n = normal(best.x)
    return center + best.x[2]*n, n, objective([0,0,0]), best.fun
