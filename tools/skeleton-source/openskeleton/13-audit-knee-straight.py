"""Audit straight-knee source triangle pairs; this is not clinical validation.

Build build/bin/gtc-skeleton-arm-pose first as shown in README. Requires NumPy
and python-fcl. All faces are converted from MATLAB 1-based indices; this is a
surface test, not containment, cartilage or a full knee range-of-motion test.
"""
import json
from pathlib import Path
import subprocess
import fcl
import numpy as np

ROOT=Path(__file__).resolve().parents[3]
BASE=ROOT/'tools/skeleton-source'
rows=subprocess.check_output([str(ROOT/'build/bin/gtc-skeleton-arm-pose')],cwd=ROOT,text=True)
matrices={}
for row in rows.splitlines():
    fields=row.split()
    if len(fields)!=18: raise ValueError('invalid pose matrix')
    matrices[(fields[0],fields[1])]=np.array(fields[2:],dtype=float).reshape(4,4).T
for sex,filename in (('male','male-N197925.npz'),('female','skelly-out.npz')):
    shift=np.asarray(json.loads((BASE/f'skeleton-{sex}.parts.json').read_text())['global_translation_m'])
    with np.load(BASE/'openskeleton/data'/filename) as source:
        cache={}
        def bone(name,posed):
            key=name,posed
            if key in cache: return cache[key]
            points=source['v_'+name]; points=np.stack((points[:,0],points[:,2],-points[:,1]),axis=1)/1000+shift
            faces=source['f_'+name]
            if faces.min()<1 or faces.max()>len(points): raise ValueError('source indices must be 1-based')
            mesh=fcl.BVHModel(); mesh.beginModel(len(points),len(faces))
            mesh.addSubModel(points,np.asarray(faces,dtype=np.int32)-1); mesh.endModel()
            matrix=matrices[(sex,name)] if posed else np.eye(4)
            result=fcl.CollisionObject(mesh,fcl.Transform(matrix[:3,:3],matrix[:3,3])); cache[key]=result
            return result
        for side in ('L','R'):
            for first,second in (('Femur','Tibia'),('Femur','Patella'),('Patella','Tibia'),('Tibia','Fibula')):
                results=[]
                for posed in (False,True):
                    a,b=bone(first+'_'+side,posed),bone(second+'_'+side,posed)
                    count=fcl.collide(a,b,fcl.CollisionRequest(num_max_contacts=10000,enable_contact=True),fcl.CollisionResult())
                    gap=fcl.distance(a,b,fcl.DistanceRequest(),fcl.DistanceResult())*1000
                    results.append((count,gap))
                print(f'{sex} {side} {first}/{second}: bind {results[0][0]} pairs, gap {results[0][1]:.2f} mm; '
                      f'straight {results[1][0]} pairs, gap {results[1][1]:.2f} mm')
                if results[1][0]: raise RuntimeError('straight-knee surfaces intersect')
