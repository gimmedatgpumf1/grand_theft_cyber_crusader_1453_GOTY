#!/usr/bin/env python3
"""FCL audit of every adjacent reconstructed vertebral surface.

The source and prior blue skeleton already contained overlaps. This checks
that pairs known to be clear in the previous blue mesh remain clear at the
selected heights, and measures all adjacent gaps; it is not a clinical test.
"""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

if not importlib.util.find_spec('fcl') or not importlib.util.find_spec('numpy'):
    print('Vertebral contact audit: skipped (NumPy and python-fcl optional)')
    raise SystemExit(0)
import fcl
import numpy as np

ROOT=Path(__file__).resolve().parent.parent
NAMES=[f'{part}{i}' for part,count in (('C',7),('T',12),('L',5)) for i in range(1,count+1)]
# Measured against the prior blue comparison mesh at 50, 170 and 250 cm.
# Pairs not in these lists already had crossings and are reported, not hidden.
CLEAR={
    'male': {'T5-T6','T6-T7','T7-T8','T8-T9','T9-T10','T10-T11','T11-T12','T12-L1','L1-L2'},
    'female': {'C5-C6','T1-T2','T5-T6','T6-T7','T7-T8','T8-T9','T9-T10','T10-T11',
               'T11-T12','L1-L2','L2-L3','L3-L4','L4-L5'},
}

def load(path):
    vertices,faces=[],[]
    for line in path.read_text().splitlines():
        row=line.split()
        if row and row[0]=='v': vertices.append([float(x) for x in row[1:4]])
        elif row and row[0]=='f': faces.append([int(x)-1 for x in row[1:4]])
    vertices=np.array(vertices,dtype=np.float64); faces=np.array(faces,dtype=np.int32)
    assert len(vertices) and len(faces)
    model=fcl.BVHModel(); model.beginModel(len(vertices),len(faces)); model.addSubModel(vertices,faces); model.endModel()
    return fcl.CollisionObject(model),faces

def contact(a,b):
    a,b=a[0],b[0]
    result=fcl.CollisionResult()
    count=fcl.collide(a,b,fcl.CollisionRequest(num_max_contacts=1000),result)
    return count, 0.0 if count else fcl.distance(a,b,fcl.DistanceRequest(),fcl.DistanceResult())

def self_crossings(mesh):
    obj,faces=mesh; result=fcl.CollisionResult()
    count=fcl.collide(obj,obj,fcl.CollisionRequest(num_max_contacts=800000,enable_contact=True),result)
    assert count<800000
    pairs={(min(c.b1,c.b2),max(c.b1,c.b2)) for c in result.contacts
           if c.b1>=0 and c.b2>=0 and c.b1!=c.b2}
    return sum(not set(faces[a]) & set(faces[b]) for a,b in pairs)

with tempfile.TemporaryDirectory(prefix='gtc-vertebra-') as temp:
    temp=Path(temp); exe=temp/'export'
    command=shlex.split(os.environ.get('CXX','g++'))+[
        '-std=c++20','-O2','-pthread','-Isrc','-Inative','-Inative/vendor/bullet/src',
        'tests/skeleton_cranial_export.cpp',
        *[f'native/client/{name}.cpp' for name in ('skeleton_room','skeleton_body','skeleton_ragdoll','raster','hud','ui_layer')],
        'build/obj/bullet/libbullet.a','-o',str(exe)]
    subprocess.run(command,cwd=ROOT,check=True)
    for height in (50,170,250):
        folder=temp/str(height); folder.mkdir()
        subprocess.run([str(exe),str(folder),'--all',str(height)],cwd=ROOT,check=True)
        for sex in ('male','female'):
            objects={name:load(folder/f'{sex}_blue_{name}.obj') for name in NAMES}
            for name in NAMES:
                white=load(folder/f'{sex}_white_{name}.obj')
                assert self_crossings(objects[name])<=self_crossings(white),(sex,height,name,'self crossing')
            maximum=0; overlaps=[]
            for first,second in zip(NAMES,NAMES[1:]):
                count,gap=contact(objects[first],objects[second]); pair=f'{first}-{second}'
                if pair in CLEAR[sex]: assert not count,(sex,height,pair,count)
                if count: overlaps.append(pair)
                maximum=max(maximum,gap)
            assert maximum<.0022*max(1,height/170),(sex,height,maximum)
            print(f'{sex} {height} cm: 23 adjacent pairs; max gap {maximum*1000:.3f} mm; '
                  f'overlaps in {len(overlaps)} previously overlapping pairs')
    # Vary each dimension that changes the torso/neck field. The profile API
    # clips these requests to the real slider bounds; no sliders are blocked.
    for dimension in (1,2,3,5,6,7):
        for multiplier in (.5,1.5):
            folder=temp/f'170-{dimension}-{multiplier}'; folder.mkdir()
            subprocess.run([str(exe),str(folder),'--all','170',str(dimension),str(multiplier)],cwd=ROOT,check=True)
            for sex in ('male','female'):
                objects={name:load(folder/f'{sex}_blue_{name}.obj') for name in NAMES}
                for first,second in zip(NAMES,NAMES[1:]):
                    if f'{first}-{second}' in CLEAR[sex]:
                        count,_=contact(objects[first],objects[second])
                        assert not count,(sex,dimension,multiplier,first,second,count)
    print('Vertebral slider-endpoint joint sweep: PASS (selected previously-clear pairs)')
print('Vertebral adjacent-surface audit: PASS (previous overlaps remain)')
