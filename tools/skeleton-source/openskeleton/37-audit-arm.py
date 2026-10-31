#!/usr/bin/env python3
"""Audit the old bone-centre arm paths against their actual reference endpoints."""
import csv
import importlib.util
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('whole',HERE/'31-measure-whole-body.py')
whole=importlib.util.module_from_spec(spec);spec.loader.exec_module(whole)


def profile(sex):
    bones=whole.mesh(sex);sh=whole.joints(sex,'shoulders');el=whole.joints(sex,'elbows')
    result={}
    for side in ('L','R'):
        p=whole.arm_points(bones,sh[side],el[side],side)
        # Sensitivity check only: this is the retracted upper-scapula extreme,
        # not a validated acromion. Do not turn it into a corrected arm length.
        sign=1 if side=='L' else -1
        scap=bones[f'Scapula_{side}']
        upper=scap[scap[:,1]>=np.quantile(scap[:,1],.75)]
        candidate=upper[np.argmax(sign*upper[:,0])]
        result[side]={'points':p,'upper':whole.distances(p[:2]),
                      'forearm':whole.distances(p[1:3]),
                      'hand':whole.distances(p[2:]),'total':whole.distances(p),
                      'candidate_to_elbow':whole.distances([candidate,el[side]]),
                      'candidate_to_head':whole.distances([candidate,sh[side]])}
    return result


def fits(sex,h):
    rows=list(csv.DictReader((whole.OUT/'ANSUR_II_PAIRED_MM.csv').open()))
    rows=[r for r in rows if r['Gender'].lower()==sex and float(r['Age'])>=18]
    height=np.array([float(r['stature'])/10 for r in rows])
    result={}
    for label,col in [('upper','acromionradialelength'),('forearm','radialestylionlength'),
                      ('hand','handlength'),('outseam','sleeveoutseam')]:
        y=np.array([float(r[col])/10 for r in rows])
        slope,intercept=np.polyfit(height,y,1)
        result[label]=float(intercept+slope*h)
    result['outseam_plus_hand']=result['outseam']+result['hand']
    return result,len(rows)


def main():
    out=[]
    for sex,subject,h in [('male','N197925',170.0),('female','N186416',170.2)]:
        geo=profile(sex);ref,n=fits(sex,h)
        for side in ('L','R','mean'):
            value={k:(geo[side][k] if side!='mean' else (geo['L'][k]+geo['R'][k])/2)
                   for k in ('upper','forearm','hand','total','candidate_to_elbow','candidate_to_head')}
            out.append(dict(subject=subject,sex=sex,height_cm=f'{h:.1f}',side=side,
                bone_head_to_elbow_cm=f'{value["upper"]:.6f}',
                elbow_to_radius_end_cm=f'{value["forearm"]:.6f}',
                radius_end_to_finger_bone_tip_path_cm=f'{value["hand"]:.6f}',
                historic_bone_path_total_cm=f'{value["total"]:.6f}',
                unverified_scapula_candidate_to_head_cm=f'{value["candidate_to_head"]:.6f}',
                unverified_scapula_candidate_to_elbow_cm=f'{value["candidate_to_elbow"]:.6f}',
                ansur_acromion_to_radiale_fit_cm=f'{ref["upper"]:.6f}',
                ansur_radiale_to_stylion_fit_cm=f'{ref["forearm"]:.6f}',
                ansur_stylion_to_fingertip_fit_cm=f'{ref["hand"]:.6f}',
                ansur_acromion_to_stylion_fit_cm=f'{ref["outseam"]:.6f}',
                ansur_outseam_plus_hand_fit_cm=f'{ref["outseam_plus_hand"]:.6f}',ansur_n=n))
    path=whole.OUT/'ARM_PATH_AUDIT.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=out[0].keys(),delimiter='\t',lineterminator='\n')
        writer.writeheader();writer.writerows(out)
    print('wrote',path)

if __name__=='__main__':main()
