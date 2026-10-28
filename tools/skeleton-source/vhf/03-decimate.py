"""Cluster-decimate the dense segmented skeleton to the game's triangle budget.

Each bone shell is mesh-simplified on its own, so a phalanx keeps its own shape
and cannot be averaged away with the skull next to it. The method is grid
clustering: vertices that fall in the same cell of a cubic grid merge into one
vertex at their centroid, and triangles that lose a corner are dropped. It is
crude but it never produces self-intersections, and on surfaces this small the
difference from a proper quadric decimator is not visible at gameplay distance.

The cell size is found per shell by bisection on the resulting triangle count.
"""
import json
import numpy as np

BUDGET = {
    'skull': 2400, 'mandible': 240, 'teeth': 420,
    'spine_cervical': 520, 'spine_thoracic': 1100, 'spine_lumbar': 620,
    'ribs_l': 700, 'ribs_r': 700, 'sternum': 160,
    'scapula_l': 340, 'scapula_r': 340, 'clavicle_l': 90, 'clavicle_r': 90,
    'pelvis': 900,
    'humerus_l': 150, 'humerus_r': 150, 'forearm_l': 180, 'forearm_r': 180,
    'hand_l': 760, 'hand_r': 760,
    'femur_l': 200, 'femur_r': 200, 'patella_l': 60, 'patella_r': 60,
    'lower_leg_l': 220, 'lower_leg_r': 220, 'foot_l': 760, 'foot_r': 760,
}
FLOOR_TRIANGLES = 12


def shells(faces):
    parent = {}
    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for f in faces:
        for v in f:
            parent.setdefault(int(v), int(v))
    for f in faces:
        a = find(int(f[0]))
        parent[find(int(f[1]))] = a
        parent[find(int(f[2]))] = a
    groups = {}
    for f in faces:
        groups.setdefault(find(int(f[0])), []).append(f)
    return list(groups.values())


def cluster(points, faces, pitch):
    """Merge vertices inside cubes of side `pitch`, then drop collapsed triangles."""
    keys = np.floor(points / pitch).astype(np.int64)
    _, inverse, counts = np.unique(keys, axis=0, return_inverse=True, return_counts=True)
    inverse = inverse.ravel()
    order = np.argsort(inverse)
    sums = np.zeros((len(counts), 3))
    np.add.at(sums, inverse, points)
    merged = sums / counts[:, None]
    remapped = inverse[np.asarray(faces)]
    good = (remapped[:, 0] != remapped[:, 1]) & (remapped[:, 1] != remapped[:, 2]) & (remapped[:, 0] != remapped[:, 2])
    return merged, remapped[good]


def decimate(points, faces, budget):
    if len(faces) <= budget:
        return points, faces
    lo, hi = 0.0005, 0.08
    best = None
    for _ in range(22):
        mid = (lo + hi) * 0.5
        p, f = cluster(points, faces, mid)
        if len(f) > budget:
            lo = mid
        else:
            hi = mid
            if len(f) >= FLOOR_TRIANGLES:
                best = (p, f)
        if len(p) <= 3:
            break
    if best is None:
        return points, faces
    return best


def decimate_group(points, faces, budget):
    """Decimate each connected shell on its own, so no bone is lost to its neighbour."""
    groups = shells(faces)
    if len(groups) == 1:
        return decimate(points, faces, budget)
    total = len(faces)
    kept_points = []
    kept_faces = []
    offset = 0
    for group in groups:
        group = np.asarray(group)
        used = np.unique(group)
        local = np.zeros(used.max() + 1, dtype=np.int64)
        local[used] = np.arange(len(used))
        sub_points = points[used]
        sub_faces = local[group]
        share = max(FLOOR_TRIANGLES, int(round(budget * len(group) / total)))
        p, f = decimate(sub_points, sub_faces, share)
        kept_points.append(p)
        kept_faces.append(f + offset)
        offset += len(p)
    return np.concatenate(kept_points), np.concatenate(kept_faces)


if __name__ == '__main__':
    import sys
    SOURCE = sys.argv[1] if len(sys.argv) > 1 else '/tmp/vhf/dense.obj'
    TARGET = sys.argv[2] if len(sys.argv) > 2 else '/tmp/vhf/female.obj'
    points_by_group = {}
    faces_by_group = {}
    base_by_group = {}
    current = None
    written = 0                      # vertices already written before this group
    for line in open(SOURCE):
        if line.startswith('o '):
            current = line[2:].strip()
            points_by_group[current] = []
            faces_by_group[current] = []
            base_by_group[current] = written
        elif line.startswith('v ') and current:
            points_by_group[current].append([float(x) for x in line.split()[1:4]])
            written += 1
        elif line.startswith('f ') and current:
            # OBJ indices are file-global; every group has its own vertex array.
            base = base_by_group[current]
            faces_by_group[current].append([int(t.split('/')[0]) - 1 - base for t in line.split()[1:4]])

    lines = ['# Structure of the female skeleton, segmented from her own CT.',
             '# Source: Visible Human Project (NLM), CT in COMODO-research/Comodo_VisibleHuman.',
             '# Bone names from Z-Anatomy (CC BY-SA 4.0); no geometry taken from it.',
             '# See tools/skeleton-source/ATTRIBUTION.md.']
    report = []
    running = 1
    total_points = 0
    total_faces = 0
    low = np.array([np.inf] * 3)
    high = np.array([-np.inf] * 3)
    for name in sorted(points_by_group):
        points = np.asarray(points_by_group[name], dtype=np.float64)
        faces = np.asarray(faces_by_group[name], dtype=np.int64)
        p, f = decimate_group(points, faces, BUDGET.get(name, 200))
        lines.append(f'o {name}')
        for v in p:
            lines.append(f'v {v[0]:.5f} {v[1]:.5f} {v[2]:.5f}')
        for t in f:
            lines.append(f'f {running + t[0]} {running + t[1]} {running + t[2]}')
        running += len(p)
        total_points += len(p)
        total_faces += len(f)
        low = np.minimum(low, p.min(0))
        high = np.maximum(high, p.max(0))
        report.append({'name': name, 'triangles': int(len(f)), 'vertices': int(len(p))})

    # Feet on the floor, laterally centred. A translation only: the height is a
    # measurement of this woman and nothing here is allowed to change it.
    shift = np.array([-(low[0] + high[0]) / 2, -low[1], 0.0])
    out = []
    for line in lines:
        if line.startswith('v '):
            v = np.array([float(x) for x in line.split()[1:4]]) + shift
            out.append(f'v {v[0]:.5f} {v[1]:.5f} {v[2]:.5f}')
        else:
            out.append(line)
    open(TARGET, 'w').write('\n'.join(out) + '\n')

    json.dump({'source': 'Visible Human Female CT (Visible Human Project, NLM)',
               'names': 'Z-Anatomy (CC BY-SA 4.0)',
               'units': 'metres, soles on y = 0, facing +z',
               'vertices': total_points, 'triangles': total_faces,
               'height': float(high[1] - low[1]),
               'bbox': {'min': (low + shift).tolist(), 'max': (high + shift).tolist()},
               'parts': report},
              open(TARGET.replace('.obj', '.parts.json'), 'w'), indent=1)
    print(f'parts {len(report)} vertices {total_points} triangles {total_faces} '
          f'height {(high[1] - low[1]):.4f} m')
