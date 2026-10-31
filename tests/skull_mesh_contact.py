#!/usr/bin/env python3
"""Optional FCL audit: compare cranial and pelvic source and blue surfaces.

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
    print('Cranial and pelvic surface audit: skipped (NumPy and python-fcl optional)')
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
    for height in (None, 50):
        folder = temp/('default' if height is None else str(height))
        folder.mkdir()
        args = [str(exe), str(folder)]
        if height is not None: args.append(str(height))
        subprocess.run(args, cwd=root, check=True)
        for sex in ('male', 'female'):
            groups = {color: {name: mesh(folder/f'{sex}_{color}_{name}.obj')
                              for name in ('Head', 'Jaw', 'C1', 'C2', 'C7', 'Pelvis_L', 'Pelvis_R',
                                           'Sacrum', 'Femur_L', 'Femur_R', 'L5')}
                      for color in ('white', 'blue')}
            scale = np.ptp(groups['blue']['Head'][0][:, 1])/np.ptp(groups['white']['Head'][0][:, 1])
            for other in ('Jaw', 'C1'):
                white_n, white_gap = contact(groups['white']['Head'][1], groups['white'][other][1])
                blue_n, blue_gap = contact(groups['blue']['Head'][1], groups['blue'][other][1])
                assert bool(white_n) == bool(blue_n), (height, sex, other, white_n, blue_n)
                if not white_n:
                    assert abs(blue_gap-white_gap*scale) < 1e-7, (height, sex, other, white_gap, blue_gap)
                print(f'{sex} {height or "default"} Head/{other}: source {white_n}, blue {blue_n}; '
                      f'gaps {white_gap*1000:.3f}/{blue_gap*1000:.3f} mm')
            for first, second in (('Jaw', 'C2'), ('Jaw', 'C7'), ('Head', 'C7')):
                white_n, _ = contact(groups['white'][first][1], groups['white'][second][1])
                blue_n, blue_gap = contact(groups['blue'][first][1], groups['blue'][second][1])
                assert not white_n and not blue_n, (height, sex, first, second, white_n, blue_n)
                print(f'{sex} {height or "default"} {first}/{second}: blue gap {blue_gap*1000:.3f} mm')
            white_n, _ = contact(groups['white']['C1'][1], groups['white']['C2'][1])
            blue_n, _ = contact(groups['blue']['C1'][1], groups['blue']['C2'][1])
            assert white_n and blue_n, (height, sex, 'C1/C2', white_n, blue_n)
            # Compare the actual registered surfaces around the pelvic ring
            # and hips. Source intersections are recorded, not claimed fixed.
            white_ring = np.concatenate([groups['white'][n][0] for n in ('Pelvis_L', 'Pelvis_R')])
            blue_ring = np.concatenate([groups['blue'][n][0] for n in ('Pelvis_L', 'Pelvis_R')])
            pelvic_scale = np.ptp(blue_ring[:, 0])/np.ptp(white_ring[:, 0])
            for first, second in (('Pelvis_L', 'Pelvis_R'), ('Pelvis_L', 'Sacrum'),
                                  ('Pelvis_R', 'Sacrum'), ('Pelvis_L', 'Femur_L'),
                                  ('Pelvis_R', 'Femur_R'), ('Sacrum', 'L5')):
                white_n, white_gap = contact(groups['white'][first][1], groups['white'][second][1])
                blue_n, blue_gap = contact(groups['blue'][first][1], groups['blue'][second][1])
                assert bool(white_n) == bool(blue_n), (height, sex, first, second, white_n, blue_n)
                if not white_n:
                    assert blue_gap < max(0.003, 2*white_gap), (height, sex, first, second, blue_gap)
                    if first.startswith('Pelvis_') and second in ('Pelvis_R', 'Sacrum'):
                        assert abs(blue_gap-white_gap*pelvic_scale) < 1e-7, (height, sex, first, second)
                print(f'{sex} {height or "default"} {first}/{second}: source {white_n}, blue {blue_n}; '
                      f'gaps {white_gap*1000:.3f}/{blue_gap*1000:.3f} mm')
print('Cranial and pelvic triangle-surface comparison: PASS (existing source overlaps remain)')
