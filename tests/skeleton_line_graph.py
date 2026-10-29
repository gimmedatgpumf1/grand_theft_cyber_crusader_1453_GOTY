"""Check both displayed anatomical graphs against the source labels.

Connectivity is the explicit textbook-based table, not inferred from distances.
Endpoint positions are approximations because the dataset has no joint landmarks.
"""
from collections import defaultdict, deque
import ast
import json
from itertools import combinations
import math
from pathlib import Path

base = Path(__file__).resolve().parent.parent / 'tools/skeleton-source'
vertebrae = {f'{region}{i}' for region, count in (('C', 7), ('T', 12), ('L', 5))
             for i in range(1, count + 1)}
# Execute only the dependency-free schema function. The generator itself uses
# NumPy for geometry, but verifying the shipped graph needs only stdlib.
source_code = (base / 'openskeleton/04-line-graph.py').read_text()
tree = ast.parse(source_code)
module = ast.Module(body=[node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'anatomy'], type_ignores=[])
namespace = {}
exec(compile(module, 'anatomy schema', 'exec'), namespace)
anatomy = namespace['anatomy']()
assert len(anatomy) == 209
def point_in_solid(point, solid, tolerance=0.0005):
    # Barycentric point-in-triangle in 3D with six-decimal storage tolerance.
    _, points, faces, boundary = solid
    def sub(a,b): return tuple(x-y for x,y in zip(a,b))
    def dot(a,b): return sum(x*y for x,y in zip(a,b))
    for ia, ib, ic in faces:
        a, b, c = points[ia], points[ib], points[ic]
        u, v, w = sub(b,a), sub(c,a), sub(point,a)
        uu, uv, vv, wu, wv = dot(u,u), dot(u,v), dot(v,v), dot(w,u), dot(w,v)
        den = uu*vv - uv*uv
        if den < 1e-20: continue
        s, t = (wu*vv-wv*uv)/den, (wv*uu-wu*uv)/den
        closest = tuple(a[i] + s*u[i] + t*v[i] for i in range(3))
        if s >= -0.002 and t >= -0.002 and s+t <= 1.002 and math.dist(point, closest) < tolerance:
            return True
    # Simplification can put a point just outside the boundary.
    for ia, ib in boundary:
        a,b = points[ia], points[ib]
        v = sub(b,a); den = dot(v,v)
        if den == 0: continue
        t = max(0, min(1, dot(sub(point,a),v)/den))
        if math.dist(point, tuple(a[i]+t*v[i] for i in range(3))) < tolerance: return True
    return False


reference = None
for sex in ('male', 'female'):
    source = json.loads((base / f'skeleton-{sex}.parts.json').read_text())
    expected = {bone['name'] for bone in source['source_bones']}
    segments, edges, dots, groups, solids, anchors, foot_volumes = defaultdict(list), [], {}, {}, {}, defaultdict(dict), set()
    for row in (base / f'skeleton-{sex}.lines').read_text().splitlines():
        if row.startswith('# Anchor '):
            parts = row.split()
            group, name = parts[2:4]
            assert name not in anchors[group]
            anchors[group][name] = list(map(int, parts[4:]))
            continue
        if row.startswith('# Group '):
            parts = row.split()
            assert parts[2] not in groups
            groups[parts[2]] = set(parts[3:])
            assert len(groups[parts[2]]) == len(parts[3:])
            continue
        if not row or row.startswith('#'): continue
        fields = row.split()
        kind = fields[0]
        if kind == 'D':
            assert len(fields) == 5 and fields[1] in vertebrae and fields[1] not in dots
            point = tuple(map(float, fields[2:]))
            assert all(map(math.isfinite, point))
            dots[fields[1]] = point
        elif kind == 'F':
            name = fields[1]
            assert name not in solids and name.startswith('FootBase_') and len(fields) == 32
            values = tuple(map(float, fields[2:]))
            assert all(map(math.isfinite, values))
            points = [values[i:i+3] for i in range(0, len(values), 3)]
            faces = [(4,5,1),(4,1,0), (0,1,3),(0,3,2), (4,6,7),(4,7,5),
                     (6,8,9),(6,9,7), (8,2,3),(8,3,9),
                     (4,0,2),(4,2,8),(4,8,6), (5,7,9),(5,9,3),(5,3,1)]
            solids[name] = (0, points, faces, [])
            foot_volumes.add(name)
        elif kind == 'M':
            name, thickness = fields[1], float(fields[2])
            count, faces, edges_count = map(int, fields[3:6])
            assert name not in solids and 0 < thickness <= 0.001
            assert 3 <= count <= 4096 and len(fields) == 6 + 3*count + 3*faces + 2*edges_count
            end = 6 + 3*count
            values = tuple(map(float, fields[6:end]))
            assert all(map(math.isfinite, values))
            points = [values[i:i+3] for i in range(0, len(values), 3)]
            face_values = list(map(int, fields[end:end+3*faces]))
            face_list = [tuple(face_values[i:i+3]) for i in range(0, len(face_values), 3)]
            edge_values = list(map(int, fields[end+3*faces:]))
            edge_list = [tuple(edge_values[i:i+2]) for i in range(0, len(edge_values), 2)]
            assert all(0 <= i < count for f in face_list for i in f)
            assert all(0 <= i < count for e in edge_list for i in e)
            solids[name] = (thickness, points, face_list, edge_list)
        elif kind == 'B':
            assert len(fields) == 8
            name = fields[1]
            a, b = tuple(map(float, fields[2:5])), tuple(map(float, fields[5:8]))
            assert a != b and all(map(math.isfinite, a+b))
            segments[name].append((a,b))
        else:
            assert kind in ('J', 'C') and len(fields) == 9
            a, b = fields[1:3]
            assert a != b
            ends = tuple(map(float, fields[3:]))
            assert all(map(math.isfinite, ends))
            edges.append((kind, a, b, ends))
    expected_groups = {}
    for side in ('L', 'R'):
        hand = ('HandScaphoid', 'HandLuna', 'HandTriquetrum', 'HandPisiform', 'HandTrapezoideum',
                'HandCapitatum', 'HandHamata', *(f'HandMeta{i}' for i in range(2, 6)))
        foot = ('Tal', 'Cal', 'Nav', 'Cub', 'MedCun', 'InterCun', 'LatCun', 'FootSesaMed',
                'FootSesaLat', *(f'FootMeta{i}' for i in range(1, 6)))
        expected_groups[f'HandPalm_{side}'] = {f'{n}_{side}' for n in hand}
        expected_groups[f'FootBase_{side}'] = {f'{n}_{side}' for n in foot}
        expected_groups[f'Forearm_{side}'] = {f'Radius_{side}', f'Ulna_{side}'}
        expected_groups[f'Shin_{side}'] = {f'Tibia_{side}', f'Fibula_{side}'}
    expected_groups['RibCage'] = {f'Rib{rib}_{side}' for side in ('L','R') for rib in range(1,13)}
    assert groups == expected_groups and set(solids) == set(groups)
    assert foot_volumes == {'FootBase_L', 'FootBase_R'}
    # The solid footprint replaces the outline tubes rather than covering them.
    assert not (set(solids) & set(segments))
    for group, (_, points, faces, boundary) in solids.items():
        if group in foot_volumes:
            edge_set = {tuple(sorted((a,b))) for f in faces for a,b in zip(f, f[1:]+f[:1])}
            segments[group] = [(points[a], points[b]) for a,b in edge_set]
        else:
            segments[group] = [(points[a], points[b]) for a,b in boundary]
        # Filled triangle mesh: one use for each boundary edge, two for
        # each internal edge; only one connected component, including holes.
        counts = defaultdict(int); graph_points = defaultdict(set)
        for face in faces:
            assert len(set(face)) == 3
            for a,b in zip(face, face[1:]+face[:1]):
                counts[tuple(sorted((a,b)))] += 1
                graph_points[a].add(b); graph_points[b].add(a)
        actual_boundary = {edge for edge, count in counts.items() if count == 1}
        assert actual_boundary == {tuple(sorted(edge)) for edge in boundary}
        assert all(count in (1,2) for count in counts.values())
        if group in foot_volumes: assert len(faces) == 16 and all(count == 2 for count in counts.values())
        found_points = {faces[0][0]}; queue = deque(found_points)
        while queue:
            for point in graph_points[queue.popleft()] - found_points:
                found_points.add(point); queue.append(point)
        assert found_points == set(range(len(points))), group
        if group in foot_volumes: continue
        assert set(anchors[group]) == groups[group]
        expected_owned = len(points)-7 if group == 'RibCage' else len(points)
        assert sorted(i for indices in anchors[group].values() for i in indices) == list(range(expected_owned))
        if group.startswith(('Forearm_', 'Shin_', 'RibCage')):
            # Both bones contribute to one filled, connected surface.
            assert any(any(i in anchors[group][name] for i in face) and
                       any(i in anchors[group][other] for i in face)
                       for face in faces for name in groups[group] for other in groups[group] if name != other)
            continue
        # The surface must fill between neighbouring metacarpal/metatarsal
        # axes, not just join individual bone silhouettes with thin slivers.
        prefix = 'Hand' if group.startswith('Hand') else 'Foot'
        side = group[-1]
        start = 2 if prefix == 'Hand' else 1
        for digit in range(start, 5):
            a = set(anchors[group][f'{prefix}Meta{digit}_{side}'])
            b = set(anchors[group][f'{prefix}Meta{digit+1}_{side}'])
            between = [face for face in faces if a.intersection(face) and b.intersection(face)]
            assert between, (group, digit)
            def area(face):
                x,y,z = (points[i] for i in face)
                u, v = [y[i]-x[i] for i in range(3)], [z[i]-x[i] for i in range(3)]
                cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
                return math.sqrt(sum(n*n for n in cross)) / 2
            assert max(map(area, between)) > 0.00005, (group, digit)
    members = {member: group for group, parts in groups.items() for member in parts}
    assert len(members) == sum(map(len, groups.values())) == 82
    assert set(members) <= expected and len(expected) == 182
    visible = (expected - set(members)) | set(groups)
    assert len(visible) == 109
    assert set(dots) == vertebrae and not set(segments) & vertebrae
    assert set(segments) | set(dots) == visible
    # Dot placement is the centre of the corresponding supplied vertebra, not
    # a guessed skeleton-level average or a dot duplicated from another bone.
    totals = defaultdict(lambda: [0.0, 0.0, 0.0, 0])
    member_vertices = defaultdict(list)
    name = None
    with (base / f'skeleton-{sex}.obj').open() as obj:
        for row in obj:
            if row.startswith('g '): name = row[2:].strip()
            elif (name in vertebrae or name in members or name == 'Sternum') and row.startswith('v '):
                coords = list(map(float, row.split()[1:4]))
                if name in members: member_vertices[name].append(coords)
                for i in range(3): totals[name][i] += coords[i]
                totals[name][3] += 1
    for name, point in dots.items():
        sums = totals[name]
        assert sums[3] and math.dist(point, tuple(x / sums[3] for x in sums[:3])) < 0.00001
    for group, part_names in groups.items():
        counts = sum(totals[name][3] for name in part_names)
        assert counts > 0 and all(totals[name][3] for name in part_names)
        source_center = tuple(sum(totals[name][i] for name in part_names) / counts for i in range(3))
        vertices = solids[group][1]
        outline_center = tuple(sum(point[i] for point in vertices) / len(vertices) for i in range(3))
        assert math.dist(source_center, outline_center) < 0.08, group
        span = max(max(point[i] for point in vertices) - min(point[i] for point in vertices) for i in range(3))
        assert 0.05 < span < (0.5 if group.startswith(('Forearm_', 'Shin_', 'RibCage')) else 0.3), group
    # Hands use source-derived anchors; feet have source-derived heel, toe
    # and distal-shin dimensions.
    for group, part_names in groups.items():
        points = solids[group][1]
        if group in foot_volumes:
            # Check actual heel, distal shin and metatarsal landmarks rather
            # than a generic prism or a centrally inset top.
            side = group[-1]
            def centroid(source):
                return tuple(sum(p[i] for p in source)/len(source) for i in range(3))
            def dot(a,b): return sum(x*y for x,y in zip(a,b))
            def sub(a,b): return tuple(x-y for x,y in zip(a,b))
            def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
            def unit(p):
                size = math.sqrt(dot(p,p)); assert size > 1e-9
                return tuple(x/size for x in p)
            def quantile(values, fraction):
                values = sorted(values); at = fraction * (len(values)-1)
                lo = math.floor(at); hi = math.ceil(at)
                return values[lo] + (values[hi]-values[lo])*(at-lo)
            heel, ankle = (centroid(member_vertices[f'{name}_{side}']) for name in ('Cal','Tal'))
            meta = [centroid(member_vertices[f'FootMeta{i}_{side}']) for i in range(1,6)]
            forward = unit(tuple(sum(p[i] for p in meta)/5 - heel[i] for i in range(3)))
            raw_up = sub(ankle, heel)
            up = unit(tuple(raw_up[i]-dot(raw_up,forward)*forward[i] for i in range(3)))
            width = unit(cross(up,forward))
            basis = (forward, width, up)
            local = [tuple(dot(p,axis) for axis in basis) for p in points]
            def project(p): return tuple(dot(p,axis) for axis in basis)
            calc = [project(p) for p in member_vertices[f'Cal_{side}']]
            x0, z0 = min(p[0] for p in calc), min(p[2] for p in calc)
            assert abs(local[4][0]-x0) < 0.00001 and abs(local[5][0]-x0) < 0.00001
            rear_cutoff = quantile([p[0] for p in calc], 0.10)
            rear = [p for p in calc if p[0] <= rear_cutoff]
            assert abs(local[4][1]-min(p[1] for p in rear)) < 0.00001
            assert abs(local[5][1]-max(p[1] for p in rear)) < 0.00001
            assert local[5][1]-local[4][1] < 0.75*(max(p[1] for p in calc)-min(p[1] for p in calc))
            assert abs(local[4][2]-z0) < 0.00001 and abs(local[5][2]-z0) < 0.00001
            distal_ankles, proximal_ankles = [], []
            for name in ('Tibia','Fibula'):
                vertices = member_vertices[f'{name}_{side}']
                low = quantile([p[1] for p in vertices], 0.04)
                high = quantile([p[1] for p in vertices], 0.90)
                distal_ankles.append([p for p in vertices if p[1] <= low])
                proximal_ankles.append([p for p in vertices if p[1] >= high])
            origin = centroid([centroid(bone) for bone in distal_ankles])
            leg = unit(sub(centroid([centroid(bone) for bone in proximal_ankles]), origin))
            along = unit(tuple(forward[i]-dot(forward,leg)*leg[i] for i in range(3)))
            top = [points[i] for i in (6,7,9,8)]
            normal = unit(cross(sub(top[1],top[0]), sub(top[2],top[0])))
            assert abs(dot(normal,leg)) > 0.9999
            assert abs(dot(normal,sub(origin,top[0]))) < 0.00001
            ankle_vertices = [p for bone in distal_ankles for p in bone]
            # The top has exactly the ankle's front-to-back extent: no padding
            # stretches it toward the toes.
            assert abs(min(dot(sub(p,origin),along) for p in top) -
                       min(dot(sub(p,origin),along) for p in ankle_vertices)) < 0.00001
            assert abs(max(dot(sub(p,origin),along) for p in top) -
                       max(dot(sub(p,origin),along) for p in ankle_vertices)) < 0.00001
            assert 0.02 < math.dist(top[0],top[3]) < 0.06
            middle = centroid(top)
            for p in ankle_vertices:
                for i in range(4):
                    edge = sub(top[(i+1)%4], top[i])
                    ref = dot(normal, cross(edge, sub(middle,top[i])))
                    value = dot(normal, cross(edge, sub(p,top[i])))
                    edge_length = math.sqrt(dot(edge,edge))
                    assert value*(1 if ref > 0 else -1) >= -0.00001*edge_length, (group, 'ankle coverage')
            distal_toes = []
            for digit in (1,5):
                vertices = [project(p) for p in member_vertices[f'FootMeta{digit}_{side}']]
                cutoff = quantile([p[0] for p in vertices], 0.70)
                subset = [p for p in vertices if p[0] >= cutoff]
                distal_toes.append((centroid(subset), subset))
            distal_toes.sort(key=lambda item: item[0][1])
            toe_vertices = distal_toes[0][1] + distal_toes[1][1]
            for index, (landmark, _) in zip((0,1), distal_toes):
                assert abs(local[index][0]-landmark[0]) < 0.00001
                assert abs(local[index+2][0]-landmark[0]) < 0.00001
                assert abs(local[index+2][1]-local[index][1]) < 0.00001
                assert local[index+2][2] > local[index][2] + 0.008
            assert abs(local[0][1]-min(p[1] for p in toe_vertices)) < 0.00001
            assert abs(local[1][1]-max(p[1] for p in toe_vertices)) < 0.00001
            assert local[1][1]-local[0][1] > local[5][1]-local[4][1]
            assert 0.1 < (local[0][0]+local[1][0])/2-x0 < 0.25
            # Front face covers every projected distal vertex of both outer
            # metatarsals, including its real vertical thickness.
            for p in toe_vertices:
                t = (p[1]-local[0][1]) / (local[1][1]-local[0][1])
                low = local[0][2] + t*(local[1][2]-local[0][2])
                high = local[2][2] + t*(local[3][2]-local[2][2])
                assert low-0.00001 <= p[2] <= high+0.00001, (group, 'toe coverage')
            assert abs(dot(normal,sub(top[3],top[0]))) < 0.00001
            triangles = solids[group][2]
            oriented = defaultdict(int)
            volume = 0
            for face in triangles:
                a,b,c = (points[i] for i in face)
                v = cross(sub(b,a),sub(c,a))
                assert dot(v,v) > 1e-12, (group, 'degenerate face')
                volume += dot(a,cross(b,c))/6
                for i,j in zip(face,face[1:]+face[:1]): oriented[(i,j)] += 1
            assert volume > 0.00001
            assert all(count == 1 and oriented[(b,a)] == 1 for (a,b),count in list(oriented.items()))
            def intersects(p,q,a,b,c):
                direction, u, v = sub(q,p), sub(b,a), sub(c,a)
                h = cross(direction,v); det = dot(u,h)
                if abs(det) < 1e-12: return False
                f = 1/det; delta = sub(p,a); x = f*dot(delta,h)
                r = cross(delta,u); y = f*dot(direction,r); t = f*dot(v,r)
                return 1e-7 < x < 1-1e-7 and 1e-7 < y and x+y < 1-1e-7 and 1e-7 < t < 1-1e-7
            for first,second in combinations(triangles,2):
                if set(first) & set(second): continue
                for edge,face in ((first,second),(second,first)):
                    a,b,c = (points[i] for i in face)
                    for i in range(3):
                        assert not intersects(points[edge[i]],points[edge[(i+1)%3]],a,b,c), (group,'self intersection')
            continue
        if group == 'RibCage':
            # Independent reconstruction of each rib's angular samples; each
            # curved strip uses the actual registered ribs, not a flat slab.
            def quantile(values, fraction):
                values = sorted(values); pos = fraction * (len(values)-1)
                lo, hi = math.floor(pos), math.ceil(pos)
                return values[lo] + (values[hi]-values[lo])*(pos-lo)
            for name in part_names:
                rib = int(name.split('_')[0][3:])
                t = totals[f'T{rib}']; sternum = totals['Sternum']
                axis = [(t[i]/t[3] + sternum[i]/sternum[3])/2 for i in range(3)]
                source = member_vertices[name]
                angles = [math.atan2(p[2]-axis[2],p[0]-axis[0]) for p in source]
                mean = math.atan2(sum(math.sin(a) for a in angles), sum(math.cos(a) for a in angles))
                angles = [math.atan2(math.sin(a-mean),math.cos(a-mean)) for a in angles]
                limits = [quantile(angles, i/8) for i in range(9)]
                expected_points = []
                for i in range(8):
                    samples = [p for p,a in zip(source,angles)
                               if a >= limits[i] and (a <= limits[i+1] if i == 7 else a < limits[i+1])]
                    assert samples
                    expected_points.append(tuple(sum(p[j] for p in samples)/len(samples) for j in range(3)))
                indices = anchors[group][name]
                assert len(indices) == 8
                assert all(math.dist(points[index], expected) < 0.00001
                           for index,expected in zip(indices,expected_points))
            assert len(solids[group][2]) == 354 and len(points) == 199
            faces = solids[group][2]
            # Every successive pair is joined at every segment, and left and
            # right halves share a full posterior seam. One mesh is not merely
            # separate pieces assigned the same name.
            for side in ('L','R'):
                for rib in range(1,12):
                    a = anchors[group][f'Rib{rib}_{side}']
                    b = anchors[group][f'Rib{rib+1}_{side}']
                    for i in range(7):
                        cell = {a[i],a[i+1],b[i],b[i+1]}
                        assert sum(set(face) <= cell for face in faces) == 2
            for rib in range(1,12):
                cell = {anchors[group][f'Rib{rib}_L'][0], anchors[group][f'Rib{rib+1}_L'][0],
                        anchors[group][f'Rib{rib}_R'][-1], anchors[group][f'Rib{rib+1}_R'][-1]}
                assert sum(set(face) <= cell for face in faces) == 2
            # The seven new front anchors lie exactly on the displayed
            # sternum, and filled faces join them to ribs 1–7 on both sides.
            sternum_points = points[192:199]
            sternum_segments = segments['Sternum']
            def distance_to_line(p,a,b):
                v = tuple(y-x for x,y in zip(a,b))
                t = max(0, min(1, sum((x-y)*z for x,y,z in zip(p,a,v)) / sum(x*x for x in v)))
                return math.dist(p, tuple(x+t*y for x,y in zip(a,v)))
            for point in sternum_points:
                assert min(distance_to_line(point,a,b) for a,b in sternum_segments) < 0.00001
            assert all(math.dist(sternum_points[i],sternum_points[i+1]) > 0.01 for i in range(6))
            for side, offset in (('L',-1),('R',0)):
                for rib in range(1,7):
                    cell = {anchors[group][f'Rib{rib}_{side}'][offset],
                            anchors[group][f'Rib{rib+1}_{side}'][offset], 191+rib, 192+rib}
                    assert sum(set(face) <= cell for face in faces) == 2
            # Floating ribs must not acquire direct front-to-sternum faces.
            for rib in range(8,13):
                for side in ('L','R'):
                    rib_points = set(anchors[group][f'Rib{rib}_{side}'])
                    assert not any(rib_points.intersection(face) and set(range(192,199)).intersection(face)
                                   for face in faces)
            oriented = defaultdict(int)
            for face in faces:
                for a,b in zip(face,face[1:]+face[:1]): oriented[(a,b)] += 1
            assert all(count == 1 for count in oriented.values())
            assert all(oriented[(b,a)] == 1 for (a,b),count in list(oriented.items()) if tuple(sorted((a,b))) not in
                       {tuple(sorted(edge)) for edge in solids[group][3]})
            continue
        if group.startswith(('Forearm_', 'Shin_')):
            def sub(a,b): return tuple(x-y for x,y in zip(a,b))
            def dot(a,b): return sum(x*y for x,y in zip(a,b))
            def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
            normals = [cross(sub(points[i],points[0]), sub(points[j],points[0]))
                       for i in range(1,len(points)) for j in range(i+1,len(points))]
            normal = max(normals, key=lambda n: dot(n,n))
            size = math.sqrt(dot(normal,normal)); assert size > 1e-10
            normal = tuple(x/size for x in normal)
            assert all(abs(dot(sub(p,points[0]),normal)) < 0.00001 for p in points)
            center = tuple(sum(p[i] for p in points)/len(points) for i in range(3))
            for name in part_names:
                source_points = member_vertices[name]
                for index in anchors[group][name]:
                    p = points[index]
                    # Hull vertices are projections of actual vertices belonging
                    # to this bone, not invented matching rectangles.
                    closest = min(math.sqrt(max(0, dot(sub(q,p),sub(q,p))-dot(sub(q,p),normal)**2))
                                  for q in source_points)
                    assert closest < 0.00002, (group,name)
                for p in source_points:
                    for a,b in solids[group][3]:
                        edge = sub(points[b],points[a])
                        ref = dot(normal,cross(edge,sub(center,points[a])))
                        value = dot(normal,cross(edge,sub(p,points[a])))
                        assert value*(1 if ref > 0 else -1) >= -0.00001*math.sqrt(dot(edge,edge)), (group,name,'coverage')
            continue
        for name in part_names:
            indices = anchors[group][name]
            source_points = member_vertices[name]
            mean = tuple(sum(v[i] for v in source_points) / len(source_points) for i in range(3))
            if name.startswith(('HandMeta', 'FootMeta')):
                assert len(indices) == 3 and math.dist(points[indices[1]], mean) < 0.00001
                lo, hi = points[indices[0]], points[indices[2]]
                assert 0.01 < math.dist(lo, hi) < 0.15
                for point in (lo, hi):
                    for axis in range(3):
                        values = [v[axis] for v in source_points]
                        assert min(values) - 0.005 <= point[axis] <= max(values) + 0.005
            else:
                assert len(indices) == 1 and math.dist(points[indices[0]], mean) < 0.00001
    assert all(f'Rib{i}_{side}' not in segments for side in ('L','R') for i in range(1,13))
    assert len(segments['Head']) >= 24
    for name, bone_segments in segments.items():
        if name in solids: continue  # boundary can include holes
        points = defaultdict(set)
        for a, b in bone_segments:
            points[a].add(b); points[b].add(a)
        visited = {bone_segments[0][0]}; queue = deque(visited)
        while queue:
            for point in points[queue.popleft()] - visited:
                visited.add(point); queue.append(point)
        assert visited == set(points), f'{name} has disconnected centreline segments'
    for kind, a, b, ends in edges:
        for bone, point in ((a, ends[:3]), (b, ends[3:])):
            def distance_to_segment(p, a, b):
                v = tuple(y-x for x,y in zip(a,b))
                t = max(0, min(1, sum((x-y)*z for x,y,z in zip(p,a,v)) / sum(x*x for x in v)))
                return math.dist(p, tuple(x+t*y for x,y in zip(a,v)))
            if bone in dots: assert math.dist(point, dots[bone]) < 0.00001, bone
            elif bone in solids:
                assert point_in_solid(point, solids[bone]), bone
            else: assert any(distance_to_segment(point, a, b) < 0.00001 for a,b in segments[bone]), bone
    graph = defaultdict(set)
    topology = set()
    for kind, a, b, _ in edges:
        key = (kind, *sorted((a,b)))
        if key in topology: assert 'RibCage' in (a,b), key
        topology.add(key); graph[a].add(b); graph[b].add(a)
    found = {'Head'}; queue = deque(found)
    while queue:
        for other in graph[queue.popleft()] - found:
            found.add(other); queue.append(other)
    assert found == visible and len(edges) == 145
    collapsed = set()
    expected_anchors = defaultdict(set)
    for original_a, original_b, kind in anatomy:
        a, b = members.get(original_a, original_a), members.get(original_b, original_b)
        if a != b:
            key = ('C' if kind == 'cartilage' else 'J', *sorted((a,b)))
            collapsed.add(key)
            if a in solids and a not in foot_volumes: expected_anchors[(key, a)].update(anchors[a][original_a])
            if b in solids and b not in foot_volumes: expected_anchors[(key, b)].update(anchors[b][original_b])
    assert topology == collapsed
    for kind, a, b, ends in edges:
        key = (kind, *sorted((a,b)))
        for group, point in ((a, ends[:3]), (b, ends[3:])):
            if group in solids and group not in foot_volumes:
                assert any(math.dist(point, solids[group][1][i]) < 0.00001
                           for i in expected_anchors[(key, group)]), (group, key)
    for side in ('L','R'):
        for i in range(1, 13):
            assert f'T{i}' in graph['RibCage']
            # Every anatomical rib has its own link to the appropriate T dot,
            # despite both sides sharing the same mesh and graph label.
            rib_anchors = anchors['RibCage'][f'Rib{i}_{side}']
            matching = [ends for kind,a,b,ends in edges if kind == 'J' and
                        {a,b} == {f'T{i}', 'RibCage'}]
            assert len(matching) == 2
            assert any(any(math.dist((ends[:3] if a == 'RibCage' else ends[3:]),
                                     solids['RibCage'][1][j]) < 0.00001 for j in rib_anchors)
                       for kind,a,b,ends in edges if kind == 'J' and {a,b} == {f'T{i}', 'RibCage'})
            if i <= 7:
                assert any(any(math.dist((ends[:3] if a == 'RibCage' else ends[3:]),
                                         solids['RibCage'][1][j]) < 0.00001 for j in rib_anchors)
                           for kind,a,b,ends in edges if kind == 'C' and {a,b} == {'Sternum', 'RibCage'})
        assert f'HandTrapezium_{side}' in graph[f'HandPalm_{side}']
        assert f'HandMeta1_{side}' in graph[f'HandTrapezium_{side}']
        for digit in range(1, 6):
            for prefix in ('Hand', 'Foot'):
                base_bone = (f'HandMeta1_{side}' if prefix == 'Hand' and digit == 1 else
                             f'HandPalm_{side}' if prefix == 'Hand' else f'FootBase_{side}')
                assert f'{prefix}ProxP{digit}_{side}' in graph[base_bone]
                last = f'{prefix}ProxP{digit}_{side}' if digit == 1 else f'{prefix}MidP{digit}_{side}'
                assert f'{prefix}DistP{digit}_{side}' in graph[last]
    if reference is None: reference = topology
    else: assert reference == topology
    print(f'{sex} line graph: PASS ({len(expected)} structures, {len(edges)} visible links, {len(foot_volumes)} ten-point feet, 2 palm surfaces, 4 paired-bone planes, one connected rib cage, {len(dots)} vertebra dots)')
