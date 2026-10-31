#!/usr/bin/env python3
"""Compute fingertip span minus both full arm lengths in the real room T-pose.

The result measures the gap between the shoulder-side humeral endpoints used
for the arms, not anatomical biacromial shoulder breadth. See the report.
"""
import argparse
import csv
import importlib.util
import re
import tempfile
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('tpose',HERE/'38-measure-t-pose-arm.py')
tpose=importlib.util.module_from_spec(spec);spec.loader.exec_module(tpose)


def measure(sex,matrices):
    source=tpose.whole.mesh(sex)
    arms=tpose.measure(sex,matrices)
    endpoints={};names={};starts={}
    for side,sign in [('L',1),('R',-1)]:
        best=None
        # Select the most lateral vertex across all finger phalanx meshes,
        # not an extremum over the whole hand or entire skeleton.
        for name,vertices in source.items():
            if not re.fullmatch(r'Hand(?:ProxP|MidP|DistP)[1-5]_'+side,name): continue
            posed=tpose.transform(vertices,matrices[(sex,name)])
            point=posed[np.argmax(sign*posed[:,0])]
            if best is None or sign*point[0]>sign*best[0][0]: best=(point,name)
        if best is None: raise ValueError('missing finger geometry')
        endpoints[side],names[side]=best
        starts[side]=arms[side]['humerus_end']
    tip_l,tip_r=endpoints['L'],endpoints['R']
    start_l,start_r=starts['L'],starts['R']
    span=float(np.linalg.norm(tip_l-tip_r)*100)
    left=float(np.linalg.norm(tip_l-start_l)*100)
    right=float(np.linalg.norm(tip_r-start_r)*100)
    return dict(left_tip=tip_l,right_tip=tip_r,left_bone=names['L'],right_bone=names['R'],
                left_start=start_l,right_start=start_r,span=span,left=left,right=right,
                gap=span-left-right,span_x=float(abs(tip_l[0]-tip_r[0])*100),
                start_gap_3d=float(np.linalg.norm(start_l-start_r)*100),
                start_gap_x=float(abs(start_l[0]-start_r[0])*100))


def generate(executable):
    matrices=tpose.matrices(executable); rows=[]
    for sex,subject in [('male','N197925'),('female','N186416')]:
        r=measure(sex,matrices)
        row=dict(subject=subject,sex=sex,left_finger_bone=r['left_bone'],right_finger_bone=r['right_bone'])
        for label in ('left_tip','right_tip','left_start','right_start'):
            for axis,value in zip('xyz',r[label]):row[f'{label}_{axis}_m']=f'{value:.8f}'
        for label in ('span','left','right','gap','span_x','start_gap_3d','start_gap_x'):
            row[label+'_cm']=f'{r[label]:.6f}'
        rows.append(row)
    path=tpose.whole.OUT/'TPOSE_FINGERTIP_SPAN_GAP.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0],delimiter='\t',lineterminator='\n')
        writer.writeheader();writer.writerows(rows)
    print('wrote',path)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--pose-cli',type=Path)
    args=parser.parse_args()
    if args.pose_cli: generate(args.pose_cli.resolve())
    else:
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/'export-t-pose';tpose.compile_exporter(exe);generate(exe)

if __name__=='__main__':main()
