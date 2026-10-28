"""Turn the labelled slabs into one mesh per bone, grouped into the rig's parts.

Input is what segments.py produced (label slabs on disk plus a component table);
the anatomical atlas is only used to *name* pieces, never for shape.

  1. Fused chains (an arm reduced to one blob by partial volume at the joints)
     are split by eroding until the bones come apart and growing those cores
     back over the whole component with a watershed, which puts the cut exactly
     where the two bones meet instead of on an arbitrary plane.
  2. Every piece is named against the atlas by centroid, globally assigned.
  3. Marching cubes runs per piece inside its own bounding box, so a 0.7 mm
     scan never forces a whole-body grid.
  4. Each bone is cluster-decimated on its own towards the part's triangle budget.

Usage:
  python3 build2.py --table table-vhf.json --labels /tmp/vhf/lab-vhf \
      --atlas tools/skeleton-source/skeleton-male.obj \
      --obj skeleton-female.obj --parts skeleton-female.parts.json
"""
import argparse
import json

import numpy as np
from scipy import ndimage as ndi
from scipy.optimize import linear_sum_assignment
from skimage import measure
from skimage.segmentation import watershed

PART_ORDER = ['skull', 'mandible', 'teeth', 'spine_cervical', 'spine_thoracic', 'spine_lumbar',
              'ribs_l', 'ribs_r', 'sternum', 'scapula_l', 'scapula_r', 'clavicle_l', 'clavicle_r',
              'pelvis', 'humerus_l', 'humerus_r', 'forearm_l', 'forearm_r', 'hand_l', 'hand_r',
              'femur_l', 'femur_r', 'patella_l', 'patella_r', 'lower_leg_l', 'lower_leg_r',
              'foot_l', 'foot_r']

BUDGET = {
    'skull': 2400, 'mandible': 240, 'teeth': 420, 'spine_cervical': 520,
    'spine_thoracic': 1100, 'spine_lumbar': 620, 'ribs_l': 700, 'ribs_r': 700,
    'sternum': 160, 'scapula_l': 340, 'scapula_r': 340, 'clavicle_l': 90,
    'clavicle_r': 90, 'pelvis': 900, 'humerus_l': 150, 'humerus_r': 150,
    'forearm_l': 180, 'forearm_r': 180, 'hand_l': 760, 'hand_r': 760,
    'femur_l': 200, 'femur_r': 200, 'patella_l': 60, 'patella_r': 60,
    'lower_leg_l': 220, 'lower_leg_r': 220, 'foot_l': 760, 'foot_r': 760,
}


def load_atlas(path):
    """Per-part vertices of a male atlas skeleton, already in metres."""
    parts, current = {}, None
    for line in open(path):
        if line.startswith('o '):
            current = line[2:].strip()
            parts.setdefault(current, [])
        elif line.startswith('v ') and current is not None:
            parts[current].append([float(x) for x in line.split()[1:4]])
    return {k: np.array(v) for k, v in parts.items() if v}


def simplify_piece(points, faces, target):
    """Quadric edge collapse down to `target` triangles, geometry left where it was.

    Small bones -- phalanges, carpals, teeth -- are only a few hundred voxels, so
    a coarse grid would turn each one into a tetrahedron. Collapsing edges keeps
    the shape at any budget.
    """
    import fast_simplification
    if len(faces) <= target:
        return points, faces
    reduction = 1.0 - target / len(faces)
    out_points, out_faces = fast_simplification.simplify(
        points.astype(np.float32), faces.astype(np.int32),
        target_reduction=float(np.clip(reduction, 0.0, 0.98)), agg=7)
    return out_points.astype(np.float64), out_faces


def decimate_group(group, budget, floor=44):
    """Decimate each bone in a part towards its share of the part's budget."""
    total = sum(len(p['faces']) for p in group)
    out_points, out_faces, offset = [], [], 0
    for piece in group:
        share = max(floor, int(round(budget * len(piece['faces']) / total)))
        points, faces = simplify_piece(piece['points'], piece['faces'], share)
        out_points.append(points)
        out_faces.append(faces + offset)
        offset += len(points)
    return np.concatenate(out_points), np.concatenate(out_faces)


def read_component(table, gid, row):
    """Gather one component's voxels into a volume cropped to its own bounding box."""
    lo = np.array(row['lo'])
    hi = np.array(row['hi'])
    volume = np.zeros(tuple(hi - lo + 1), dtype=bool)
    for start, _stop, path in table['slabs']:
        slab = np.load(path, mmap_mode='r')
        first = max(int(lo[0]), int(start))
        last = min(int(hi[0]), int(start) + slab.shape[0] - 1)
        if last >= first:
            hit = np.array(slab[first - start:last - start + 1,
                                 lo[1]:hi[1] + 1, lo[2]:hi[2] + 1]) == gid
            volume[first - lo[0]:last - lo[0] + 1] |= hit
            del hit
        del slab
    return volume, lo


def segment_distance(points, seg_a, seg_b):
    """Distance from each point to each axis segment, points by segments."""
    direction = seg_b - seg_a
    length2 = np.maximum((direction * direction).sum(1), 1e-9)
    rel = points[:, None, :] - seg_a[None, :, :]
    t = np.clip((rel * direction[None]).sum(2) / length2[None], 0.0, 1.0)
    closest = seg_a[None] + t[:, :, None] * direction[None]
    return np.linalg.norm(points[:, None, :] - closest, axis=2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--table', required=True)
    parser.add_argument('--atlas', required=True)
    parser.add_argument('--obj', required=True)
    parser.add_argument('--parts', required=True)
    parser.add_argument('--min-voxels', type=int, default=60)
    parser.add_argument('--triangles', type=int, default=40000)
    parser.add_argument('--split-above', type=int, default=20000,
                        help='components this large are cut per voxel, not per piece')
    args = parser.parse_args()

    table = json.load(open(args.table))
    spacing = np.array(table['spacing'], dtype=float)
    components = {int(k): v for k, v in table['components'].items()}
    print(f'{len(components)} components, spacing {np.round(spacing, 4)} mm', flush=True)

    # ------------------------------------------------------------- naming frame
    # The atlas only says *where bones are*, never what they look like. It is
    # fitted on the two things a cadaver scan measures reliably: the soles and
    # the stature. Her arms lie flexed across her abdomen, so nothing about the
    # fit is allowed to depend on the arm pose.
    atlas = load_atlas(args.atlas)
    atlas_mm = {k: np.stack([v[:, 1], -v[:, 2], v[:, 0]], 1) * 1000.0 for k, v in atlas.items()}
    all_atlas = np.concatenate(list(atlas_mm.values()))
    subject_top = (max(c['hi'][0] for c in components.values()) + 1) * spacing[0]
    subject_bottom = min(c['lo'][0] for c in components.values()) * spacing[0]
    atlas_span = all_atlas[:, 0].max() - all_atlas[:, 0].min()
    scale = float(np.clip((subject_top - subject_bottom) / atlas_span, 0.82, 1.18))
    trunk = np.concatenate([v for k, v in atlas_mm.items()
                            if k.startswith(('spine', 'pelvis', 'skull', 'ribs', 'sternum',
                                             'femur', 'lower_leg', 'scapula'))]) * scale
    body = [c for c in components.values() if c['voxels'] > 2000]
    body_pts = np.array([c['centroid'] for c in body]) * spacing
    weights = np.array([c['voxels'] for c in body], dtype=float)
    shift = np.zeros(3)
    shift[0] = subject_bottom - all_atlas[:, 0].min() * scale
    shift[1] = np.average(body_pts[:, 1], weights=weights) - trunk[:, 1].mean()
    shift[2] = np.average(body_pts[:, 2], weights=weights) - trunk[:, 2].mean()
    atlas_verts = {k: v * scale + shift for k, v in atlas_mm.items()}
    print(f'atlas scale {scale:.4f}, shift {np.round(shift, 1)} mm', flush=True)

    # One axis per part, from the atlas: the cut between two bones falls where
    # their axes meet, which is the joint.
    names = [p for p in PART_ORDER if p in atlas_verts]
    seg_a, seg_b = [], []
    for name in names:
        v = atlas_verts[name]
        centre = v.mean(0)
        _, _, vt = np.linalg.svd(v - centre, full_matrices=False)
        axis = vt[0]
        t = (v - centre) @ axis
        seg_a.append(centre + axis * t.min())
        seg_b.append(centre + axis * t.max())
    seg_a = np.array(seg_a)
    seg_b = np.array(seg_b)

    # ------------------------------------------------------------------ pieces
    structure = ndi.generate_binary_structure(3, 1)
    pieces = []
    for gid, row in sorted(components.items(), key=lambda kv: -kv[1]['voxels']):
        if row['voxels'] < args.min_voxels:
            continue
        volume, lo = read_component(table, gid, row)
        if not volume.any():
            continue
        voxels = np.argwhere(volume)
        points = (voxels + lo) * spacing
        if len(voxels) >= args.split_above:
            best = np.empty(len(points), dtype=np.int32)
            for start in range(0, len(points), 60000):
                chunk = points[start:start + 60000]
                best[start:start + 60000] = np.argmin(
                    segment_distance(chunk, seg_a, seg_b), axis=1)
            owner = best
        else:
            owner = np.zeros(len(points), dtype=np.int32)
            d = segment_distance(points.mean(0)[None], seg_a, seg_b)[0]
            owner[:] = int(np.argmin(d))
        for slot, name in enumerate(names):
            sel = owner == slot
            if sel.sum() < args.min_voxels:
                continue
            marks = np.zeros(volume.shape, dtype=bool)
            marks[voxels[sel, 0], voxels[sel, 1], voxels[sel, 2]] = True
            labels, count = ndi.label(marks, structure)
            for index in range(1, count + 1):
                mask = labels == index
                n = int(mask.sum())
                if n < args.min_voxels or min(mask.shape) < 2:
                    continue
                vertices, triangles, _, _ = measure.marching_cubes(
                    mask.astype(np.float32), 0.5, spacing=spacing)
                vertices = vertices + lo * spacing
                xyz = np.stack([vertices[:, 2], vertices[:, 0], -vertices[:, 1]], 1) / 1000.0
                pieces.append({'gid': gid, 'part': name, 'points': xyz,
                               'faces': triangles, 'voxels': n})
        print(f'  component {gid} ({row["voxels"]}v)', flush=True)
        del volume, voxels, points

    # ------------------------------------------------------- group, decimate, write
    groups = {}
    for piece in pieces:
        groups.setdefault(piece['part'], []).append(piece)
    missing = [p for p in PART_ORDER if p not in groups]
    if missing:
        print(f'WARNING: no geometry for {missing}', flush=True)

    low = np.array([min(p['points'][:, i].min() for p in pieces) for i in range(3)])
    high = np.array([max(p['points'][:, i].max() for p in pieces) for i in range(3)])
    fall = np.array([-(low[0] + high[0]) / 2, -low[1], 0.0])

    total_faces = sum(len(p['faces']) for p in pieces)
    lines = ['# Human skeleton segmented from a cadaver CT, one object per bone group.',
             '# Source: Visible Human Project (NLM), CC BY 4.0.',
             '# Units: metres, soles on y = 0, facing +z.',
             '# See tools/skeleton-source/ATTRIBUTION.md.']
    report, running, verts_total, faces_total = [], 1, 0, 0
    for part in PART_ORDER:
        if part not in groups:
            continue
        group = groups[part]
        own = sum(len(p['faces']) for p in group)
        budget = max(160, int(round(args.triangles * own / total_faces)))
        points_out, faces = decimate_group(group, budget)
        points_out = points_out + fall
        lines.append(f'o {part}')
        for v in points_out:
            lines.append(f'v {v[0]:.5f} {v[1]:.5f} {v[2]:.5f}')
        for f in faces:
            lines.append(f'f {running + int(f[0])} {running + int(f[1])} {running + int(f[2])}')
        running += len(points_out)
        verts_total += len(points_out)
        faces_total += len(faces)
        report.append({'name': part, 'vertices': int(len(points_out)),
                       'triangles': int(len(faces)), 'pieces': len(group),
                       'bbox': {'low': (points_out.min(0)).tolist(),
                                'high': (points_out.max(0)).tolist()}})

    open(args.obj, 'w').write('\n'.join(lines) + '\n')
    json.dump({'stature': float(high[1] - low[1]), 'parts': report,
               'pieces': len(pieces), 'atlas_scale': scale,
               'bbox': {'low': (low + fall).tolist(), 'high': (high + fall).tolist()}},
              open(args.parts, 'w'), indent=1)
    print(f'{len(report)} parts, {len(pieces)} pieces, {verts_total} vertices, '
          f'{faces_total} triangles, stature {high[1] - low[1]:.4f} m', flush=True)


if __name__ == '__main__':
    main()
