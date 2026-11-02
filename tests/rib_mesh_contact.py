#!/usr/bin/env python3
"""Audit neutral rib surfaces, not cartilage or clinical joint fit.

The blue ribs are mirrored in pairs. Require adjacent ribs and selected
costovertebral surfaces to remain separate across tested sizes and slider
extremes. This does not model cartilage or prove clinical joint fit.
"""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

if not importlib.util.find_spec('fcl') or not importlib.util.find_spec('numpy'):
    print('Rib contact audit: skipped (NumPy and python-fcl optional)')
    raise SystemExit(0)
import fcl
import numpy as np

ROOT=Path(__file__).resolve().parent.parent

def load(path):
    vertices=[]; faces=[]
    for line in path.read_text().splitlines():
        row=line.split()
        if row and row[0]=='v': vertices.append([float(x) for x in row[1:4]])
        elif row and row[0]=='f': faces.append([int(x)-1 for x in row[1:4]])
    vertices=np.asarray(vertices,dtype=np.float64); faces=np.asarray(faces,dtype=np.int32)
    assert len(vertices) and len(faces)
    model=fcl.BVHModel(); model.beginModel(len(vertices),len(faces)); model.addSubModel(vertices,faces); model.endModel()
    return fcl.CollisionObject(model),faces

def contact(a,b):
    a,b=a[0],b[0]; result=fcl.CollisionResult()
    count=fcl.collide(a,b,fcl.CollisionRequest(num_max_contacts=1000),result)
    return count,0.0 if count else fcl.distance(a,b,fcl.DistanceRequest(),fcl.DistanceResult())

def self_crossings(mesh):
    obj,faces=mesh; result=fcl.CollisionResult()
    count=fcl.collide(obj,obj,fcl.CollisionRequest(num_max_contacts=800000,enable_contact=True),result)
    assert count<800000
    pairs={(min(c.b1,c.b2),max(c.b1,c.b2)) for c in result.contacts
           if c.b1>=0 and c.b2>=0 and c.b1!=c.b2}
    return sum(not set(faces[a]) & set(faces[b]) for a,b in pairs)

with tempfile.TemporaryDirectory(prefix='gtc-ribs-') as temp:
    temp=Path(temp); exe=temp/'export'
    command=shlex.split(os.environ.get('CXX','g++'))+[
        '-std=c++20','-O2','-pthread','-Isrc','-Inative','-Inative/vendor/bullet/src',
        'tests/skeleton_cranial_export.cpp',
        *[f'native/client/{name}.cpp' for name in ('skeleton_room','skeleton_body','skeleton_ragdoll','raster','hud','ui_layer')],
        'build/obj/bullet/libbullet.a','-o',str(exe)]
    subprocess.run(command,cwd=ROOT,check=True)
    profiles=([(h,None,None) for h in (50,170,250)]+
              [(170,d,m) for d in (1,2,3,5,6,7) for m in (.5,1.5)]+
              [(h,'combined',end) for h in (50,170,250) for end in ('lower','upper')])
    for index,(height,dimension,multiplier) in enumerate(profiles):
        folder=temp/str(index); folder.mkdir(); args=[str(exe),str(folder),'--ribs',str(height)]
        if dimension is not None: args += [str(dimension),str(multiplier)]
        subprocess.run(args,cwd=ROOT,check=True)
        for sex in ('male','female'):
            def mesh(color,name): return load(folder/f'{sex}_{color}_{name}.obj')
            ribs={f'Rib{n}_{side}':mesh('blue',f'Rib{n}_{side}') for n in range(1,13) for side in 'LR'}
            vertebrae={n:mesh('blue',f'T{n}') for n in range(1,13)}
            for side in 'LR':
                for n in range(1,12):
                    a,b=f'Rib{n}_{side}',f'Rib{n+1}_{side}'
                    count,_=contact(ribs[a],ribs[b]); assert not count,(sex,height,dimension,multiplier,a,b,count)
                for n in range(1,13):
                    for t in ([n,n-1] if n>1 else [n]):
                        count,_=contact(ribs[f'Rib{n}_{side}'],vertebrae[t])
                        assert not count,(sex,height,dimension,multiplier,f'Rib{n}_{side}',f'T{t}',count)
            if dimension is None:
                # Both blue ribs use the left template. Report any inherited
                # left self-crossing and its mirrored-copy counts explicitly.
                inherited=[]
                for n in range(1,13):
                    left=f'Rib{n}_L'; baseline=self_crossings(mesh('white',left))
                    actual=[]
                    for side in 'LR':
                        name=f'Rib{n}_{side}'; count=self_crossings(ribs[name]); actual.append(count)
                        assert count<=baseline,(sex,height,name,'self crossings',baseline,count)
                    if baseline: inherited.append((n,baseline,*actual))
                print(f'{sex} {height} cm: adjacent ribs and selected rib/vertebra surfaces clear; '
                      f'self-crossings (rib, source-left, blue-left, blue-right) {inherited}')
    print('Rib surface sweep: PASS (selected surfaces; cartilage not tested)')
