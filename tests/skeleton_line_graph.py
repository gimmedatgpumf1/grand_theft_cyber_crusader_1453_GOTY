"""Check both displayed anatomical graphs against the source labels.

Connectivity is the explicit textbook-based table, not inferred from distances.
Endpoint positions are approximations because the dataset has no joint landmarks.
"""
from collections import Counter, defaultdict, deque
import json
import math
from pathlib import Path

base = Path(__file__).resolve().parent.parent / 'tools/skeleton-source'
reference = None
for sex in ('male', 'female'):
    source = json.loads((base / f'skeleton-{sex}.parts.json').read_text())
    expected = {bone['name'] for bone in source['source_bones']}
    segments, edges = defaultdict(list), []
    for row in (base / f'skeleton-{sex}.lines').read_text().splitlines():
        if not row or row.startswith('#'): continue
        fields = row.split()
        kind = fields[0]
        if kind == 'B':
            assert len(fields) == 8
            name = fields[1]
            a, b = tuple(map(float, fields[2:5])), tuple(map(float, fields[5:8]))
            assert name in expected and a != b and all(map(math.isfinite, a+b))
            segments[name].append((a,b))
        else:
            assert kind in ('J', 'C') and len(fields) == 9
            a, b = fields[1:3]
            assert a in expected and b in expected and a != b
            ends = tuple(map(float, fields[3:]))
            assert all(map(math.isfinite, ends))
            edges.append((kind, a, b, ends))
    assert set(segments) == expected and len(expected) == 182
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
            assert any(distance_to_segment(point, a, b) < 0.00001 for a,b in segments[bone]), bone
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
    assert found == expected and len(topology) == 209
    for side in ('L','R'):
        for i in range(1, 13):
            assert f'T{i}' in graph[f'Rib{i}_{side}']
            if i <= 7: assert ('C', *sorted(('Sternum',f'Rib{i}_{side}'))) in topology
            if i >= 11: assert 'Sternum' not in graph[f'Rib{i}_{side}']
        for digit in range(1, 6):
            for prefix in ('Hand', 'Foot'):
                assert f'{prefix}ProxP{digit}_{side}' in graph[f'{prefix}Meta{digit}_{side}']
                last = f'{prefix}ProxP{digit}_{side}' if digit == 1 else f'{prefix}MidP{digit}_{side}'
                assert f'{prefix}DistP{digit}_{side}' in graph[last]
    if reference is None: reference = topology
    else: assert reference == topology
    print(f'{sex} line graph: PASS ({len(expected)} structures, {len(topology)} named links)')
