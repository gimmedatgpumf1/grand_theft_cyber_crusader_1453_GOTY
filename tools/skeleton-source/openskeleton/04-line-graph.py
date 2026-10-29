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
    lines = {name: [vertices.mean(axis=0)] if name in VERTEBRAE else line_for(name, vertices, bones)
             for name, vertices in bones.items()}
    out = ['# OpenSkeleton centreline approximation; connections from explicit anatomy, not proximity.',
           '# D vertebra dot; B bone segment; J joint connection; C inferred cartilage connection.']
    def fmt(p): return ' '.join(f'{x:.6f}' for x in p)
    for name in sorted(lines):
        pts = lines[name]
        if name in VERTEBRAE:
            out.append(f'D {name} {fmt(pts[0])}')
        for a,b in zip(pts,pts[1:]):
            if np.linalg.norm(b-a)>1e-7: out.append(f'B {name} {fmt(a)} {fmt(b)}')
    for a,b,kind in links:
        # Anatomical endpoints are not supplied. Localise only the already-known
        # relationship with nearest points on its centrelines; never use
        # distance to decide which structures are connected.
        if a in VERTEBRAE and b in VERTEBRAE:
            x, y = lines[a][0], lines[b][0]
        elif a in VERTEBRAE:
            _, x, y = closest_to_point(lines[a][0], lines[b])
        elif b in VERTEBRAE:
            _, y, x = closest_to_point(lines[b][0], lines[a])
        else:
            choices = [closest_on_segments(p, q, r, s)
                       for p, q in zip(lines[a], lines[a][1:])
                       for r, s in zip(lines[b], lines[b][1:])]
            _, x, y = min((choice for choice in choices if choice is not None), key=lambda value: value[0])
        out.append(f'{"C" if kind == "cartilage" else "J"} {a} {b} {fmt(x)} {fmt(y)}')
    (BASE / f'skeleton-{sex}.lines').write_text('\n'.join(out)+'\n')
    print(f'{sex}: {len(bones)} structures, {len(links)} anatomical relationships, {len(out)-2} primitives')

if __name__ == '__main__':
    for sex in ('male','female'): build(sex)
