#!/usr/bin/env python3
"""Audit registered scapular X span and pose sensitivity; NOT a landmark fit."""
import csv
import importlib.util
import json
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('whole',HERE/'31-measure-whole-body.py')
whole=importlib.util.module_from_spec(spec);spec.loader.exec_module(whole)


def registered_source(sex):
    name='male-N197925.npz' if sex=='male' else 'skelly-out.npz'
    with np.load(HERE/'data'/name) as s:
        # Identical global conversion and shift to the importer, but no
        # simplification. This does not restore the original cadaver CT pose.
        shift=np.array(json.loads((whole.SOURCE/f'skeleton-{sex}.parts.json').read_text())['global_translation_m'])
        result={}
        for side in ('L','R'):
            for bone in ('Scapula','Clavicula'):
                k=bone+'_'+side;p=s['v_'+k].astype(float)
                result[k]=np.stack((p[:,0],p[:,2],-p[:,1]),axis=1)/1000+shift
        return result


def audit(bones):
    details={};points=[];hypothetical={10:[],20:[]}
    for side,sign in [('L',1),('R',-1)]:
        scap=bones['Scapula_'+side];clav=bones['Clavicula_'+side]
        # Retain the exact previously reported selection, without assuming
        # the most lateral upper-quarter scapular point is the acromion.
        upper=scap[scap[:,1]>=np.quantile(scap[:,1],.75)]
        point=upper[np.argmax(sign*upper[:,0])];points.append(point)
        lateral_x=sign*clav[:,0]
        medial=clav[lateral_x<np.quantile(lateral_x,.05)].mean(axis=0)
        lateral=clav[lateral_x>np.quantile(lateral_x,.95)].mean(axis=0)
        angle=np.arctan2(lateral[1]-medial[1],sign*(lateral[0]-medial[0]))
        for target in hypothetical:
            # Hypothetical coronal rotation of the chosen scapular point with
            # the clavicle, about its estimated medial endpoint; neither the
            # pivot nor the target angle is a reconstructed clinical pose.
            delta=np.deg2rad(target)-angle
            x=sign*(point[0]-medial[0]);y=point[1]-medial[1]
            hypothetical[target].append(medial[0]+sign*(x*np.cos(delta)-y*np.sin(delta)))
        details[side]={'point':point,'medial':medial,'lateral':lateral,'angle':np.rad2deg(angle)}
    width=lambda v: abs(v[0]-v[1])*100
    return {'span':width([p[0] for p in points]),'span_10':width(hypothetical[10]),
            'span_20':width(hypothetical[20]),'details':details}


def main():
    rows=[]
    for sex,subject in [('male','N197925'),('female','N186416')]:
        for geometry,bones in [('source_full_resolution',registered_source(sex)),('game_simplified',whole.mesh(sex))]:
            r=audit(bones)
            for side in ('L','R'):
                p=r['details'][side];point=p['point'];med=p['medial'];lat=p['lateral']
                rows.append(dict(subject=subject,sex=sex,geometry=geometry,side=side,
                    scapular_extreme_x_m=f'{point[0]:.8f}',scapular_extreme_y_m=f'{point[1]:.8f}',
                    scapular_extreme_z_m=f'{point[2]:.8f}',
                    clavicle_medial_x_m=f'{med[0]:.8f}',clavicle_medial_y_m=f'{med[1]:.8f}',
                    clavicle_medial_z_m=f'{med[2]:.8f}',clavicle_lateral_x_m=f'{lat[0]:.8f}',
                    clavicle_lateral_y_m=f'{lat[1]:.8f}',clavicle_lateral_z_m=f'{lat[2]:.8f}',
                    clavicle_coronal_proxy_degrees=f'{p["angle"]:.6f}',span_cm=f'{r["span"]:.6f}',
                    hypothetical_10_degrees_cm=f'{r["span_10"]:.6f}',
                    hypothetical_20_degrees_cm=f'{r["span_20"]:.6f}'))
    path=whole.OUT/'SHOULDER_POSE_AUDIT.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0].keys(),delimiter='\t',lineterminator='\n')
        writer.writeheader();writer.writerows(rows)
    print('wrote',path)

if __name__=='__main__':main()
