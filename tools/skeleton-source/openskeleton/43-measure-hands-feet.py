#!/usr/bin/env python3
"""Measure posed hand/foot bone proxies and compare ANSUR surface dimensions.

Nothing here is a clinical measurement or patient-specific normality test.
See anthropometry/HAND_FOOT_COMPARISON.md for anatomical and sample limitations.
"""
import argparse
import csv
import importlib.util
import tempfile
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('tpose',HERE/'38-measure-t-pose-arm.py')
tpose=importlib.util.module_from_spec(spec);spec.loader.exec_module(tpose)
OUT=tpose.whole.OUT


def unit(v):
    size=np.linalg.norm(v)
    if size<1e-10: raise ValueError('degenerate measurement axis')
    return v/size


def distal_region(bone,proximal_phalanx,quantile):
    # Select a region near the distal metacarpal/metatarsal head, not the
    # maximum diagonal of the bone. This is not a labelled articular landmark.
    axis=unit(proximal_phalanx.mean(axis=0)-bone.mean(axis=0))
    projection=bone@axis
    return bone[projection>=np.quantile(projection,quantile)]


def measure(sex,pose,quantile=0.90):
    d=tpose.whole.mesh(sex);arms=tpose.measure(sex,pose)
    shoulders=tpose.whole.joints(sex,'shoulders');elbows=tpose.whole.joints(sex,'elbows')
    result={}
    for side in ('L','R'):
        def mesh(name): return tpose.transform(d[f'{name}_{side}'],pose[sex,f'{name}_{side}'])
        path=tpose.whole.arm_points(d,shoulders[side],elbows[side],side)
        wrist=tpose.transform([path[2]],pose[sex,f'Radius_{side}'])[0]
        tip=arms[side]['tip'];hand_axis=unit(tip-wrist)
        caps=[distal_region(mesh(f'HandMeta{i}'),mesh(f'HandProxP{i}'),quantile) for i in (2,5)]
        delta=caps[1].mean(axis=0)-caps[0].mean(axis=0)
        transverse=unit(delta-hand_axis*np.dot(delta,hand_axis))
        hand_width=(np.max(caps[1]@transverse)-np.min(caps[0]@transverse))*100
        heel=mesh('Cal');midfoot=mesh('FootMeta3')
        forward=midfoot.mean(axis=0)-heel.mean(axis=0);forward[1]=0;forward=unit(forward)
        heel_projection=float(np.min(heel@forward));toe_projections={}
        for digit in range(1,6): toe_projections[digit]=float(np.max(mesh(f'FootDistP{digit}')@forward))
        longest=max(toe_projections,key=toe_projections.get)
        foot_length=(toe_projections[longest]-heel_projection)*100
        caps=[distal_region(mesh(f'FootMeta{i}'),mesh(f'FootProxP{i}'),quantile) for i in (1,5)]
        transverse=unit(np.cross([0,1,0],forward))
        if np.dot(caps[1].mean(axis=0)-caps[0].mean(axis=0),transverse)<0:transverse=-transverse
        foot_width=(np.max(caps[1]@transverse)-np.min(caps[0]@transverse))*100
        result[side]={'hand_length':float(np.linalg.norm(tip-wrist)*100),
                      'hand_breadth':float(hand_width),'foot_length':float(foot_length),
                      'foot_breadth':float(foot_width),'longest_toe':longest}
    return result


def references(sex,height):
    rows=list(csv.DictReader((OUT/'ANSUR_II_PAIRED_MM.csv').open()))
    rows=[r for r in rows if r['Gender'].lower()==sex and float(r['Age'])>=18]
    h=np.array([float(r['stature'])/10 for r in rows])
    subset=(h>=165)&(h<175);result={}
    for key,column in [('hand_length','handlength'),('hand_breadth','handbreadth'),
                       ('foot_length','footlength'),('foot_breadth','footbreadthhorizontal')]:
        y=np.array([float(r[column])/10 for r in rows]);slope,intercept=np.polyfit(h,y,1)
        result[key]={'n':int(subset.sum()),'mean':float(y[subset].mean()),
                     'sd':float(y[subset].std(ddof=1)),'height_prediction':float(intercept+slope*height),
                     'intercept':float(intercept),'slope':float(slope),'fit_n':len(h)}
    return result


def calculate(pose):
    rows=[]
    for sex,subject,height in [('male','N197925',170.0),('female','N186416',170.2)]:
        base=measure(sex,pose);lo=measure(sex,pose,0.85);hi=measure(sex,pose,0.95)
        ref=references(sex,height)
        for side in ('L','R','mean'):
            for kind in ('hand_length','hand_breadth','foot_length','foot_breadth'):
                def value(x):return (x['L'][kind]+x['R'][kind])/2 if side=='mean' else x[side][kind]
                r=ref[kind];v=value(base)
                rows.append({'subject':subject,'sex':sex,'height_cm':height,'side':side,'dimension':kind,
                    'bone_proxy_cm':v,'height_prediction_cm':r['height_prediction'],
                    'unmatched_difference_cm':v-r['height_prediction'],'biological_mean_cm':r['mean'],
                    'biological_sd_cm':r['sd'],'biological_n':r['n'],'fit_n':r['fit_n'],
                    'fit_intercept_cm':r['intercept'],'fit_slope':r['slope'],
                    'cap85_cm':value(lo) if 'breadth' in kind else 'NA',
                    'cap95_cm':value(hi) if 'breadth' in kind else 'NA',
                    'longest_toe':base[side]['longest_toe'] if kind=='foot_length' and side!='mean' else 'NA'})
    return rows


def write(rows):
    path=OUT/'HAND_FOOT_COMPARISON.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0].keys(),delimiter='\t',lineterminator='\n');writer.writeheader()
        writer.writerows({k:f'{v:.6f}' if isinstance(v,(float,np.floating)) else v for k,v in r.items()} for r in rows)
    print('wrote',path)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--pose-cli',type=Path);args=parser.parse_args()
    with tempfile.TemporaryDirectory() as d:
        exe=args.pose_cli.resolve() if args.pose_cli else Path(d)/'pose'
        if not args.pose_cli:tpose.compile_exporter(exe)
        write(calculate(tpose.matrices(exe)))

if __name__=='__main__':main()
