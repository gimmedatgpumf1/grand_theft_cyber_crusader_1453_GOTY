"""Verify source-derived T poses, the joint graph and rigid bone transforms."""
import ast
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import runpy

base = Path(__file__).resolve().parent.parent / 'tools/skeleton-source'
tree = ast.parse((base / 'openskeleton/04-line-graph.py').read_text())
body = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'anatomy']
ns = {}
exec(compile(ast.Module(body=body, type_ignores=[]), 'anatomy', 'exec'), ns)
edges = ns['anatomy']()
classes = runpy.run_path(str(base / 'openskeleton/joint_mobility.py'))['mobility']
counts = Counter(classes(a,b,kind) for a,b,kind in edges)
assert sum(counts.values()) == 209 and len(counts) == 31
assert counts['finger_mcp_condyloid'] == counts['finger_pip_hinge'] == counts['finger_dip_hinge'] == 8
assert counts['thumb_mcp'] == counts['thumb_ip_hinge'] == counts['thumb_cmc_saddle'] == 2
assert counts['toe_ip_hinge'] == 18
links = {frozenset((a,b)) for a,b,_ in edges}

def read_obj(path):
    bones = defaultdict(list)
    faces = []
    name = None
    for row in path.open():
        if row.startswith('g '): name = row[2:].strip()
        elif row.startswith('v '): bones[name].append(tuple(map(float, row.split()[1:4])))
        elif row.startswith('f '): faces.append(row)
    return bones, faces

def distance(a,b): return math.dist(a,b)

for sex in ('male','female'):
    original, faces = read_obj(base / f'skeleton-{sex}.obj')
    posed, posed_faces = read_obj(base / f'skeleton-{sex}-tpose.obj')
    transforms = json.loads((base / f'skeleton-{sex}-tpose.json').read_text())
    assert len(original) == len(posed) == len(transforms) == 182
    assert set(original) == set(posed) == set(transforms) and faces == posed_faces
    for name, transform in transforms.items():
        parent, matrix, offset = transform['parent'], transform['rotation'], transform['offset']
        if name == 'Sacrum': assert parent is None
        else: assert frozenset((name,parent)) in links
        # Tree cannot cycle and every chain reaches the root.
        visited = {name}
        while parent is not None:
            assert parent not in visited
            visited.add(parent)
            parent = transforms[parent]['parent']
        assert 'Sacrum' in visited
        for i in range(3):
            for j in range(3):
                dot = sum(matrix[k][i]*matrix[k][j] for k in range(3))
                assert abs(dot-(1 if i == j else 0)) < 1e-9
        determinant = (matrix[0][0]*(matrix[1][1]*matrix[2][2]-matrix[1][2]*matrix[2][1])
                       -matrix[0][1]*(matrix[1][0]*matrix[2][2]-matrix[1][2]*matrix[2][0])
                       +matrix[0][2]*(matrix[1][0]*matrix[2][1]-matrix[1][1]*matrix[2][0]))
        assert abs(determinant-1) < 1e-9, name
        assert len(original[name]) == len(posed[name])
        for source, actual in zip(original[name], posed[name]):
            expected = tuple(sum(matrix[i][j]*source[j] for j in range(3))+offset[i] for i in range(3))
            assert distance(expected, actual) < 0.000001, (sex,name)
    balls = {}
    for row in (base / f'skeleton-{sex}-tpose.lines').read_text().splitlines():
        if row.startswith('S '):
            _,name,x,y,z,radius = row.split()
            balls[name] = (float(x), float(y), float(z))
    for side, sign in (('L',1),('R',-1)):
        sh, elbow = balls['Shoulder_'+side], balls['Elbow_'+side]
        hip, knee = balls['Hip_'+side], balls['Knee_'+side]
        assert (elbow[0]-sh[0])*sign > 0.25
        assert abs(elbow[1]-sh[1]) < 0.03 and abs(elbow[2]-sh[2]) < 0.03
        assert hip[1]-knee[1] > 0.35 and abs(hip[0]-knee[0]) < 0.04
        wrist = posed['HandScaphoid_'+side]
        center = tuple(sum(p[i] for p in wrist)/len(wrist) for i in range(3))
        assert (center[0]-elbow[0])*sign > 0.18 and abs(center[1]-elbow[1]) < 0.05
    assert abs(min(p[1] for points in posed.values() for p in points)) < 0.000002
    print(f'{sex}: 182 rigid source bones, anatomical tree, unchanged faces, T-pose alignment PASS')
print('All 209 graph relationships have mobility classes: PASS')
