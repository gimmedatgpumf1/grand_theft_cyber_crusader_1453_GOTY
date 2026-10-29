"""Generate labelled centreline and anatomical-link data for the C room.

Connections are an explicit anatomical schema, never discovered by proximity.
OpenSkeleton supplies surfaces, not joint landmarks. Centrelines and the visible
connection endpoints are therefore geometric approximations, not clinical joint
centres. The graph is an inspection aid, not a diagnostic anatomical model.
"""
from pathlib import Path
import json
import math
import numpy as np
from scipy.spatial import Delaunay

BASE = Path(__file__).resolve().parent.parent
VERTEBRAE = {f'{region}{i}' for region, count in (('C', 7), ('T', 12), ('L', 5))
             for i in range(1, count + 1)}


def anatomy():
    edges = []
    def add(a, b, kind='joint'):
        assert a != b
        edges.append((a, b, kind))

    # Atlas is connected to the head, axis, remaining vertebrae and sacrum.
    add('Head', 'Jaw'); add('Head', 'C1')
    for spine, count in (('C', 7), ('T', 12), ('L', 5)):
        for i in range(1, count): add(f'{spine}{i}', f'{spine}{i+1}')
    add('C7', 'T1'); add('T12', 'L1'); add('L5', 'Sacrum')
    for side in ('L', 'R'):
        def pair(a, b, kind='joint'): add(f'{a}_{side}', f'{b}_{side}', kind)
        add('Sacrum', f'Pelvis_{side}')
        pair('Pelvis', 'Femur'); pair('Femur', 'Patella'); pair('Femur', 'Tibia')
        pair('Tibia', 'Fibula'); pair('Tibia', 'Tal'); pair('Fibula', 'Tal')
        pair('Tal', 'Cal'); pair('Tal', 'Nav'); pair('Cal', 'Cub')
        for c in ('MedCun', 'InterCun', 'LatCun'): pair('Nav', c)
        for c, digit in (('MedCun', 1), ('InterCun', 2), ('LatCun', 3), ('Cub', 4), ('Cub', 5)):
            pair(c, f'FootMeta{digit}')
        for digit in range(1, 6):
            pair(f'FootMeta{digit}', f'FootProxP{digit}')
            if digit == 1: pair(f'FootProxP{digit}', f'FootDistP{digit}')
            else:
                pair(f'FootProxP{digit}', f'FootMidP{digit}')
                pair(f'FootMidP{digit}', f'FootDistP{digit}')
        pair('FootMeta1', 'FootSesaMed'); pair('FootMeta1', 'FootSesaLat')
        # The shoulder girdle connects via the clavicle, not by a fictitious
        # direct joint between scapula and ribs.
        add('Sternum', f'Clavicula_{side}'); pair('Clavicula', 'Scapula')
        pair('Scapula', 'Humerus'); pair('Humerus', 'Radius'); pair('Humerus', 'Ulna')
        pair('Radius', 'Ulna'); pair('Radius', 'HandScaphoid'); pair('Radius', 'HandLuna')
        # Carpal articulations and the thumb-to-little-finger paths.
        for a, b in [('HandScaphoid','HandLuna'), ('HandLuna','HandTriquetrum'),
                     ('HandTriquetrum','HandPisiform'), ('HandScaphoid','HandTrapezium'),
                     ('HandScaphoid','HandTrapezoideum'), ('HandLuna','HandCapitatum'),
                     ('HandLuna','HandHamata'), ('HandTriquetrum','HandHamata'),
                     ('HandTrapezium','HandMeta1'), ('HandTrapezoideum','HandMeta2'),
                     ('HandCapitatum','HandMeta3'), ('HandHamata','HandMeta4'),
                     ('HandHamata','HandMeta5')]: pair(a, b)
        for digit in range(1, 6):
            pair(f'HandMeta{digit}', f'HandProxP{digit}')
            if digit == 1: pair(f'HandProxP{digit}', f'HandDistP{digit}')
            else:
                pair(f'HandProxP{digit}', f'HandMidP{digit}')
                pair(f'HandMidP{digit}', f'HandDistP{digit}')
        # Posterior rib-vertebra articulations. The anterior cartilage is not
        # in this dataset: show it as an inferred bridge, not a bone or joint.
        for i in range(1, 13): add(f'T{i}', f'Rib{i}_{side}')
        for i in range(1, 8): add('Sternum', f'Rib{i}_{side}', 'cartilage')
        for i in range(8, 11): pair(f'Rib{i}', f'Rib{i-1}', 'cartilage')
    add('Pelvis_L', 'Pelvis_R')
    return edges


def unified_groups():
    groups = {}
    for side in ('L', 'R'):
        # Keep the thumb's trapezium and first metacarpal separate: their
        # carpometacarpal joint is the mobile base of the thumb.
        hand = ('HandScaphoid', 'HandLuna', 'HandTriquetrum', 'HandPisiform',
                'HandTrapezoideum', 'HandCapitatum', 'HandHamata')
        hand += tuple(f'HandMeta{i}' for i in range(2, 6))
        foot = ('Tal', 'Cal', 'Nav', 'Cub', 'MedCun', 'InterCun', 'LatCun',
                'FootSesaMed', 'FootSesaLat') + tuple(f'FootMeta{i}' for i in range(1, 6))
        groups[f'HandPalm_{side}'] = tuple(f'{name}_{side}' for name in hand)
        groups[f'FootBase_{side}'] = tuple(f'{name}_{side}' for name in foot)
    return groups


def unified_surface(bones, members):
    """A filled 3-D surface spanning the actual bone axes, not bone outlines.

    Carpals anchor the base; each metacarpal supplies its two ends and centre. Triangulation fills *between* those anchors. The
    3-D coordinates are retained: projection chooses connectivity only.
    """
    points, owners = [], {}
    for name in members:
        vertices = bones[name]
        centre = vertices.mean(axis=0)
        anchors = [centre]
        if name.startswith('HandMeta'):
            _, _, basis = np.linalg.svd(vertices - centre, full_matrices=False)
            axis = basis[0]
            along = (vertices-centre) @ axis
            lo, hi = np.quantile(along, [0.05, 0.95])
            anchors = [centre + axis*lo, centre, centre + axis*hi]
        owners[name] = list(range(len(points), len(points)+len(anchors)))
        points.extend(anchors)
    points = np.array(points)
    centre = points.mean(axis=0)
    _, _, basis = np.linalg.svd(points-centre, full_matrices=False)
    projected = (points-centre) @ basis[:2].T
    mesh = Delaunay(projected)
    triangles = []
    edge_counts = {}
    for raw in mesh.simplices:
        a, b, c = map(int, raw)
        # Keep a consistent winding in the triangulation plane.
        u, v = projected[b]-projected[a], projected[c]-projected[a]
        if u[0]*v[1] - u[1]*v[0] < 0: b, c = c, b
        triangles.append((a, b, c))
        for p, q in ((a,b),(b,c),(c,a)):
            key = tuple(sorted((p,q)))
            if key not in edge_counts: edge_counts[key] = [0, (p,q)]
            edge_counts[key][0] += 1
    boundary = [edge for count, edge in edge_counts.values() if count == 1]
    assert set(int(i) for face in triangles for i in face) == set(range(len(points)))
    return points, triangles, boundary, owners


def foot_hexahedron(bones, members, side):
    """Fit a tapered, six-faced shoe shape around the source foot bones.

    The long axis follows heel to metatarsals; the height axis comes from heel
    to ankle. Rear dimensions derive from calcaneus and talus, forefoot dimensions
    from the five metatarsals. Shift the sloping sides only as far as needed to
    contain all included source vertices, preserving the measured taper.
    """
    heel = bones[f'Cal_{side}'].mean(axis=0)
    ankle = bones[f'Tal_{side}'].mean(axis=0)
    metatarsals = [bones[f'FootMeta{i}_{side}'] for i in range(1, 6)]
    forefoot = np.mean([bone.mean(axis=0) for bone in metatarsals], axis=0)
    length = forefoot - heel
    length /= np.linalg.norm(length)
    height = ankle - heel
    height -= np.dot(height, length) * length
    assert np.linalg.norm(height) > 1e-8
    height /= np.linalg.norm(height)
    width = np.cross(height, length)
    width /= np.linalg.norm(width)
    axes = np.array([length, width, height])
    local = np.concatenate([bones[name] for name in members]) @ axes.T
    rear = np.concatenate([bones[f'Cal_{side}'], bones[f'Tal_{side}']]) @ axes.T
    front = np.concatenate(metatarsals) @ axes.T
    x0, x1 = local[:, 0].min(), local[:, 0].max()
    t = (local[:, 0] - x0) / (x1 - x0)
    low, high = np.zeros((2, 2)), np.zeros((2, 2))
    for axis in (1, 2):
        # Individual heel/forefoot dimensions choose the taper. The global
        # extrema of residuals enclose every included bone at its real position.
        low_delta = front[:, axis].min() - rear[:, axis].min()
        high_delta = front[:, axis].max() - rear[:, axis].max()
        low[0, axis-1] = np.min(local[:, axis] - t * low_delta)
        high[0, axis-1] = np.max(local[:, axis] - t * high_delta)
        low[1, axis-1] = low[0, axis-1] + low_delta
        high[1, axis-1] = high[0, axis-1] + high_delta
    # Binary indices are length (rear/front), width and height. The six quads
    # are planar trapezoids, not an equal-sided cuboid.
    points = np.array([np.array([x1 if i & 4 else x0,
                                 high[int(bool(i & 4)), 0] if i & 2 else low[int(bool(i & 4)), 0],
                                 high[int(bool(i & 4)), 1] if i & 1 else low[int(bool(i & 4)), 1]]) @ axes
                       for i in range(8)])
    return points, axes


def box_to_line(box, source, line):
    """Attach an existing anatomical link to the fitted hexahedron surface."""
    points, _ = box
    quads = ((0,1,3,2), (4,6,7,5), (0,4,5,1), (2,3,7,6), (0,2,6,4), (1,5,7,3))
    centre = points.mean(axis=0)
    planes = []
    for face in quads:
        p = points[face[0]]
        normal = np.cross(points[face[1]] - p, points[face[2]] - p)
        normal /= np.linalg.norm(normal)
        if np.dot(normal, centre - p) > 0: normal = -normal
        planes.append((p, normal))
    def inside(p): return all(np.dot(normal, p - origin) <= 1e-8 for origin, normal in planes)
    choices = [closest_to_point(point, line) for point in source if inside(point)]
    assert choices, 'source link has no anchor inside its fitted foot'
    _, start, end = min(choices, key=lambda value: value[0])
    direction = end - start
    if np.linalg.norm(direction) < 1e-10: direction = start - centre
    if np.linalg.norm(direction) < 1e-10: direction = points[4] - points[0]
    times = [-np.dot(normal, start-origin) / np.dot(normal, direction)
             for origin, normal in planes if np.dot(normal, direction) > 1e-12]
    assert times
    on_surface = start + max(0, min(times)) * direction
    return closest_to_point(on_surface, line)


def read_bones(path):
    result = {}; part = None
    for line in path.open():
        if line.startswith('g '):
            part = line[2:].strip(); assert part not in result
            result[part] = []
        elif line.startswith('v '):
            assert part is not None
            result[part].append([float(v) for v in line.split()[1:4]])
    return {k: np.array(v) for k, v in result.items()}


def line_for(name, vertices, bones):
    centre = vertices.mean(axis=0)
    if name.startswith('Rib'):
        number = int(name.split('_')[0][3:])
        axis = (bones[f'T{number}'].mean(axis=0) + bones['Sternum'].mean(axis=0)) / 2
        delta = vertices[:, [0, 2]] - axis[[0, 2]]
        angle = np.arctan2(delta[:, 1], delta[:, 0])
        mean = math.atan2(np.sin(angle).mean(), np.cos(angle).mean())
        angle = np.arctan2(np.sin(angle - mean), np.cos(angle - mean))
        bins = np.quantile(angle, np.linspace(0, 1, 9))
        samples = []
        for i in range(8):
            mask = (angle >= bins[i]) & (angle <= bins[i+1] if i == 7 else angle < bins[i+1])
            if mask.any(): samples.append(vertices[mask].mean(axis=0))
        return samples if len(samples) >= 2 else [centre, centre + [0, 0.001, 0]]
    # Principal axes are shape-derived; they do not decide which bones connect.
    _, _, basis = np.linalg.svd(vertices - centre, full_matrices=False)
    along = (vertices - centre) @ basis[0]
    lo, hi = np.quantile(along, [0.05, 0.95])
    if name == 'Head':
        # The supplied head is one fused structure. Two closed ellipses make
        # its outline legible from the front and side rather than a single stick.
        projections = (vertices - centre) @ basis.T
        bounds = np.quantile(projections, [0.05, 0.95], axis=0)
        mid = centre + ((bounds[0] + bounds[1]) / 2) @ basis
        radii = (bounds[1] - bounds[0]) / 2
        outline = []
        for plane in (1, 2):
            for i in range(17):
                if plane == 2 and i == 0: continue
                angle = 2 * math.pi * i / 16
                outline.append(mid + basis[0] * (radii[0] * math.cos(angle)) +
                               basis[plane] * (radii[plane] * math.sin(angle)))
        return outline
    points = [centre + basis[0] * lo, centre, centre + basis[0] * hi]
    # Broader structures need branches rather than one misleading stick.
    if name in ('Jaw', 'Sacrum', 'Sternum') or name.startswith(('Pelvis_', 'Scapula_')):
        across = (vertices - centre) @ basis[1]
        a, b = np.quantile(across, [0.12, 0.88])
        return points + [centre, centre + basis[1] * a, centre, centre + basis[1] * b]
    return points


def closest_on_segments(a0, a1, b0, b1):
    """Closest visual points on two *already anatomically linked* polylines."""
    u, v, w = a1 - a0, b1 - b0, a0 - b0
    aa, bb, cc, dd, ee = np.dot(u,u), np.dot(u,v), np.dot(v,v), np.dot(u,w), np.dot(v,w)
    if aa < 1e-18 or cc < 1e-18: return None
    candidates = []
    def take(s, t):
        x, y = a0 + s*u, b0 + t*v
        candidates.append((np.linalg.norm(x-y), x, y))
    for s in (0.0, 1.0): take(s, np.clip((ee + bb*s) / cc, 0, 1))
    for t in (0.0, 1.0): take(np.clip((bb*t - dd) / aa, 0, 1), t)
    denom = aa*cc - bb*bb
    if denom > 1e-18:
        s, t = (bb*ee - cc*dd) / denom, (aa*ee - bb*dd) / denom
        if 0 <= s <= 1 and 0 <= t <= 1: take(s, t)
    return min(candidates, key=lambda value: value[0])


def closest_to_point(point, polyline):
    choices = []
    for a, b in zip(polyline, polyline[1:]):
        delta = b - a
        squared = np.dot(delta, delta)
        if squared < 1e-18: continue
        t = np.clip(np.dot(point - a, delta) / squared, 0, 1)
        other = a + t * delta
        choices.append((np.linalg.norm(point - other), point, other))
    return min(choices, key=lambda value: value[0])


def build(sex):
    bones = read_bones(BASE / f'skeleton-{sex}.obj')
    manifest = json.loads((BASE / f'skeleton-{sex}.parts.json').read_text())
    expected = {item['name'] for item in manifest['source_bones']}
    assert set(bones) == expected and len(bones) == 182
    links = anatomy()
    assert all(a in bones and b in bones for a, b, _ in links)
    assert len({tuple(sorted((a,b))) for a,b,_ in links}) == len(links)
    connected = {'Head'}
    while True:
        more = connected | {b for a,b,_ in links if a in connected} | {a for a,b,_ in links if b in connected}
        if more == connected: break
        connected = more
    assert connected == expected, f'unconnected structures: {expected-connected}'
    groups = unified_groups()
    member_to_group = {member: group for group, members in groups.items() for member in members}
    assert len(member_to_group) == sum(map(len, groups.values())) and set(member_to_group) <= expected
    lines = {name: [vertices.mean(axis=0)] if name in VERTEBRAE else line_for(name, vertices, bones)
             for name, vertices in bones.items() if name not in member_to_group}
    solids, boxes = {}, {}
    for group, members in groups.items():
        if group.startswith('FootBase_'):
            boxes[group] = foot_hexahedron(bones, members, group[-1])
        else:
            solids[group] = unified_surface(bones, members)
    # Collapse only the explicit anatomical graph: internal group edges vanish;
    # external edges are remapped and duplicates removed, never added by distance.
    visible_links, seen = [], set()
    for original_a, original_b, kind in links:
        a, b = member_to_group.get(original_a, original_a), member_to_group.get(original_b, original_b)
        if a == b: continue
        key = (kind, *sorted((a, b)))
        if key not in seen:
            seen.add(key); visible_links.append((a, b, kind, original_a, original_b))
    out = ['# OpenSkeleton centreline approximation; connections from explicit anatomy, not proximity.',
           '# D vertebra dot; B bone segment; M palm mesh; H foot hexahedron; J joint; C cartilage.']
    for group, members in groups.items(): out.append(f'# Group {group} ' + ' '.join(members))
    def fmt(p): return ' '.join(f'{x:.6f}' for x in p)
    for name in sorted(lines):
        pts = lines[name]
        if name in VERTEBRAE:
            out.append(f'D {name} {fmt(pts[0])}')
        for a,b in zip(pts,pts[1:]):
            if np.linalg.norm(b-a)>1e-7: out.append(f'B {name} {fmt(a)} {fmt(b)}')
    for name in sorted(boxes):
        out.append(f'H {name} ' + ' '.join(fmt(p) for p in boxes[name][0]))
    for name in sorted(solids):
        points, triangles, boundary, owners = solids[name]
        for member, indices in owners.items():
            out.append(f'# Anchor {name} {member} ' + ' '.join(map(str, indices)))
        values = [fmt(p) for p in points] + [' '.join(map(str, t)) for t in triangles] + [' '.join(map(str, e)) for e in boundary]
        out.append(f'M {name} 0.001000 {len(points)} {len(triangles)} {len(boundary)} ' + ' '.join(values))
    def line_segments(name):
        return list(zip(lines[name], lines[name][1:]))
    def solid_to_line(solid, original, line):
        points, _, _, owners = solid
        choices = [closest_to_point(points[index], line) for index in owners[original]]
        return min(choices, key=lambda value: value[0])
    for a,b,kind,original_a,original_b in visible_links:
        # Anatomical endpoints are not supplied. Localise only the already-known
        # relationship with nearest points on its centrelines; never use
        # distance to decide which structures are connected.
        if a in VERTEBRAE and b in VERTEBRAE:
            x, y = lines[a][0], lines[b][0]
        elif a in VERTEBRAE:
            _, x, y = closest_to_point(lines[a][0], lines[b])
        elif b in VERTEBRAE:
            _, y, x = closest_to_point(lines[b][0], lines[a])
        elif a in boxes:
            _, x, y = box_to_line(boxes[a], line_for(original_a, bones[original_a], bones), lines[b])
        elif b in boxes:
            _, y, x = box_to_line(boxes[b], line_for(original_b, bones[original_b], bones), lines[a])
        elif a in solids:
            _, x, y = solid_to_line(solids[a], original_a, lines[b])
        elif b in solids:
            _, y, x = solid_to_line(solids[b], original_b, lines[a])
        else:
            choices = [closest_on_segments(p, q, r, s)
                       for p, q in line_segments(a)
                       for r, s in line_segments(b)]
            _, x, y = min((choice for choice in choices if choice is not None), key=lambda value: value[0])
        out.append(f'{"C" if kind == "cartilage" else "J"} {a} {b} {fmt(x)} {fmt(y)}')
    (BASE / f'skeleton-{sex}.lines').write_text('\n'.join(out)+'\n')
    primitive_count = sum(not row.startswith('#') for row in out)
    print(f'{sex}: {len(bones)} structures, {len(links)} original and {len(visible_links)} visible links, {primitive_count} primitives')

if __name__ == '__main__':
    for sex in ('male','female'): build(sex)
