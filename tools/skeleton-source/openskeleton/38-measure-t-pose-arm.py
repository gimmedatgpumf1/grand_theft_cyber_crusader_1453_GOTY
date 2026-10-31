#!/usr/bin/env python3
"""Measure humerus-to-middle-finger-bone-tip in the actual room T-pose.

Uses SkeletonRoom::restoreReferencePose() matrices, not a surrogate pose. Also
records an unvalidated scapular shoulder proxy; the finger endpoint is bone,
not skin. Neither endpoint matches ANSUR. See T_POSE_ARM_MEASUREMENTS.md.
"""
import argparse
import csv
import importlib.util
import subprocess
import tempfile
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent

def module(name,filename):
    spec=importlib.util.spec_from_file_location(name,HERE/filename)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    return m

whole=module('whole','31-measure-whole-body.py')
audit=module('arm_audit','37-audit-arm.py')
REPO=whole.REPO


def compile_exporter(output):
    subprocess.run(['bash','native/vendor/bullet/build.sh'],cwd=REPO,check=True)
    sources=['tools/skeleton-source/openskeleton/38-export-t-pose.cpp',
             'native/client/skeleton_room.cpp','native/client/skeleton_body.cpp',
             'native/client/skeleton_ragdoll.cpp','native/client/raster.cpp',
             'native/client/hud.cpp','native/client/ui_layer.cpp','build/obj/bullet/libbullet.a']
    subprocess.run(['c++','-std=c++17','-O2','-pthread','-Isrc','-Inative',
                    '-Inative/vendor/bullet/src',*sources,'-o',str(output)],cwd=REPO,check=True)


def matrices(executable):
    text=subprocess.check_output([str(executable)],cwd=REPO,text=True)
    result={}
    for line in text.splitlines():
        t=line.split('\t')
        if len(t)!=18 or (t[0],t[1]) in result: raise ValueError('bad pose matrix row')
        matrix=np.asarray([float(v) for v in t[2:]]).reshape((4,4),order='F')
        if not np.isfinite(matrix).all(): raise ValueError('non-finite pose matrix')
        result[(t[0],t[1])]=matrix
    if len(result)!=364: raise ValueError('not both complete 182-bone poses')
    return result


def transform(points,m):
    return np.asarray(points)@m[:3,:3].T+m[:3,3]


def measure(sex,pose):
    mesh=whole.mesh(sex); shoulders=whole.joints(sex,'shoulders'); elbows=whole.joints(sex,'elbows')
    result={}
    for side,sign in [('L',1),('R',-1)]:
        def positioned(name,p): return transform(p,pose[(sex,f'{name}_{side}')])
        scap=positioned('Scapula',mesh[f'Scapula_{side}'])
        # This local extrema heuristic is NOT an identified anatomical acromion.
        # Perform it on the actual posed mesh rather than its prior source pose.
        upper=scap[scap[:,1]>=np.quantile(scap[:,1],.75)]
        shoulder=upper[np.argmax(sign*upper[:,0])]
        mid=positioned('HandMidP3',mesh[f'HandMidP3_{side}'])
        distal=positioned('HandDistP3',mesh[f'HandDistP3_{side}'])
        finger_direction=distal.mean(0)-mid.mean(0)
        finger_direction/=np.linalg.norm(finger_direction)
        fingertip=distal[np.argmax(distal@finger_direction)]
        head=positioned('Humerus',shoulders[side])
        humerus=positioned('Humerus',mesh[f'Humerus_{side}'])
        # On this horizontal T-pose, the medial extreme is at the shoulder
        # end of the humeral head, not the distal elbow end. This is a geometric
        # bone-surface endpoint, not a skin acromion.
        humerus_end=humerus[np.argmin(sign*humerus[:,0])]
        elbow=positioned('Humerus',elbows[side])
        source_path=whole.arm_points(mesh,shoulders[side],elbows[side],side)
        wrist=positioned('Radius',source_path[2])
        def distance(a,b):return float(np.linalg.norm(b-a)*100)
        delta=fingertip-shoulder
        upper_dir=(elbow-head)/np.linalg.norm(elbow-head)
        fore_dir=(wrist-elbow)/np.linalg.norm(wrist-elbow)
        if sign*upper_dir[0]<.999 or sign*fore_dir[0]<.99 or sign*finger_direction[0]<.9:
            raise ValueError(f'{sex} {side} is not an approximately extended lateral arm')
        result[side]={'shoulder':shoulder,'tip':fingertip,'head':head,'humerus_end':humerus_end,
                      'elbow':elbow,'wrist':wrist,
                      'straight':distance(shoulder,fingertip),'horizontal':float(sign*delta[0]*100),
                      'head_to_tip':distance(head,fingertip),'humerus_end_to_tip':distance(humerus_end,fingertip),
                      'upper_alignment':float(sign*upper_dir[0]),'fore_alignment':float(sign*fore_dir[0]),
                      'finger_alignment':float(sign*finger_direction[0])}
    return result


def generate(executable):
    pose=matrices(executable);rows=[]
    for sex,subject,height in [('male','N197925',170.0),('female','N186416',170.2)]:
        result=measure(sex,pose); fit,n=audit.fits(sex,height);ref=fit['outseam_plus_hand']
        for side in ('L','R','mean'):
            if side=='mean':
                r={k:(result['L'][k]+result['R'][k])/2 for k in ('straight','horizontal','head_to_tip',
                        'humerus_end_to_tip','upper_alignment','fore_alignment','finger_alignment')}
            else:r=result[side]
            def coords(key):
                return [f'{x:.8f}' for x in r[key]] if side!='mean' else ['NA']*3
            sx,sy,sz=coords('shoulder'); tx,ty,tz=coords('tip'); hx,hy,hz=coords('humerus_end')
            rows.append(dict(subject=subject,sex=sex,height_cm=f'{height:.1f}',side=side,
                shoulder_proxy_x_m=sx,shoulder_proxy_y_m=sy,shoulder_proxy_z_m=sz,
                humerus_proximal_x_m=hx,humerus_proximal_y_m=hy,humerus_proximal_z_m=hz,
                middle_bone_tip_x_m=tx,middle_bone_tip_y_m=ty,middle_bone_tip_z_m=tz,
                shoulder_proxy_to_fingertip_3d_cm=f'{r["straight"]:.6f}',
                shoulder_proxy_to_fingertip_horizontal_cm=f'{r["horizontal"]:.6f}',
                joint_center_to_tip_3d_cm=f'{r["head_to_tip"]:.6f}',
                proximal_humerus_to_tip_3d_cm=f'{r["humerus_end_to_tip"]:.6f}',
                upper_axis_alignment=f'{r["upper_alignment"]:.8f}',forearm_axis_alignment=f'{r["fore_alignment"]:.8f}',
                finger_axis_alignment=f'{r["finger_alignment"]:.8f}',
                ansur_height_prediction_cm=f'{ref:.6f}',
                raw_humerus_vs_surface_difference_cm=f'{r["humerus_end_to_tip"]-ref:.6f}',
                raw_humerus_vs_surface_difference_percent=f'{100*(r["humerus_end_to_tip"]/ref-1):.4f}',ansur_n=n))
    path=whole.OUT/'T_POSE_ARM_MEASUREMENTS.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0].keys(),delimiter='\t',lineterminator='\n')
        writer.writeheader();writer.writerows(rows)
    print('wrote',path)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--pose-cli',type=Path)
    args=parser.parse_args()
    if args.pose_cli: generate(args.pose_cli.resolve())
    else:
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/'export-t-pose';compile_exporter(exe);generate(exe)

if __name__=='__main__':main()
