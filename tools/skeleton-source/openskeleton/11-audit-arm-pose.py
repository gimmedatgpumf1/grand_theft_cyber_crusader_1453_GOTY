"""Audit source triangle surfaces in the candidate arm-out pose.

Requires NumPy and python-fcl plus build/bin/gtc-skeleton-arm-pose; build it as
shown in openskeleton/README.md. MATLAB faces are 1-based. This tests surface
intersection only, not containment, cartilage, or medical validity. The arm-out
pose is an approximate reference, not medical validation. Contact counts are
capped at 10,000 triangle pairs.
"""
import json
from pathlib import Path
import subprocess
import fcl
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'tools/skeleton-source'
rows = subprocess.check_output([str(ROOT/'build/bin/gtc-skeleton-arm-pose')], cwd=ROOT, text=True)
matrices = {}
for row in rows.splitlines():
    fields = row.split()
    if len(fields) != 18: raise ValueError('bad matrix output')
    matrices[(fields[0], fields[1])] = np.array(fields[2:], dtype=float).reshape(4,4).T

for sex, filename in (('male','male-N197925.npz'),('female','skelly-out.npz')):
    shift = np.asarray(json.loads((BASE/f'skeleton-{sex}.parts.json').read_text())['global_translation_m'])
    with np.load(BASE/'openskeleton/data'/filename) as source:
        cache = {}
        def bone(name, posed):
            key = name,posed
            if key in cache: return cache[key]
            vertices = source['v_'+name]
            vertices = np.stack((vertices[:,0],vertices[:,2],-vertices[:,1]),axis=1)/1000+shift
            faces = source['f_'+name]
            if faces.min()<1 or faces.max()>len(vertices): raise ValueError('faces must be 1-based')
            mesh = fcl.BVHModel(); mesh.beginModel(len(vertices),len(faces))
            mesh.addSubModel(vertices, np.asarray(faces,dtype=np.int32)-1); mesh.endModel()
            matrix = matrices[(sex,name)] if posed else np.eye(4)
            result = fcl.CollisionObject(mesh,fcl.Transform(matrix[:3,:3],matrix[:3,3]))
            cache[key] = result
            return result
        for side in ('L','R'):
            for first,second in (('Scapula','Humerus'),('Clavicula','Humerus'),
                                 ('Humerus','Ulna'),('Humerus','Radius'),('Ulna','Radius')):
                values = []
                for posed in (False,True):
                    a,b=bone(first+'_'+side,posed),bone(second+'_'+side,posed)
                    count=fcl.collide(a,b,fcl.CollisionRequest(num_max_contacts=10000,enable_contact=True),fcl.CollisionResult())
                    gap=fcl.distance(a,b,fcl.DistanceRequest(),fcl.DistanceResult())*1000
                    values.append(f'{count} pairs, gap {gap:.2f} mm')
                print(f'{sex} {side} {first}/{second}: bind {values[0]}; candidate {values[1]}')
