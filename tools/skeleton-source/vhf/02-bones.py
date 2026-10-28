"""Turn the labelled slabs into one mesh per bone, grouped into the rig's parts.

Input is what 01-segments.py produced: label slabs on disk plus a component
table. The atlas is only used to *name* the axial skeleton and to decide which
bone axis a voxel belongs to; no vertex, proportion or surface of the result
comes from it.

Cadaver scans do not arrive as tidy bones. The Visible Human Female was scanned
with both arms folded and both legs together, so at 1600 HU a whole arm and a
whole thigh-plus-shin are each one connected mass: cartilage is one millimetre
thick and partial volume bridges it. Two facts about bone undo that:

  * cortical bone reaches 2,000-3,000 HU, the bridge across a joint does not, so
    a 2400 HU threshold leaves one dense island per bone;
  * two bones that share a joint are further apart along the bone than they are
    across it, so the border that halves the geodesic distance between two dense
    islands falls in the middle of the bridge, which is where the joint is.

Then the appendicular skeleton is named by height band and side (see
anatomy.py), because a folded arm has no usable relationship to an atlas pose,
and the axial skeleton is named against the atlas.

Usage:
  python3 02-bones.py --table labF.json --atlas ../skeleton-male.obj \
      --raw vol-full.npy --obj skeleton-female.obj --parts skeleton-female.parts.json
"""
import argparse
import json
import sys

import numpy as np
from scipy import ndimage as ndi
from skimage import measure
from skimage.graph import MCP_Geometric

sys.path.insert(0, __file__.rsplit('/', 1)[0])
import anatomy

PART_ORDER = ['skull', 'mandible', 'teeth', 'spine_cervical', 'spine_thoracic', 'spine_lumbar',
              'ribs_l', 'ribs_r', 'sternum', 'scapula_l', 'scapula_r', 'clavicle_l', 'clavicle_r',
              'pelvis', 'humerus_l', 'humerus_r', 'forearm_l', 'forearm_r', 'hand_l', 'hand_r',
              'femur_l', 'femur_r', 'patella_l', 'patella_r', 'lower_leg_l', 'lower_leg_r',
              'foot_l', 'foot_r']


def load_atlas(path):
    """Per-part vertices of an atlas skeleton, already in metres."""
    parts, current = {}, None
    for line in open(path):
        if line.startswith('o '):
            current = line[2:].strip()
            parts.setdefault(current, [])
        elif line.startswith('v ') and current is not None:
            parts[current].append([float(x) for x in line.split()[1:4]])
    return {k: np.array(v) for k, v in parts.items() if v}


def simplify_piece(points, faces, target):
    """Quadric edge collapse down to `target` triangles, shape left where it was.

    A phalanx is only a few hundred voxels, so a coarse grid would turn it into a
    tetrahedron; collapsing edges keeps small bones recognisable at any budget.
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
    """Gather one component's voxels into a volume cropped to its bounding box."""
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


def dense_regions(volume, dense_hu, spacing, structure, keep_ratio=0.03, seed=150):
    """Cut one bone mass at its joints: dense islands, then geodesic distance."""
    raw = dense_hu if dense_hu is not None else volume.astype(np.float32)
    dense, _count = ndi.label((raw > 2400) & volume, structure=structure)
    sizes = np.bincount(dense.ravel())
    sizes[0] = 0
    keep = np.flatnonzero(sizes >= seed)
    if len(keep) < 2:
        return None
    costs = np.where(volume, 1.0, np.inf)
    fields = []
    for index in keep:
        finder = MCP_Geometric(costs, sampling=tuple(float(x) for x in spacing),
                               fully_connected=True)
        fields.append(finder.find_costs(starts=np.argwhere(dense == index))[0])
    owner = np.argmin(np.stack(fields), axis=0).astype(np.int16) + 1
    owner[~volume] = 0
    counts = np.bincount(owner.ravel())
    floor = max(500, int(keep_ratio * volume.sum()))
    regions = [(int(k), int(counts[k])) for k in range(1, len(counts))
               if counts[k] >= floor]
    return [(owner == k) for k, _ in regions], region_centre(owner, regions)


def region_centre(owner, regions):
    out = []
    for k, _ in regions:
        out.append(np.argwhere(owner == k).mean(0))
    return out


def parallel_pair(regions):
    """Which two of three bone masses run alongside each other.

    The radius and ulna lie together for their whole length, so their principal
    axes are close and nearly parallel whatever the arm is doing; the humerus
    meets them end to end at the elbow and diverges.
    """
    axes, centres = [], []
    for region in regions:
        points = np.argwhere(region).astype(float)
        centre = points.mean(0)
        _, _, vt = np.linalg.svd(points - centre, full_matrices=False)
        axes.append(vt[0] / np.linalg.norm(vt[0]))
        centres.append(centre)
    best = None
    for i, j in ((0, 1), (0, 2), (1, 2)):
        score = abs(float(axes[i] @ axes[j])) - float(np.linalg.norm(centres[i] - centres[j])) / 400.0
        if best is None or score > best[0]:
            best = (score, (i, j))
    return best[1]


def mesh_of(mask, lo, spacing):
    """Marching cubes on one mask, in its own bounding box, into game axes."""
    vertices, triangles, _, _ = measure.marching_cubes(mask.astype(np.float32), 0.5,
                                                       spacing=spacing)
    vertices = vertices + lo * spacing
    return np.stack([vertices[:, 2], vertices[:, 0], -vertices[:, 1]], 1) / 1000.0, triangles


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--table', required=True)
    parser.add_argument('--atlas', required=True)
    parser.add_argument('--raw', required=True, help='full-resolution HU volume, memory-mapped')
    parser.add_argument('--obj', required=True)
    parser.add_argument('--parts', required=True)
    parser.add_argument('--min-voxels', type=int, default=60)
    parser.add_argument('--triangles', type=int, default=60000)
    parser.add_argument('--fused-above', type=int, default=150000)
    args = parser.parse_args()

    table = json.load(open(args.table))
    spacing = np.array(table['spacing'], dtype=float)
    components = {int(k): v for k, v in table['components'].items()}
    raw = np.load(args.raw, mmap_mode='r')
    print(f'{len(components)} components, spacing {np.round(spacing, 4)} mm', flush=True)

    marks = anatomy.landmarks(components, spacing)
    print('landmarks ' + ', '.join(f'{k} {v:.0f} mm' for k, v in marks.items()), flush=True)

    # ------------------------------------------------------------- naming frame
    atlas = load_atlas(args.atlas)
    atlas_mm = {k: np.stack([v[:, 1], -v[:, 2], v[:, 0]], 1) * 1000.0 for k, v in atlas.items()}
    all_atlas = np.concatenate(list(atlas_mm.values()))
    bottom = min(c['lo'][0] for c in components.values()) * spacing[0]
    top = (max(c['hi'][0] for c in components.values()) + 1) * spacing[0]
    scale = float(np.clip((top - bottom) / (all_atlas[:, 0].max() - all_atlas[:, 0].min()),
                          0.82, 1.18))
    trunk = np.concatenate([v for k, v in atlas_mm.items()
                            if k.startswith(('spine', 'pelvis', 'skull', 'ribs', 'sternum',
                                             'scapula'))]) * scale
    body = [c for c in components.values() if c['voxels'] > 2000]
    body_pts = np.array([c['centroid'] for c in body]) * spacing
    weights = np.array([c['voxels'] for c in body], dtype=float)
    shift = np.zeros(3)
    shift[0] = bottom - all_atlas[:, 0].min() * scale
    shift[1] = np.average(body_pts[:, 1], weights=weights) - trunk[:, 1].mean()
    shift[2] = np.average(body_pts[:, 2], weights=weights) - trunk[:, 2].mean()
    atlas_verts = {k: v * scale + shift for k, v in atlas_mm.items()}
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
    seg_a, seg_b = np.array(seg_a), np.array(seg_b)
    centre_atlas = {k: v.mean(0) for k, v in atlas_verts.items()}
    print(f'atlas scale {scale:.4f}, shift {np.round(shift, 1)} mm', flush=True)

    structure = ndi.generate_binary_structure(3, 1)
    pieces, forearm_tips = [], []

    def add(part, mask, lo, count_voxels):
        if count_voxels < args.min_voxels or min(mask.shape) < 2:
            return
        points, faces = mesh_of(mask, lo, spacing)
        pieces.append({'part': part, 'points': points, 'faces': faces, 'voxels': count_voxels})

    # ------------------------------------------- fused limb masses, cut at joints
    order = sorted(components.items(), key=lambda kv: -kv[1]['voxels'])
    for gid, row in order:
        extent = (np.array(row['hi']) - np.array(row['lo']) + 1) * spacing
        centre = np.array(row['centroid']) * spacing
        if row['voxels'] < args.fused_above or extent[0] < 250:
            continue
        volume, lo = read_component(table, gid, row)
        dense_hu = np.array(raw[int(lo[0]):int(row['hi'][0]) + 1, lo[1]:row['hi'][1] + 1,
                                lo[2]:row['hi'][2] + 1]).astype(np.float32)
        split = dense_regions(volume, dense_hu, spacing, structure)
        del dense_hu
        is_arm = centre[0] > marks['hip'] + 150
        if split is None:
            part = anatomy.limb_part(centre, extent, row['voxels'], marks, forearm_tips)
            add(part or 'pelvis', volume, lo, int(volume.sum()))
            del volume
            continue
        regions, centres = split
        print(f'  mass {gid}: {row["voxels"]}v cut into {len(regions)} at z{centre[0]:.0f}',
              flush=True)
        if is_arm and len(regions) >= 3:
            big = sorted(range(len(regions)), key=lambda k: -int(regions[k].sum()))[:3]
            pair = parallel_pair([regions[k] for k in big])
            pair = {big[pair[0]], big[pair[1]]}
            side = anatomy.side_of((sum(centres[k][2] for k in big) / 3 + lo[2]) * spacing[2])
            for k in big:
                part = f'humerus_{side}' if k not in pair else f'forearm_{side}'
                mask = regions[k]
                add(part, mask, lo, int(mask.sum()))
                if part.startswith('forearm'):
                    voxels = np.argwhere(mask)
                    tip = (voxels[np.argmax(voxels[:, 0])] + lo) * spacing
                    forearm_tips.append((tip, side))
            del volume, regions
            continue
        for k, mask in enumerate(regions):
            piece_centre = (np.argwhere(mask).mean(0) + lo) * spacing
            piece_extent = (np.argwhere(mask).max(0) - np.argwhere(mask).min(0) + 1) * spacing
            part = anatomy.limb_part(piece_centre, piece_extent, int(mask.sum()), marks,
                                     forearm_tips)
            if part is None:
                part = names[int(np.argmin(np.linalg.norm(
                    np.array([centre_atlas[n] for n in names]) - piece_centre, axis=1)))]
            add(part, mask, lo, int(mask.sum()))
        del volume, regions

    # ------------------------------------------------- remaining bones, by naming
    for gid, row in order:
        extent = (np.array(row['hi']) - np.array(row['lo']) + 1) * spacing
        if row['voxels'] >= args.fused_above and extent[0] >= 250:
            continue
        if row['voxels'] < args.min_voxels:
            continue
        volume, lo = read_component(table, gid, row)
        if not volume.any():
            continue
        voxels = np.argwhere(volume)
        if len(voxels) >= 20000:
            points = (voxels + lo) * spacing
            owner = np.empty(len(points), dtype=np.int32)
            for start in range(0, len(points), 60000):
                owner[start:start + 60000] = np.argmin(
                    segment_distance(points[start:start + 60000], seg_a, seg_b), axis=1)
            for slot, name in enumerate(names):
                sel = owner == slot
                if sel.sum() < args.min_voxels:
                    continue
                marks_mask = np.zeros(volume.shape, dtype=bool)
                marks_mask[voxels[sel, 0], voxels[sel, 1], voxels[sel, 2]] = True
                labels, count = ndi.label(marks_mask, structure)
                for index in range(1, count + 1):
                    add(name, labels == index, lo, int((labels == index).sum()))
        else:
            piece_centre = (voxels.mean(0) + lo) * spacing
            piece_extent = (voxels.max(0) - voxels.min(0) + 1) * spacing
            part = anatomy.limb_part(piece_centre, piece_extent, len(voxels), marks,
                                     forearm_tips)
            if part is None:
                part = names[int(np.argmin(np.linalg.norm(
                    np.array([centre_atlas[n] for n in names]) - piece_centre, axis=1)))]
            add(part, volume, lo, len(voxels))
        del volume

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
             '# Source: Visible Human Project, U.S. National Library of Medicine, CC BY 4.0.',
             '# Units: metres, soles on y = 0, facing +z.',
             '# See tools/skeleton-source/ATTRIBUTION.md.']
    report, running, verts_total, faces_total = [], 1, 0, 0
    for part in PART_ORDER:
        if part not in groups:
            continue
        group = groups[part]
        own = sum(len(p['faces']) for p in group)
        points_out, faces = decimate_group(group, max(160, int(round(
            args.triangles * own / total_faces))))
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
                       'triangles': int(len(faces)), 'bones': len(group),
                       'bbox': {'low': points_out.min(0).tolist(),
                                'high': points_out.max(0).tolist()}})

    open(args.obj, 'w').write('\n'.join(lines) + '\n')
    json.dump({'stature': float(high[1] - low[1]), 'parts': report,
               'bones': len(pieces), 'atlas_scale': scale, 'landmarks': marks,
               'bbox': {'low': (low + fall).tolist(), 'high': (high + fall).tolist()}},
              open(args.parts, 'w'), indent=1)
    print(f'{len(report)} parts, {len(pieces)} bones, {verts_total} vertices, '
          f'{faces_total} triangles, stature {high[1] - low[1]:.4f} m', flush=True)


if __name__ == '__main__':
    main()
