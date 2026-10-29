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
reference = None
for sex in ('male', 'female'):
    source = json.loads((base / f'skeleton-{sex}.parts.json').read_text())
    expected = {bone['name'] for bone in source['source_bones']}
    segments, edges, dots, groups, solids = defaultdict(list), [], {}, {}, {}
    for row in (base / f'skeleton-{sex}.lines').read_text().splitlines():
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
        elif kind == 'P':
            name, thickness, count = fields[1], float(fields[2]), int(fields[3])
            assert name not in solids and 3 <= count <= 12 and len(fields) == 4 + 3 * count
            values = tuple(map(float, fields[4:]))
            assert all(map(math.isfinite, values)) and 0.008 <= thickness <= 0.08
            points = [values[i:i+3] for i in range(0, len(values), 3)]
            solids[name] = (thickness, points)
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
    # The solid footprint replaces the outline tubes rather than covering them.
    assert not (set(solids) & set(segments))
    for group, (_, points) in solids.items():
        segments[group] = list(zip(points, points[1:] + points[:1]))
        def sub(a, b): return tuple(x-y for x, y in zip(a, b))
        def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
        def dot(a, b): return sum(x*y for x,y in zip(a,b))
        triangles = [cross(sub(points[i],points[0]), sub(points[i+1],points[0]))
                     for i in range(1, len(points)-1)]
        normal = tuple(sum(t[i] for t in triangles) for i in range(3))
        length = math.sqrt(dot(normal, normal))
        assert length > 1e-10
        normal = tuple(x/length for x in normal)
        for i, point in enumerate(points):
            assert abs(dot(sub(point,points[0]), normal)) < 0.00001
            assert dot(cross(sub(points[(i+1)%len(points)],point),
                             sub(points[(i+2)%len(points)],points[(i+1)%len(points)])), normal) > -1e-9
    members = {member: group for group, parts in groups.items() for member in parts}
    assert len(members) == sum(map(len, groups.values())) == 50
    assert set(members) <= expected and len(expected) == 182
    visible = (expected - set(members)) | set(groups)
    assert len(visible) == 136
    assert set(dots) == vertebrae and not set(segments) & vertebrae
    assert set(segments) | set(dots) == visible
    # Each solid has one closed footprint, not its old individual bones.
    for group in groups:
        shape = segments[group]
        assert 3 <= len(shape) <= 12 and shape[0][0] == shape[-1][1]
        assert all(a[1] == b[0] for a, b in zip(shape, shape[1:]))
    # Dot placement is the centre of the corresponding supplied vertebra, not
    # a guessed skeleton-level average or a dot duplicated from another bone.
    totals = defaultdict(lambda: [0.0, 0.0, 0.0, 0])
    name = None
    with (base / f'skeleton-{sex}.obj').open() as obj:
        for row in obj:
            if row.startswith('g '): name = row[2:].strip()
            elif (name in vertebrae or name in members) and row.startswith('v '):
                coords = list(map(float, row.split()[1:4]))
                for i in range(3): totals[name][i] += coords[i]
                totals[name][3] += 1
    for name, point in dots.items():
        sums = totals[name]
        assert sums[3] and math.dist(point, tuple(x / sums[3] for x in sums[:3])) < 0.00001
    for group, part_names in groups.items():
        counts = sum(totals[name][3] for name in part_names)
        assert counts > 0 and all(totals[name][3] for name in part_names)
        source_center = tuple(sum(totals[name][i] for name in part_names) / counts for i in range(3))
        vertices = [a for a, _ in segments[group]]
        outline_center = tuple(sum(point[i] for point in vertices) / len(vertices) for i in range(3))
        assert math.dist(source_center, outline_center) < 0.03, group
        span = max(max(point[i] for point in vertices) - min(point[i] for point in vertices) for i in range(3))
        assert 0.05 < span < 0.3, group
    assert all(len(segments[f'Rib{i}_{side}']) >= 5 for side in ('L','R') for i in range(1,13))
    assert len(segments['Head']) >= 24
    for name, bone_segments in segments.items():
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
    for a, b, kind in anatomy:
        a, b = members.get(a, a), members.get(b, b)
        if a != b: collapsed.add(('C' if kind == 'cartilage' else 'J', *sorted((a,b))))
    assert topology == collapsed
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
    print(f'{sex} line graph: PASS ({len(expected)} structures, {len(topology)} visible links, {len(solids)} solid shapes, {len(dots)} vertebra dots)')
