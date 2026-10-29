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
    """Fit a heel-to-toe solid with a real ankle-sized upper face.

    The sole follows the source heel and outer metatarsal heads. The top is a
    quadrilateral around the distal tibia and fibula, not their connecting line.
    The registered feet point downward, so the sole follows the source pose.
    """
    heel = bones[f'Cal_{side}']
    ankle = bones[f'Tal_{side}']
    metatarsals = [bones[f'FootMeta{i}_{side}'] for i in range(1, 6)]
    forward = np.mean([bone.mean(axis=0) for bone in metatarsals], axis=0) - heel.mean(axis=0)
    forward /= np.linalg.norm(forward)
    up = ankle.mean(axis=0) - heel.mean(axis=0)
    up -= np.dot(up, forward) * forward
    assert np.linalg.norm(up) > 1e-8
    up /= np.linalg.norm(up)
    width = np.cross(up, forward)
    width /= np.linalg.norm(width)
    axes = np.array([forward, width, up])
    calc = heel @ axes.T
    # Width at the back of the calcaneus, not its much wider midsection.
    rear = calc[calc[:, 0] <= np.quantile(calc[:, 0], 0.10)]
    heads = []
    for bone in (metatarsals[0], metatarsals[4]):
        vertices = bone @ axes.T
        distal = vertices[vertices[:, 0] >= np.quantile(vertices[:, 0], 0.90)]
        heads.append((distal.mean(axis=0), distal))
    heads.sort(key=lambda item: item[0][1])
    toes = np.concatenate([item[1] for item in heads])
    x0, z0 = calc[:, 0].min(), calc[:, 2].min()
    x1 = toes[:, 0].max()
    toe_height = np.mean([np.ptp((bone @ axes.T)[:, 2]) for bone in (metatarsals[0], metatarsals[4])])
    z1 = toes[:, 2].min() - 0.35 * toe_height
    def sole_height(x): return z0 + (z1-z0) * (x-x0) / (x1-x0)
    low_x, high_x = (item[1][:, 0].max() for item in heads)
    bottom = np.array([[x0, rear[:, 1].min(), z0],
                       [x0, rear[:, 1].max(), z0],
                       [high_x, toes[:, 1].max(), sole_height(high_x)],
                       [low_x, toes[:, 1].min(), sole_height(low_x)]]) @ axes
    distal, proximal = [], []
    for name in ('Tibia', 'Fibula'):
        vertices = bones[f'{name}_{side}']
        # The registered subjects' lower shin ends are at minimum world Y.
        distal.append(vertices[vertices[:, 1] <= np.quantile(vertices[:, 1], 0.08)])
        proximal.append(vertices[vertices[:, 1] >= np.quantile(vertices[:, 1], 0.90)])
    origin = np.mean([p.mean(axis=0) for p in distal], axis=0)
    leg = np.mean([p.mean(axis=0) for p in proximal], axis=0) - origin
    leg /= np.linalg.norm(leg)
    along = forward - np.dot(forward, leg)*leg
    along /= np.linalg.norm(along)
    across = np.cross(leg, along)
    if np.dot(across, width) < 0: across = -across
    footprint = (np.concatenate(distal) - origin) @ np.array([along, across]).T
    u0, u1 = footprint[:, 0].min(), footprint[:, 0].max()
    near = footprint[footprint[:, 0] <= np.quantile(footprint[:, 0], 0.25)]
    far = footprint[footprint[:, 0] >= np.quantile(footprint[:, 0], 0.75)]
    t = (footprint[:, 0] - u0) / (u1-u0)
    dl = far[:, 1].min()-near[:, 1].min()
    dh = far[:, 1].max()-near[:, 1].max()
    v0 = np.min(footprint[:, 1]-t*dl)
    v1 = np.max(footprint[:, 1]-t*dh)
    target = np.array([origin + along*u0 + across*v0,
                       origin + along*u0 + across*v1,
                       origin + along*u1 + across*(v1+dh),
                       origin + along*u1 + across*(v0+dl)])
    def plane(a, b, c):
        normal = np.cross(b-a, c-a)
        assert np.linalg.norm(normal) > 1e-10
        return np.r_[normal, -np.dot(normal, a)]
    top_plane = plane(target[0], target[1], target[2])
    centre = target.mean(axis=0)
    # A planar hexahedron cannot generally use two arbitrary source quads
    # verbatim. Keep the measured sole, fit four planar sides, then enlarge the
    # ankle face just enough to contain both projected distal-bone footprints.
    def cross2(a, b): return a[0]*b[1] - a[1]*b[0]
    for scale in np.linspace(1.0, 3.0, 101):
        candidate = centre + (target-centre)*scale
        sides = [plane(bottom[i], bottom[(i+1)%4], (candidate[i]+candidate[(i+1)%4])/2) for i in range(4)]
        top = []
        for i in range(4):
            equations = np.stack((top_plane, sides[i-1], sides[i]))
            top.append(np.linalg.solve(equations[:, :3], -equations[:, 3]))
        top = np.array(top)
        polygon = (top-origin) @ np.array([along, across]).T
        middle = polygon.mean(axis=0)
        if all(cross2(polygon[(i+1)%4]-polygon[i], p-polygon[i]) *
               cross2(polygon[(i+1)%4]-polygon[i], middle-polygon[i]) >= -1e-12
               for p in footprint for i in range(4)):
            break
    else:
        raise ValueError(f'ankle footprint does not fit foot top: {side}')
    # Binary indices are rear/front, low/high width, bottom/top.
    order = (0, 1, 3, 2)
    points = []
    for index in order: points.extend((bottom[index], top[index]))
    return np.array(points), axes


def box_to_line(box, source, line):
    """Attach an anatomical link to the low hexahedron's actual surface."""
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
    if not choices:
        # Thin feet intentionally do not enclose the tall ankle bones. Project
        # the selected bone's anchors to the closest triangle on the surface.
        def nearest_triangle(p, a, b, c):
            u, v, w = b-a, c-a, p-a
            uu, uv, vv, wu, wv = np.dot(u,u), np.dot(u,v), np.dot(v,v), np.dot(w,u), np.dot(w,v)
            den = uu*vv - uv*uv
            s, t = (wu*vv-wv*uv)/den, (wv*uu-wu*uv)/den
            candidates = []
            if s >= 0 and t >= 0 and s+t <= 1: candidates.append(a+s*u+t*v)
            for x, y in ((a,b),(b,c),(c,a)):
                d = y-x
                candidates.append(x + np.clip(np.dot(p-x,d)/np.dot(d,d), 0, 1)*d)
            return min(candidates, key=lambda q: np.linalg.norm(p-q))
        projected = []
        for p in source:
            candidates = [nearest_triangle(p, points[q[0]], points[q[j]], points[q[j+1]])
                          for q in quads for j in (1,2)]
            projected.append(min(candidates, key=lambda q: np.linalg.norm(p-q)))
        return min((closest_to_point(p, line) for p in projected), key=lambda value: value[0])
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
