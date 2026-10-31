#!/usr/bin/env python3
"""Optional FCL audit: compare actual skull/jaw/C1 source and blue surfaces.

Triangle intersections are not clinical diagnoses or a containment test. The
pre-existing source contacts are reported, not silently treated as fixed.
"""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

if not importlib.util.find_spec('fcl') or not importlib.util.find_spec('numpy'):
    print('Cranial source-surface audit: skipped (NumPy and python-fcl optional)')
    raise SystemExit(0)
import fcl
import numpy as np

root = Path(__file__).resolve().parent.parent

def mesh(path):
    vertices, faces = [], []
    with path.open() as file:
        for line in file:
            fields = line.split()
            if not fields:
                continue
            if fields[0] == 'v': vertices.append([float(x) for x in fields[1:4]])
            if fields[0] == 'f': faces.append([int(x)-1 for x in fields[1:4]])
    vertices = np.asarray(vertices, dtype=np.float64)
    faces = np.asarray(faces, dtype=np.int32)
    assert len(vertices) and len(faces) and faces.min() >= 0 and faces.max() < len(vertices)
    model = fcl.BVHModel()
    model.beginModel(len(vertices), len(faces))
    model.addSubModel(vertices, faces)
    model.endModel()
    return vertices, fcl.CollisionObject(model)

def contact(a, b):
    result = fcl.CollisionResult()
    n = fcl.collide(a, b, fcl.CollisionRequest(num_max_contacts=1000, enable_contact=True), result)
    if n:
        return n, 0.0
    return n, fcl.distance(a, b, fcl.DistanceRequest(), fcl.DistanceResult())

with tempfile.TemporaryDirectory(prefix='gtc-cranial-') as temp:
    temp = Path(temp)
    exe = temp/'export'
    command = shlex.split(os.environ.get('CXX', 'g++')) + [
        '-std=c++17', '-O2', '-pthread', '-Isrc', '-Inative', '-Inative/vendor/bullet/src',
        'tests/skeleton_cranial_export.cpp', 'native/client/skeleton_room.cpp',
        'native/client/skeleton_body.cpp', 'native/client/skeleton_ragdoll.cpp',
        'native/client/raster.cpp', 'native/client/hud.cpp', 'native/client/ui_layer.cpp',
        'build/obj/bullet/libbullet.a', '-o', str(exe)]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(exe), str(temp)], cwd=root, check=True)
    for sex in ('male', 'female'):
        groups = {color: {name: mesh(temp/f'{sex}_{color}_{name}.obj')
                          for name in ('Head', 'Jaw', 'C1')}
                  for color in ('white', 'blue')}
        scale = np.ptp(groups['blue']['Head'][0][:, 1])/np.ptp(groups['white']['Head'][0][:, 1])
        for other in ('Jaw', 'C1'):
            white_n, white_gap = contact(groups['white']['Head'][1], groups['white'][other][1])
            blue_n, blue_gap = contact(groups['blue']['Head'][1], groups['blue'][other][1])
            assert bool(white_n) == bool(blue_n), (sex, other, white_n, blue_n)
            if not white_n:
                assert abs(blue_gap-white_gap*scale) < 1e-7, (sex, other, white_gap, blue_gap)
            print(f'{sex} Head/{other}: source {white_n} contacts, blue {blue_n} contacts; '
                  f'gaps {white_gap*1000:.3f}/{blue_gap*1000:.3f} mm')
print('Cranial triangle-surface comparison: PASS (existing source overlaps remain)')
