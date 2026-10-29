"""Check both displayed anatomical graphs against the source labels.

Connectivity is the explicit textbook-based table, not inferred from distances.
Endpoint positions are approximations because the dataset has no joint landmarks.
"""
from collections import defaultdict, deque
import ast
import json
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
    segments, edges, dots, groups, solids, anchors, hexahedra = defaultdict(list), [], {}, {}, {}, defaultdict(dict), set()
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
        elif kind == 'H':
            name = fields[1]
            assert name not in solids and name.startswith('FootBase_') and len(fields) == 26
            values = tuple(map(float, fields[2:]))
            assert all(map(math.isfinite, values))
            points = [values[i:i+3] for i in range(0, len(values), 3)]
            quads = ((0,1,3,2), (4,6,7,5), (0,4,5,1), (2,3,7,6), (0,2,6,4), (1,5,7,3))
            faces = [(q[0],q[1],q[2]) for q in quads] + [(q[0],q[2],q[3]) for q in quads]
            solids[name] = (0, points, faces, [])
            hexahedra.add(name)
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
    assert groups == expected_groups and set(solids) == set(groups)
    assert hexahedra == {'FootBase_L', 'FootBase_R'}
    # The solid footprint replaces the outline tubes rather than covering them.
    assert not (set(solids) & set(segments))
    for group, (_, points, faces, boundary) in solids.items():
        if group in hexahedra:
            segments[group] = [(points[i], points[i ^ bit]) for i in range(8) for bit in (1,2,4) if i < (i ^ bit)]
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
        if group in hexahedra: assert len(faces) == 12 and all(count == 2 for count in counts.values())
        found_points = {faces[0][0]}; queue = deque(found_points)
        while queue:
            for point in graph_points[queue.popleft()] - found_points:
                found_points.add(point); queue.append(point)
        assert found_points == set(range(len(points))), group
        if group in hexahedra: continue
        assert set(anchors[group]) == groups[group]
        assert sorted(i for indices in anchors[group].values() for i in indices) == list(range(len(points)))
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
    assert len(members) == sum(map(len, groups.values())) == 50
    assert set(members) <= expected and len(expected) == 182
    visible = (expected - set(members)) | set(groups)
    assert len(visible) == 136
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
            elif (name in vertebrae or name in members) and row.startswith('v '):
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
        assert 0.05 < span < 0.3, group
    # Hands use source-derived anchors; feet use a low hexahedron whose sole
    # reaches the actual heel and spans the source bone footprint.
    for group, part_names in groups.items():
        points = solids[group][1]
        if group in hexahedra:
            # The bottom covers the full bone footprint and heel, while a
            # homothetically inset top makes a low, closed pyramid frustum.
            side = group[-1]
            def centroid(name):
                source = member_vertices[name]
                return tuple(sum(p[i] for p in source)/len(source) for i in range(3))
            def dot(a,b): return sum(x*y for x,y in zip(a,b))
            def sub(a,b): return tuple(x-y for x,y in zip(a,b))
            def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
            def unit(p):
                size = math.sqrt(dot(p,p)); assert size > 1e-9
                return tuple(x/size for x in p)
            heel, ankle = centroid(f'Cal_{side}'), centroid(f'Tal_{side}')
            meta = [centroid(f'FootMeta{i}_{side}') for i in range(1,6)]
            forward = unit(tuple(sum(p[i] for p in meta)/5 - heel[i] for i in range(3)))
            raw_up = sub(ankle, heel)
            up = unit(tuple(raw_up[i]-dot(raw_up,forward)*forward[i] for i in range(3)))
            width = unit(cross(up,forward))
            basis = (forward, width, up)
            local = [tuple(dot(p,axis) for axis in basis) for p in points]
            all_bones = [p for member in part_names for p in member_vertices[member]]
            rear_bones = member_vertices[f'Cal_{side}'] + member_vertices[f'Tal_{side}']
            front_bones = [p for i in range(1,6) for p in member_vertices[f'FootMeta{i}_{side}']]
            x0, x1 = local[0][0], local[4][0]
            assert 0.1 < x1-x0 < 0.3
            assert abs(x0 - min(dot(p,forward) for p in all_bones)) < 0.00001
            assert abs(x1 - max(dot(p,forward) for p in all_bones)) < 0.00001
            assert abs(local[2][0]-x0) < 0.00001 and abs(local[6][0]-x1) < 0.00001
            for axis, bit in ((1,2),(2,0)):
                rear = [dot(p,basis[axis]) for p in rear_bones]
                front = [dot(p,basis[axis]) for p in front_bones]
                low0, low1 = local[0][axis], local[4][axis]
                assert abs((low1-low0) - (min(front)-min(rear))) < 0.00001
                if bit:
                    high0, high1 = local[bit][axis], local[4+bit][axis]
                    assert abs((high1-high0) - (max(front)-max(rear))) < 0.00001
                residuals = []
                for p in all_bones:
                    t = (dot(p,forward)-x0)/(x1-x0)
                    value = dot(p,basis[axis]); lower = low0 + t*(low1-low0)
                    assert value >= lower - 0.00001
                    if bit:
                        upper = high0 + t*(high1-high0)
                        assert value <= upper + 0.00001
                        residuals.append(upper-value)
                if bit: assert min(residuals) < 0.00001
            assert local[6][1]-local[4][1] > local[2][1]-local[0][1] + 0.005
            centre = [sum(local[i][axis] for i in (0,2,4,6))/4 for axis in range(3)]
            rear_z = [dot(p,up) for p in rear_bones]
            front_z = [dot(p,up) for p in front_bones]
            thickness = 0.4 * ((max(rear_z)-min(rear_z)) + (max(front_z)-min(front_z))) / 2
            assert 0.01 < thickness < 0.04
            for i in (0,2,4,6):
                expected_top = [centre[a] + 0.58*(local[i][a]-centre[a]) + (thickness if a == 2 else 0)
                                for a in range(3)]
                assert math.dist(local[i+1], expected_top) < 0.00001
            # Upper footprint is inset at both ends and on both sides.
            assert x0 < local[1][0] < local[5][0] < x1
            assert local[3][1]-local[1][1] < local[2][1]-local[0][1]
            assert local[7][1]-local[5][1] < local[6][1]-local[4][1]
            quads = ((0,1,3,2),(4,6,7,5),(0,4,5,1),(2,3,7,6),(0,2,6,4),(1,5,7,3))
            for a,b,c,d in quads:
                normal = unit(cross(sub(points[b],points[a]), sub(points[c],points[a])))
                assert abs(dot(normal,sub(points[d],points[a]))) < 0.00001
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
    assert all(len(segments[f'Rib{i}_{side}']) >= 5 for side in ('L','R') for i in range(1,13))
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
        assert key not in topology
        topology.add(key); graph[a].add(b); graph[b].add(a)
    found = {'Head'}; queue = deque(found)
    while queue:
        for other in graph[queue.popleft()] - found:
            found.add(other); queue.append(other)
    assert found == visible and len(topology) == 159
    collapsed = set()
    expected_anchors = defaultdict(set)
    for original_a, original_b, kind in anatomy:
        a, b = members.get(original_a, original_a), members.get(original_b, original_b)
        if a != b:
            key = ('C' if kind == 'cartilage' else 'J', *sorted((a,b)))
            collapsed.add(key)
            if a in solids and a not in hexahedra: expected_anchors[(key, a)].update(anchors[a][original_a])
            if b in solids and b not in hexahedra: expected_anchors[(key, b)].update(anchors[b][original_b])
    assert topology == collapsed
    for kind, a, b, ends in edges:
        key = (kind, *sorted((a,b)))
        for group, point in ((a, ends[:3]), (b, ends[3:])):
            if group in solids and group not in hexahedra:
                assert any(math.dist(point, solids[group][1][i]) < 0.00001
                           for i in expected_anchors[(key, group)]), (group, key)
    for side in ('L','R'):
        for i in range(1, 13):
            assert f'T{i}' in graph[f'Rib{i}_{side}']
            if i <= 7: assert ('C', *sorted(('Sternum',f'Rib{i}_{side}'))) in topology
            if i >= 11: assert 'Sternum' not in graph[f'Rib{i}_{side}']
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
    print(f'{sex} line graph: PASS ({len(expected)} structures, {len(topology)} visible links, {len(hexahedra)} foot hexahedra, {len(solids)-len(hexahedra)} palm surfaces, {len(dots)} vertebra dots)')
