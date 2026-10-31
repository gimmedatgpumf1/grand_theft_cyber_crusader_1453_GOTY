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
    return vertices, fcl.CollisionObject(model), faces

def contact(a, b):
    result = fcl.CollisionResult()
    n = fcl.collide(a, b, fcl.CollisionRequest(num_max_contacts=1000, enable_contact=True), result)
    if n:
        return n, 0.0
    return n, fcl.distance(a, b, fcl.DistanceRequest(), fcl.DistanceResult())

def self_crossings(mesh_data):
    _,obj,faces=mesh_data
    result=fcl.CollisionResult()
    count=fcl.collide(obj,obj,fcl.CollisionRequest(num_max_contacts=800000,enable_contact=True),result)
    assert count<800000, 'self-intersection audit contact budget exhausted'
    pairs={(min(c.b1,c.b2),max(c.b1,c.b2)) for c in result.contacts
           if c.b1>=0 and c.b2>=0 and c.b1!=c.b2}
    return [(a,b) for a,b in pairs if not set(faces[a]) & set(faces[b])]

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
    # The pelvis interpolation now changes throughout 100–190 cm. Exercise
    # actual triangle contacts across that range, not only at its endpoints.
    for height in (None, 50, 100, 120, 135, 145, 150, 155, 160, 165, 170, 175, 180, 190, 250):
        folder = temp/('default' if height is None else str(height))
        folder.mkdir()
        args = [str(exe), str(folder)]
        if height is not None: args.append(str(height))
        subprocess.run(args, cwd=root, check=True)
        for sex in ('male', 'female'):
            groups = {color: {name: mesh(folder/f'{sex}_{color}_{name}.obj')
                              for name in ('Head', 'Jaw', 'C1', 'C2', 'C7', 'Pelvis_L', 'Pelvis_R',
                                           'Sacrum', 'Sternum', 'Femur_L', 'Femur_R', 'L5', 'Tibia_L', 'Tibia_R', 'Tal_L', 'Tal_R',
                                           'Clavicula_L', 'Clavicula_R', 'Scapula_L', 'Scapula_R',
                                           'Humerus_L', 'Humerus_R', 'Radius_L', 'Radius_R')}
                      for color in ('white', 'blue')}
            scale = np.ptp(groups['blue']['Head'][0][:, 1])/np.ptp(groups['white']['Head'][0][:, 1])
            if height is None:
                # FCL checks beyond face-normal reversal: disjoint surface
                # triangles in these averaged bones must not cross each other.
                for name in ('Pelvis_L','Femur_L','Tibia_L','Scapula_L','Humerus_L','Sacrum','Sternum'):
                    assert not self_crossings(groups['blue'][name]), (sex,name,'self-intersection')
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
            # The blue ring is now bilateral rather than a uniform copy of
            # both source sides. Inspect its actual triangles instead of
            # asserting that the old source contacts must be reproduced.
            for first, second in (('Pelvis_L', 'Pelvis_R'), ('Pelvis_L', 'Sacrum'),
                                  ('Pelvis_R', 'Sacrum'), ('Pelvis_L', 'Femur_L'),
                                  ('Pelvis_R', 'Femur_R')):
                white_n, white_gap = contact(groups['white'][first][1], groups['white'][second][1])
                blue_n, blue_gap = contact(groups['blue'][first][1], groups['blue'][second][1])
                assert not blue_n and blue_gap < 0.003, (height, sex, first, second, blue_n, blue_gap)
                print(f'{sex} {height or "default"} {first}/{second}: source {white_n}, blue {blue_n}; '
                      f'gaps {white_gap*1000:.3f}/{blue_gap*1000:.3f} mm')
            # Selected joints have explicitly fitted surfaces. Check both
            # sides at each test height; these are not all anatomical joints.
            allowed=max(1.0,(height or (173 if sex=='male' else 166))/160)
            for side in ('L','R'):
                n,gap=contact(groups['blue'][f'Clavicula_{side}'][1],groups['blue']['Sternum'][1])
                assert not n and gap<0.012*allowed, (height,sex,side,'sternoclavicular',n,gap)
                for a,b,limit in (('Femur','Tibia',0.003), ('Tibia','Tal',0.005),
                                  ('Clavicula','Scapula',0.003), ('Scapula','Humerus',0.004),
                                  ('Humerus','Radius',0.010)):
                    first,second=f'{a}_{side}',f'{b}_{side}'
                    n,gap=contact(groups['blue'][first][1],groups['blue'][second][1])
                    assert not n and gap<limit*allowed, (height,sex,first,second,n,gap)
            white_n, _ = contact(groups['white']['Sacrum'][1], groups['white']['L5'][1])
            blue_n, _ = contact(groups['blue']['Sacrum'][1], groups['blue']['L5'][1])
            assert bool(white_n) == bool(blue_n), (height, sex, 'Sacrum/L5', white_n, blue_n)
            print(f'{sex} {height or "default"} Sacrum/L5: source {white_n}, blue {blue_n}')
    # Exercise the endpoints of the actual arm, hip and shoulder tracks on
    # both sexes and several heights. Check surfaces after the real mapping,
    # not just the formula numbers or the source skeleton.
    for height in (50, 170, 250):
        for dimension in (4, 6, 7):  # Arm, Hip, Shoulder
            for multiplier in (0.5, 1.5):
                folder = temp/f'{height}-{dimension}-{multiplier}'
                folder.mkdir()
                subprocess.run([str(exe),str(folder),str(height),str(dimension),str(multiplier)],cwd=root,check=True)
                for sex in ('male','female'):
                    names=('Pelvis_L','Pelvis_R','Sacrum','Sternum','Femur_L','Femur_R','Tibia_L','Tibia_R',
                           'Tal_L','Tal_R','Clavicula_L','Clavicula_R','Scapula_L','Scapula_R',
                           'Humerus_L','Humerus_R','Radius_L','Radius_R')
                    blue={name:mesh(folder/f'{sex}_blue_{name}.obj')[1] for name in names}
                    for side in ('L','R'):
                        for first,second in ((f'Clavicula_{side}','Sternum'),
                                             (f'Pelvis_{side}',f'Femur_{side}'),
                                             (f'Pelvis_{side}','Sacrum'),
                                             (f'Femur_{side}',f'Tibia_{side}'),
                                             (f'Tibia_{side}',f'Tal_{side}'),
                                             (f'Clavicula_{side}',f'Scapula_{side}'),
                                             (f'Scapula_{side}',f'Humerus_{side}'),
                                             (f'Humerus_{side}',f'Radius_{side}')):
                            n,gap=contact(blue[first],blue[second])
                            assert not n and gap<0.015*max(1,height/170), (height,dimension,multiplier,sex,first,second,n,gap)
    print('Bilateral slider joint surface sweep: PASS (selected joints, both sexes)')
    # Audit every paired source-side mesh plus the two reconstructed axial
    # bones for new disjoint triangle self-crossings, not only the pelvis.
    all_folder=temp/'all-bones'; all_folder.mkdir()
    subprocess.run([str(exe),str(all_folder),'--all'],cwd=root,check=True)
    for sex in ('male','female'):
        files=sorted(all_folder.glob(f'{sex}_blue_*.obj'))
        assert len(files)==79, (sex,len(files))
        for path in files:
            source=all_folder/path.name.replace('_blue_','_white_',1)
            old=self_crossings(mesh(source)); new=self_crossings(mesh(path))
            assert len(new)<=len(old), (sex,path.name,'new self-intersection',len(old),len(new))
    print('Bilateral source-surface self-intersection audit: PASS (79 parts per sex)')
print('Cranial and pelvic triangle-surface comparison: PASS (source Sacrum/L5 overlaps remain)')
