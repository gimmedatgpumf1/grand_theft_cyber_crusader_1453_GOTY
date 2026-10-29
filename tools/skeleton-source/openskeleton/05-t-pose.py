"""Create a static T-pose from the labelled OpenSkeleton sources.

Each source structure is a rigid body. A spanning tree is chosen only from the
explicit anatomical graph; when a joint rotates, all of its descendants rotate
with it. Closed-loop anatomical connections remain in the graph, but are not
kinematic tree edges. Source surfaces supply approximate pivots, not landmarks.
The generated pose and joint centres are visual approximations, not clinical data.
"""
from collections import defaultdict, deque
import importlib.util
import json
from pathlib import Path

import numpy as np
from scipy.spatial import cKDTree

HERE = Path(__file__).resolve().parent
BASE = HERE.parent
spec = importlib.util.spec_from_file_location('line_graph', HERE / '04-line-graph.py')
graph = importlib.util.module_from_spec(spec)
spec.loader.exec_module(graph)


def hierarchy():
    adjacent = defaultdict(list)
    for a, b, _ in graph.anatomy():
        adjacent[a].append(b)
        adjacent[b].append(a)
    parents = {'Sacrum': None}
    pending = deque(['Sacrum'])
    while pending:
        parent = pending.popleft()
        for child in adjacent[parent]:
            if child not in parents:
                parents[child] = parent
                pending.append(child)
    assert len(parents) == 182
    return parents


def descendants(parents, root):
    found = {root}
    pending = deque([root])
    while pending:
        parent = pending.popleft()
        for child, candidate in parents.items():
            if candidate == parent:
                assert child not in found
                found.add(child)
                pending.append(child)
    return found


def contact(first, second):
    # Contacts choose a pivot only after the explicit graph chooses the pair.
    distance, index = cKDTree(second).query(first)
    mask = distance <= np.quantile(distance, 0.01)
    return ((first[mask] + second[index[mask]]) * 0.5).mean(axis=0)


def align(source, target):
    source, target = source / np.linalg.norm(source), target / np.linalg.norm(target)
    v = np.cross(source, target)
    sine, cosine = np.linalg.norm(v), np.dot(source, target)
    if sine < 1e-8:
        if cosine > 0: return np.eye(3)
        guide = np.eye(3)[np.argmin(np.abs(source))]
        v = np.cross(source, guide)
        v /= np.linalg.norm(v)
        return 2 * np.outer(v, v) - np.eye(3)
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + k + k @ k * ((1-cosine) / (sine*sine))


def build(sex):
    source = BASE / f'skeleton-{sex}.obj'
    bones = graph.read_bones(source)
    parents = hierarchy()
    assert set(parents) == set(bones)
    rotation = {n: np.eye(3) for n in bones}
    offset = {n: np.zeros(3) for n in bones}
    def posed(name): return bones[name] @ rotation[name].T + offset[name]
    def turn(names, matrix, pivot):
        for name in names:
            rotation[name] = matrix @ rotation[name]
            offset[name] = matrix @ (offset[name] - pivot) + pivot
    for side in ('L', 'R'):
        def full(name): return f'{name}_{side}'
        def bone(name): return posed(full(name))
        def joint(a, b): return contact(bone(a), bone(b))
        def subtree(name): return descendants(parents, full(name))
        # Raise the entire arm about its glenohumeral contact; straighten
        # forearm and all descendants together at the elbow.
        shoulder = joint('Scapula', 'Humerus')
        elbow = (joint('Humerus', 'Ulna') + joint('Humerus', 'Radius')) / 2
        lateral = np.array([1.0 if side == 'L' else -1.0, 0., 0.])
        turn(subtree('Humerus'), align(elbow-shoulder, lateral), shoulder)
        elbow = (joint('Humerus', 'Ulna') + joint('Humerus', 'Radius')) / 2
        wrist = joint('Radius', 'HandScaphoid')
        turn(subtree('Radius') | subtree('Ulna'), align(wrist-elbow, lateral), elbow)
        # Align the palm with the forearm at the wrist; the fingers are still
        # attached to their own palm parents and move with this rotation.
        wrist = (joint('Radius', 'HandScaphoid') + joint('Radius', 'HandLuna')) / 2
        hand = {n for n in subtree('Humerus') if n.startswith('Hand')}
        turn(hand, align(bone('HandMeta3').mean(axis=0)-wrist, lateral), wrist)
        # Straighten each phalanx while retaining the whole chain. Metacarpals
        # and the grouped palm remain fixed relative to the wrist.
        for digit in range(1, 6):
            chain = [f'HandProxP{digit}']
            if digit != 1: chain.append(f'HandMidP{digit}')
            chain.append(f'HandDistP{digit}')
            metacarpal = f'HandMeta{digit}'
            direction = joint(metacarpal, chain[0]) - bone(metacarpal).mean(axis=0)
            direction /= np.linalg.norm(direction)
            for i, name in enumerate(chain):
                pivot = joint(metacarpal if i == 0 else chain[i-1], name)
                tip = joint(name, chain[i+1]) if i+1 < len(chain) else bone(name).mean(axis=0)
                turn(subtree(name), align(tip-pivot, direction), pivot)
        # Straighten the knees and direct the feet forward, preserving each
        # whole foot and the fitted paired tibia/fibula surface.
        down = np.array([0., -1., 0.])
        hip, knee = joint('Pelvis', 'Femur'), joint('Femur', 'Tibia')
        turn(subtree('Femur'), align(knee-hip, down), hip)
        knee, ankle = joint('Femur', 'Tibia'), joint('Tibia', 'Tal')
        turn(subtree('Tibia'), align(ankle-knee, down), knee)
        ankle = joint('Tibia', 'Tal')
        foot_axis = (bone('FootMeta1').mean(axis=0) + bone('FootMeta5').mean(axis=0))/2 - bone('Cal').mean(axis=0)
        turn(subtree('Tal'), align(foot_axis, np.array([0., 0., 1.])), ankle)
    # A single global translation keeps both feet above the floor; it does not
    # alter any individual bone or joint after the hierarchy has been posed.
    floor = min(posed(name)[:,1].min() for name in bones)
    for name in bones: offset[name][1] -= floor
    # Do not allow a newly introduced large gap between initially touching
    # anatomical neighbours. Existing source registration is not guaranteed
    # to be gap-free, so this deliberately does not invent corrections for it.
    for first, second, kind in graph.anatomy():
        if kind != 'joint': continue
        before = cKDTree(bones[second]).query(bones[first])[0].min()
        after = cKDTree(posed(second)).query(posed(first))[0].min()
        assert before >= 0.005 or after <= max(0.01, before + 0.008), (sex, first, second, before, after)
    destination = BASE / f'skeleton-{sex}-tpose.obj'
    name = None
    with source.open() as src, destination.open('w') as out:
        out.write('# Static T-pose, rigid transformations from the corresponding source OBJ.\n')
        for row in src:
            if row.startswith('#'): continue
            if row.startswith('g '): name = row[2:].strip()
            if row.startswith('v '):
                point = np.array([float(v) for v in row.split()[1:4]]) @ rotation[name].T + offset[name]
                row = 'v ' + ' '.join(f'{x:.6f}' for x in point) + '\n'
            out.write(row)
    transforms = {n: {'parent': parents[n], 'rotation': rotation[n].tolist(), 'offset': offset[n].tolist()}
                  for n in sorted(bones)}
    (BASE / f'skeleton-{sex}-tpose.json').write_text(json.dumps(transforms, indent=1) + '\n')
    print(f'{sex}: {len(bones)} rigid source structures, T-pose written')


if __name__ == '__main__':
    for sex in ('male', 'female'): build(sex)
