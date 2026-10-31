#!/usr/bin/env python3
"""Measure T-pose X extrema and 3D point-to-point span of both scapulae.

This is the actual game mesh's geometric width, not a verified biacromial
measurement or a clinical comparison. See TPOSE_SCAPULA_EXTREMA.md.
"""
import argparse
import csv
import importlib.util
import subprocess
import tempfile
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent

def load(name,filename):
    spec=importlib.util.spec_from_file_location(name,HERE/filename)
    obj=importlib.util.module_from_spec(spec);spec.loader.exec_module(obj)
    return obj

tpose=load('tpose','38-measure-t-pose-arm.py')
seven=load('seven','35-seven-dimensions.py')


def measure(sex,matrices):
    d=tpose.whole.mesh(sex)
    extremes=[]
    for side in ('L','R'):
        name=f'Scapula_{side}'
        pos=tpose.transform(d[name],matrices[(sex,name)])
        if len(pos)==0 or not np.isfinite(pos).all():raise ValueError('invalid scapula geometry')
        extremes.extend([(pos[np.argmin(pos[:,0])],name),(pos[np.argmax(pos[:,0])],name)])
    left=min(extremes,key=lambda item:item[0][0])
    right=max(extremes,key=lambda item:item[0][0])
    delta=right[0]-left[0]
    return {'minimum':left[0],'minimum_bone':left[1],'maximum':right[0],'maximum_bone':right[1],
            'width_x':float(delta[0]*100),'distance_3d':float(np.linalg.norm(delta)*100)}


def compile_formula(exe):
    subprocess.run(['c++','-std=c++17','-O2','-I',str(tpose.REPO/'native'),
                    str(HERE/'35-seven-dimensions.cpp'),'-o',str(exe)],cwd=tpose.REPO,check=True)


def generate(pose_exe,formula_exe):
    matrices=tpose.matrices(pose_exe); stats=seven.stats(); rows=[]
    for sex,subject,height in [('male','N197925',170.0),('female','N186416',170.2)]:
        r=measure(sex,matrices)
        text=subprocess.check_output([str(formula_exe),sex,str(height)],cwd=tpose.REPO,text=True)
        predictions=dict(line.split('\t') for line in text.splitlines())
        expected=float(predictions['shoulder_width'])
        n,mean,sd,source=stats[(sex,'shoulder_width')]
        row=dict(subject=subject,sex=sex,height_cm=f'{height:.1f}',
                 min_x_bone=r['minimum_bone'],max_x_bone=r['maximum_bone'])
        for label in ('minimum','maximum'):
            for axis,value in zip('xyz',r[label]):row[f'{label}_{axis}_m']=f'{value:.8f}'
        row.update(scapula_x_width_cm=f'{r["width_x"]:.6f}',
                   selected_points_3d_cm=f'{r["distance_3d"]:.6f}',
                   nhanes_prediction_cm=f'{expected:.6f}',
                   unmatched_x_difference_cm=f'{r["width_x"]-expected:.6f}',
                   unmatched_x_difference_percent=f'{100*(r["width_x"]/expected-1):.4f}',
                   bio_sample_mean_cm=f'{mean:.6f}',bio_sample_sd_cm=f'{sd:.6f}',
                   bio_sample_n=n,bio_source=source)
        rows.append(row)
    path=tpose.whole.OUT/'TPOSE_SCAPULA_EXTREMA.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0],delimiter='\t',lineterminator='\n')
        writer.writeheader();writer.writerows(rows)
    print('wrote',path)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--pose-cli',type=Path)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory() as d:
        pose=args.pose_cli.resolve() if args.pose_cli else Path(d)/'pose'
        if not args.pose_cli: tpose.compile_exporter(pose)
        formula=Path(d)/'formula';compile_formula(formula)
        generate(pose,formula)

if __name__=='__main__':main()
