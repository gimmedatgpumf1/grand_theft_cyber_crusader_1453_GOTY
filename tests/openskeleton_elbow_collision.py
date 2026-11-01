"""Optional FCL source-surface audit of the room's elbow reference frames.

The sample checks triangle intersection, not full containment, cartilage, or
individual clinical ROM. It reports intersections and gaps without treating
surface clearance as a constraint on the room's reference angles.
"""
import importlib.util
import json
import subprocess
from pathlib import Path

if not importlib.util.find_spec('fcl') or not importlib.util.find_spec('numpy'):
    print('Elbow collision audit: skipped (NumPy and python-fcl optional)')
    raise SystemExit(0)
import numpy as np
import fcl

ROOT=Path(__file__).resolve().parent.parent
BASE=ROOT/'tools/skeleton-source'
rows=subprocess.check_output([str(ROOT/'build/bin/gtc-skeleton-elbow-sweep')],cwd=ROOT,text=True)
matrices={}
for row in rows.splitlines():
    f=row.split()
    assert len(f)==20
    matrices[(f[0],f[1],int(f[2]),f[3])]=np.array(f[4:],dtype=float).reshape(4,4).T

def spans(values):
    result=[]
    for angle in values:
        if result and angle==result[-1][1]+1: result[-1]=(result[-1][0],angle)
        else: result.append((angle,angle))
    return result

for sex,filename in (('male','male-N197925.npz'),('female','skelly-out.npz')):
    shift=np.asarray(json.loads((BASE/f'skeleton-{sex}.parts.json').read_text())['global_translation_m'])
    with np.load(BASE/'openskeleton/data'/filename) as source:
        for side in ('L','R'):
            objects={}
            for name in ('Humerus','Ulna','Radius'):
                key=name+'_'+side; points=source['v_'+key]
                points=np.stack((points[:,0],points[:,2],-points[:,1]),axis=1)/1000+shift
                faces=source['f_'+key]; assert faces.min()>=1 and faces.max()<=len(points)
                mesh=fcl.BVHModel(); mesh.beginModel(len(points),len(faces))
                mesh.addSubModel(points,np.asarray(faces,dtype=np.int32)-1); mesh.endModel()
                objects[name]=fcl.CollisionObject(mesh)
            bad={'Ulna':[],'Radius':[]}; straight_gaps={}
            for angle in range(151):
                for name,obj in objects.items():
                    m=matrices[(sex,side,angle,name+'_'+side)]
                    obj.setRotation(m[:3,:3]); obj.setTranslation(m[:3,3])
                for name in ('Ulna','Radius'):
                    h,o=objects['Humerus'],objects[name]
                    count=fcl.collide(h,o,fcl.CollisionRequest(num_max_contacts=1),fcl.CollisionResult())
                    if count: bad[name].append(angle)
                    if angle==0:
                        straight_gaps[name]=None if count else 1000*fcl.distance(h,o,fcl.DistanceRequest(),fcl.DistanceResult())
            def gap(name):
                return 'intersection' if straight_gaps[name] is None else f"{straight_gaps[name]:.2f} mm"
            print(f"{sex} {side} elbow: 0° ulna {gap('Ulna')}, "
                  f"radius {gap('Radius')}; intersection angle spans "
                  f"ulna {spans(bad['Ulna'])}, radius {spans(bad['Radius'])}")
print('Elbow source-surface audit: informational only')
