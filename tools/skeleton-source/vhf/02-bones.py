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

# The atlas is a mirror-ambiguous reference: fitting it to a cadaver by shifted
# centroids can flip left and right, and it has no idea where a folded arm's
# elbow is. So it is only ever allowed to name the axial skeleton, whose parts
# do not move between poses. Every limb name comes from anatomy.limb_part.
AXIAL = [p for p in PART_ORDER if p.startswith(('skull', 'mandible', 'teeth', 'spine', 'ribs',
                                                'sternum', 'scapula', 'clavicle', 'pelvis'))]


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


def ball(spacing, radius_mm):
    """An ellipsoid in voxels that is a sphere in millimetres."""
    radius = np.asarray(radius_mm, dtype=float) / spacing
    grid = np.indices([int(2 * r) + 1 for r in radius]).astype(float)
    grid -= radius.reshape(3, 1, 1, 1)
    return ((grid / radius.reshape(3, 1, 1, 1)) ** 2).sum(0) <= 1.0


def drop_flecks(points, faces, min_diagonal=0.012, rescue_triangles=400, thin_plates=False):
    """Remove the crumbs a thresholded scan leaves around a bone.

    A ragged cortical shell produces small islands a few millimetres across
    sitting off the bone. On screen they read as debris floating around the body,
    and that is a defect the eye finds instantly. Three tests, in order:

    Size. The smallest bone in a body is the sesamoid, about four millimetres;
    the smallest named bone here -- a distal phalanx -- is over twenty. Anything
    under a 12 mm diagonal is crumb, whatever its triangle count, because a
    densely meshed crumb still has thirty faces.

    Flatness, and only where cartilage lives. Calcified costal cartilage survives
    any density threshold and hangs in the intercostal spaces as flat ribbons
    that look exactly like debris. A small bone is a barrel six to ten
    millimetres through; a small ribbon is two millimetres thick. The test is
    confined to the ribs because the patella is a cortex shell too, and dropping
    one side's flakes and not the other's breaks the mirror.

    Isolation is judged later, once every part is assembled, because a piece can
    be alone inside its own group and still be sitting next to its neighbour in
    the next group along.
    """
    if not len(faces):
        return points, faces
    parent = list(range(len(points)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for tri in faces:
        a = find(int(tri[0]))
        parent[find(int(tri[1]))] = a
        parent[find(int(tri[2]))] = a
    roots = np.array([find(i) for i in range(len(points))])
    tri_root = roots[faces[:, 0]]
    root_list = np.unique(tri_root)
    islands = {}
    for root in root_list:
        selected = tri_root == root
        used = np.unique(faces[selected])
        cloud = points[used]
        span = float(np.linalg.norm(cloud.max(0) - cloud.min(0)))
        flat = False
        if len(cloud) >= 4:
            axes = np.linalg.svd(cloud - cloud.mean(0), full_matrices=False)[2]
            local = cloud @ axes.T
            flat = thin_plates and span < 0.030 and \
                float((local.max(0) - local.min(0)).min()) < 0.005
        keep = (span >= min_diagonal and not flat) or int(selected.sum()) >= rescue_triangles
        islands[int(root)] = {'keep': keep, 'cloud': cloud, 'span': span}
    keep_roots = {r for r, v in islands.items() if v['keep']}
    if not keep_roots:
        # A whole part below every threshold is not crumb -- it is a small bone.
        keep_roots = {max(islands, key=lambda r: len(islands[r]['cloud']))}
    keep_tri = np.isin(tri_root, list(keep_roots))
    if keep_tri.all():
        return points, faces
    faces = faces[keep_tri]
    used = np.unique(faces)
    remap = np.full(len(points), -1, dtype=np.int64)
    remap[used] = np.arange(len(used))
    return points[used], remap[faces]


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


def thin_part(part):
    """Where calcified costal cartilage turns up, so the flatness test applies."""
    return part.startswith(('ribs', 'sternum'))


def decimate_group(group, budget, floor=44, part=''):
    """Decimate each bone in a part towards its share of the part's budget."""
    total = sum(len(p['faces']) for p in group)
    out_points, out_faces, offset = [], [], 0
    for piece in group:
        share = max(floor, int(round(budget * len(piece['faces']) / total)))
        points, faces = simplify_piece(piece['points'], piece['faces'], share)
        out_points.append(points)
        out_faces.append(faces + offset)
        offset += len(points)
    points = np.concatenate(out_points)
    faces = np.concatenate(out_faces)
    return drop_flecks(points, faces, thin_plates=thin_part(part))


def count_islands(faces):
    """Number of connected pieces in a face list, which is the bone count."""
    parent = {}

    def find(a):
        parent.setdefault(a, a)
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for tri in faces:
        a = find(int(tri[0]))
        for other in tri[1:]:
            b = find(int(other))
            if a != b:
                parent[b] = a
    return len({find(int(tri[0])) for tri in faces})


def strip_orphans(built, span_limit=0.020, gap=0.012):
    """Drop islands that hang in space, judged against the whole body at once.

    Per-part cleaning cannot see across parts, and a rib tip that lands in the
    hand group, or a sternal fragment, is only recognisable as debris when the
    whole skeleton is in view. The size limit is deliberately low, two
    centimetres, because the one bone that genuinely stands alone is the patella,
    which floats in the quadriceps tendon a centimetre off the femur -- a wider
    net would delete real anatomy to catch the same stray twice.
    """
    from scipy.spatial import cKDTree
    points = np.concatenate([p for _, p, _ in built])
    faces = np.concatenate([f + sum(len(b[1]) for b in built[:i])
                            for i, (_, _, f) in enumerate(built)])
    owner = np.concatenate([[i] * len(p) for i, (_, p, _) in enumerate(built)])
    parent = list(range(len(points)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for tri in faces:
        a = find(int(tri[0]))
        parent[find(int(tri[1]))] = a
        parent[find(int(tri[2]))] = a
    roots = np.array([find(i) for i in range(len(points))])
    tri_root = roots[faces[:, 0]]
    clouds = {int(r): points[np.unique(faces[tri_root == r])] for r in np.unique(tri_root)}
    # The largest island of each part always survives. A thin plate such as the
    # sternum can break into pieces smaller than the span limit, and deleting the
    # whole bone would be a worse error than keeping a fragment.
    island_part = {root: int(owner[np.flatnonzero(roots == root)[0]]) for root in clouds}
    biggest = {}
    for root, cloud in clouds.items():
        span = float(np.linalg.norm(cloud.max(0) - cloud.min(0)))
        part = island_part[root]
        if part not in biggest or span > biggest[part][0]:
            biggest[part] = (span, root)
    protected = {root for _, root in biggest.values()}
    drop = set()
    # The sternum is a plate that ossifies in segments and sits between two
    # cartilages with real gaps; its pieces are anatomy, not debris.
    keep_solid = {b[0] for b in built if b[0] == 'sternum'}
    for root, cloud in clouds.items():
        if root in protected:
            continue
        if any(part == 'sternum' for part in keep_solid) and island_part[root] in {
                i for i, b in enumerate(built) if b[0] in keep_solid}:
            continue
        if float(np.linalg.norm(cloud.max(0) - cloud.min(0))) >= span_limit:
            continue
        others = np.concatenate([c for r, c in clouds.items() if r != root])
        if len(others) and cKDTree(others[::max(1, len(others) // 30000)]).query(cloud)[0].min() > gap:
            drop.add(root)
    if not drop:
        return built
    keep_tri = ~np.isin(tri_root, list(drop))
    print(f'  removed {len(drop)} islands hanging in space', flush=True)
    out, base, offset = [], 0, 0
    for i, (part, p, _) in enumerate(built):
        own = faces[(owner[faces[:, 0]] == i) & keep_tri] - base
        used = np.unique(own) if len(own) else np.zeros(0, int)
        remap = np.full(len(p), -1, np.int64)
        remap[used] = np.arange(len(used))
        out.append((part, p[used], remap[own.astype(int)] if len(own) else own))
        base += len(p)
    return out


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



def axial_name(point, centre_atlas, midline):
    """Nearest atlas part, restricted to the piece's own side.

    The atlas is fitted to a cadaver with a single global shift, so on its own it
    will happily call a left rib a right one. Anatomy does not mirror: a piece on
    the left of the midline can only be a left part. The side constraint is what
    makes the mirror check meaningful.
    """
    side = anatomy.side_of(point[2], midline)
    allowed = [k for k in centre_atlas
               if not (k.endswith('_l') and side == 'r') and not (k.endswith('_r') and side == 'l')]
    distances = np.linalg.norm(np.array([centre_atlas[k] for k in allowed]) - point, axis=1)
    return allowed[int(np.argmin(distances))]


def mesh_of(mask, lo, spacing, part=''):
    """Marching cubes on one mask, in its own bounding box, into game axes."""
    vertices, triangles, _, _ = measure.marching_cubes(mask.astype(np.float32), 0.5,
                                                       spacing=spacing)
    vertices = vertices + lo * spacing
    points = np.stack([vertices[:, 2], vertices[:, 0], -vertices[:, 1]], 1) / 1000.0
    return drop_flecks(points, triangles, thin_plates=thin_part(part))


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
    names = [p for p in AXIAL if p in atlas_verts]
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
        # A 1600 HU mask keeps two bones apart but keeps only the densest part of
        # each one, which shreds thin plates: the pelvis, the scapulae and the
        # sternum are two to four millimetres thick, and at 1600 their surface
        # breaks into shards. So the piece is grown back to its real extent at
        # 1200 HU -- above soft tissue, below cortical peak -- but only inside
        # its own neighbourhood, so the separation decided at 1600 stands.
        # Fingers and toes are small bones in a small space: a phalanx is six
        # millimetres of cortex, so a tight regrowth leaves the digits as loose
        # slats that read as debris. They get a wider reach and a lower floor.
        close_part = part.startswith(('hand', 'foot')) or part == 'sternum'
        reach, floor, radius = (4, 1000, 2.5) if close_part else (2, 1200, 1.5)
        crop = np.asarray(raw[lo[0]:lo[0] + mask.shape[0], lo[1]:lo[1] + mask.shape[1],
                             lo[2]:lo[2] + mask.shape[2]])
        near = ndi.binary_dilation(mask, structure, iterations=reach)
        mask = mask | (near & (crop > floor))
        # A cortical shell is one to two millimetres thick, so a 1600 HU mask of
        # it is full of pinholes and the marching-cubes surface comes out torn.
        # Torn surfaces read as debris on screen. Closing with a 1.5 mm ball
        # makes every bone watertight (zero boundary edges, measured) and adds
        # about four per cent of volume, which is far cheaper than the holes.
        mask = ndi.binary_fill_holes(ndi.binary_closing(
            ndi.binary_opening(mask, structure, iterations=1), ball(spacing, radius)))
        if int(mask.sum()) < args.min_voxels:
            return
        points, faces = mesh_of(mask, lo, spacing, part)
        pieces.append({'part': part, 'points': points, 'faces': faces, 'voxels': count_voxels})

    # ------------------------------------------- fused limb masses, cut at joints
    order = sorted(components.items(), key=lambda kv: -kv[1]['voxels'])
    for gid, row in order:
        extent = (np.array(row['hi']) - np.array(row['lo']) + 1) * spacing
        centre = np.array(row['centroid']) * spacing
        if row['voxels'] < args.fused_above or extent[0] < 250:
            continue
        volume, lo = read_component(table, gid, row)
        if centre[0] > marks['hip'] + 150:
            # An arm. Three bones with two different axes, so they are separated
            # by their own dense cores rather than by a height plane.
            dense_hu = np.array(raw[int(lo[0]):int(row['hi'][0]) + 1, lo[1]:row['hi'][1] + 1,
                                    lo[2]:row['hi'][2] + 1]).astype(np.float32)
            split = dense_regions(volume, dense_hu, spacing, structure)
            del dense_hu
            if split is not None and len(split[0]) >= 2:
                regions, _centres = split
                sizes = [int(r.sum()) for r in regions]
                humerus = int(np.argmax(sizes))
                side = anatomy.side_of((np.argwhere(regions[humerus])[:, 2].mean()
                                        + lo[2]) * spacing[2], marks['midline'])
                shoulder = np.argwhere(regions[humerus]).mean(0)
                forearm = np.zeros(volume.shape, dtype=bool)
                for k, mask in enumerate(regions):
                    if k == humerus:
                        continue
                    forearm |= mask
                add(f'humerus_{side}', regions[humerus], lo, sizes[humerus])
                add(f'forearm_{side}', forearm, lo, int(forearm.sum()))
                # The wrist is the far end of the forearm from the shoulder;
                # her forearms cross her abdomen, so no fixed direction works.
                points = np.argwhere(forearm)
                tip = points[int(np.argmax(np.linalg.norm(points - shoulder, axis=1)))]
                forearm_tips.append(((tip + lo) * spacing, side))
                print(f'  mass {gid}: arm cut into humerus ({sizes[humerus]}v) + forearm '
                      f'({int(forearm.sum())}v)', flush=True)
            else:
                part = anatomy.limb_part(centre, extent, row['voxels'], marks, forearm_tips)
                add(part or 'pelvis', volume, lo, int(volume.sum()))
            del volume
            continue
        if centre[0] < marks['hip'] + 100:
            # A leg. Thigh, shin and foot are stacked, so they are separated by
            # the joint heights measured on this subject. Splitting a leg by
            # dense cores finds the tibia and the fibula -- two bones that run
            # side by side down the whole shin -- and cuts the leg lengthwise.
            side = anatomy.side_of(centre[2], marks['midline'])
            points = np.argwhere(volume)
            height = (points[:, 0] + lo[0]) * spacing[0]
            for part, low, high in ((f'foot_{side}', -1.0, marks['ankle']),
                                    (f'lower_leg_{side}', marks['ankle'], marks['knee']),
                                    (f'femur_{side}', marks['knee'], 1e6)):
                keep = (height >= low) & (height < high)
                if keep.sum() < args.min_voxels:
                    continue
                mask = np.zeros(volume.shape, dtype=bool)
                chosen = points[keep]
                mask[chosen[:, 0], chosen[:, 1], chosen[:, 2]] = True
                add(part, mask, lo, int(mask.sum()))
            print(f'  mass {gid}: leg cut at knee {marks["knee"]:.0f} and ankle '
                  f'{marks["ankle"]:.0f} mm', flush=True)
            del volume
            continue
        # Anything else is trunk: name it against the atlas.
        regions, _centres = dense_regions(volume, None, spacing, structure) or ([volume], None)
        for mask in regions:
            if mask.sum() < args.min_voxels:
                continue
            points = np.argwhere(mask)
            piece_centre = (points.mean(0) + lo) * spacing
            piece_extent = (points.max(0) - points.min(0) + 1) * spacing
            part = anatomy.limb_part(piece_centre, piece_extent, int(mask.sum()), marks,
                                     forearm_tips)
            if part is None:
                part = axial_name(piece_centre, centre_atlas, marks['midline'])
            add(part, mask, lo, int(mask.sum()))
        print(f'  mass {gid}: trunk split into {len(regions)}', flush=True)
        del volume

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
                part = axial_name(piece_centre, centre_atlas, marks['midline'])
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
    # Centre laterally on the spine, not on the bounding box: her arms are folded
    # unevenly, so the box midpoint is not the midline of her body.
    midline = [p['points'][:, 0].mean() for p in pieces
               if p['part'].startswith(('spine', 'pelvis', 'skull', 'sternum'))]
    centre_x = float(np.median(midline)) if midline else (low[0] + high[0]) / 2
    fall = np.array([-centre_x, -low[1], 0.0])
    total_faces = sum(len(p['faces']) for p in pieces)

    lines = ['# Human skeleton segmented from a cadaver CT, one object per bone group.',
             '# Source: Visible Human Project, U.S. National Library of Medicine, CC BY 4.0.',
             '# Units: metres, soles on y = 0, facing +z.',
             '# See tools/skeleton-source/ATTRIBUTION.md.']
    report, running, verts_total, faces_total = [], 1, 0, 0
    built = []
    for part in PART_ORDER:
        if part not in groups:
            continue
        group = groups[part]
        own = sum(len(p['faces']) for p in group)
        points_out, faces = decimate_group(group, max(160, int(round(
            args.triangles * own / total_faces))), part=part)
        built.append((part, points_out + fall, faces))
    built = strip_orphans(built)
    built = [b for b in built if len(b[2])]
    for part, points_out, faces in built:
        lines.append(f'o {part}')
        for v in points_out:
            lines.append(f'v {v[0]:.5f} {v[1]:.5f} {v[2]:.5f}')
        for f in faces:
            lines.append(f'f {running + int(f[0])} {running + int(f[1])} {running + int(f[2])}')
        running += len(points_out)
        verts_total += len(points_out)
        faces_total += len(faces)
        report.append({'name': part, 'vertices': int(len(points_out)),
                       'triangles': int(len(faces)), 'bones': count_islands(faces),
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
