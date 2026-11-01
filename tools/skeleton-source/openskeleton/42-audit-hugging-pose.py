#!/usr/bin/env python3
"""Inspect registered-source versus actual T-pose arm directions, not intent."""
import csv
import importlib.util
import tempfile
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('tpose',HERE/'38-measure-t-pose-arm.py')
tpose=importlib.util.module_from_spec(spec);spec.loader.exec_module(tpose)


def calculate(poses):
    rows=[]
    for sex in ('male','female'):
        mesh=tpose.whole.mesh(sex); shoulders=tpose.whole.joints(sex,'shoulders'); elbows=tpose.whole.joints(sex,'elbows')
        for state,pose in poses.items():
            def pos(name,point): return tpose.transform(np.asarray([point]),pose[sex,name])[0]
            sternum=pos('Sternum',mesh['Sternum'].mean(0))
            for side,sign in [('L',1),('R',-1)]:
                p=tpose.whole.arm_points(mesh,shoulders[side],elbows[side],side)
                shoulder=pos('Humerus_'+side,p[0]);elbow=pos('Humerus_'+side,p[1])
                wrist=pos('Radius_'+side,p[2]);tip=pos('HandDistP3_'+side,p[-1])
                upper=elbow-shoulder;fore=wrist-elbow
                angle=np.degrees(np.arccos(np.clip(np.dot(upper,fore)/(np.linalg.norm(upper)*np.linalg.norm(fore)),-1,1)))
                rows.append(dict(sex=sex,pose=state,side=side,elbow_shaft_deflection_deg=angle,
                    forearm_inward_x_cm=-sign*(wrist[0]-elbow[0])*100,
                    wrist_ahead_of_shoulder_z_cm=(wrist[2]-shoulder[2])*100,
                    finger_tip_own_side_from_sternum_x_cm=sign*(tip[0]-sternum[0])*100,
                    humerus_outward_axis_alignment=sign*upper[0]/np.linalg.norm(upper),
                    forearm_outward_axis_alignment=sign*fore[0]/np.linalg.norm(fore)))
    return rows


def main():
    with tempfile.TemporaryDirectory() as d:
        exe=Path(d)/'pose';tpose.compile_exporter(exe)
        poses={'registered_source':tpose.matrices(exe,('--source',)), 'room_tpose':tpose.matrices(exe)}
        rows=calculate(poses)
    path=tpose.whole.OUT/'HUGGING_POSE_AUDIT.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0],delimiter='\t',lineterminator='\n');writer.writeheader()
        writer.writerows({k:f'{v:.6f}' if isinstance(v,(float,np.floating)) else v for k,v in row.items()} for row in rows)
    print(path)
if __name__=='__main__': main()
