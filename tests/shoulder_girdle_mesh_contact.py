#!/usr/bin/env python3
"""Check paired blue shoulder surfaces at selected sizes and slider limits.

Checks triangle intersections and gaps, not cartilage or anatomical joint fit.
The blue shoulder bones are already mirrored; the white source is unchanged.
"""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

if not importlib.util.find_spec('fcl') or not importlib.util.find_spec('numpy'):
    print('Shoulder girdle surface audit: skipped (NumPy and python-fcl optional)')
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

with tempfile.TemporaryDirectory(prefix='gtc-shoulders-') as temp:
    temp=Path(temp); exe=temp/'export'
    command=shlex.split(os.environ.get('CXX','g++'))+[
        '-std=c++20','-O2','-pthread','-Isrc','-Inative','-Inative/vendor/bullet/src',
        'tests/skeleton_cranial_export.cpp',
        *[f'native/client/{name}.cpp' for name in ('skeleton_room','skeleton_body','skeleton_ragdoll','raster','hud','ui_layer')],
        'build/obj/bullet/libbullet.a','-o',str(exe)]
    subprocess.run(command,cwd=ROOT,check=True)
    profiles=([(h,None,None) for h in (50,170,250)]+
              [(170,d,m) for d in range(1,8) for m in (.5,1.5)]+
              [(h,'combined',end) for h in (50,170,250) for end in ('lower','upper')])
    for index,(height,dimension,multiplier) in enumerate(profiles):
        folder=temp/str(index); folder.mkdir(); args=[str(exe),str(folder),'--shoulders',str(height)]
        if dimension is not None: args += [str(dimension),str(multiplier)]
        subprocess.run(args,cwd=ROOT,check=True)
        for sex in ('male','female'):
            def mesh(color,name): return load(folder/f'{sex}_{color}_{name}.obj')
            sternum=mesh('blue','Sternum'); maximum=[0.,0.,0.]
            for side in 'LR':
                scapula=mesh('blue',f'Scapula_{side}'); clavicle=mesh('blue',f'Clavicula_{side}')
                humerus=mesh('blue',f'Humerus_{side}')
                for i,(a,b) in enumerate(((clavicle,sternum),(clavicle,scapula),(scapula,humerus))):
                    count,gap=contact(a,b); assert not count,(sex,height,dimension,multiplier,side,i,count)
                    maximum[i]=max(maximum[i],gap)
                    # Existing selected-joint tolerance, not a clinical threshold.
                    assert gap<.015*max(1,height/170),(sex,height,dimension,multiplier,side,i,gap)
                for n in range(1,13):
                    rib=mesh('blue',f'Rib{n}_{side}')
                    for name,bone in (('Scapula',scapula),('Clavicula',clavicle)):
                        count,_=contact(bone,rib)
                        assert not count,(sex,height,dimension,multiplier,name,side,n,count)
            if dimension is None:
                # Both blue sides use the same left-side template.
                for name in ('Scapula','Clavicula'):
                    baseline=self_crossings(mesh('white',f'{name}_L'))
                    for side in 'LR':
                        count=self_crossings(mesh('blue',f'{name}_{side}'))
                        assert count<=baseline,(sex,height,name,side,baseline,count)
                print(f'{sex} {height} cm: blue max joint gaps clavicle/sternum, clavicle/scapula, '
                      f'scapula/humerus: {", ".join(f"{x*1000:.3f}" for x in maximum)} mm; '
                      'both sides clear of same-side rib surfaces')
    print('Shoulder girdle surface sweep: PASS (selected surfaces, not anatomical joint validation)')
