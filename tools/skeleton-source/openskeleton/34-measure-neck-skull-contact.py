#!/usr/bin/env python3
"""Estimate the C1-to-skull contacts and measure them to posterior C7.

This is a bone-to-bone geometric proxy in the original source pose, not the
ANSUR C7-to-tragion surface measurement. NumPy, SciPy and a C++17 compiler
are required for the generated arithmetic comparison.
"""
import csv
import importlib.util
import subprocess
import tempfile
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('whole_body',HERE/'31-measure-whole-body.py')
whole=importlib.util.module_from_spec(spec)
spec.loader.exec_module(whole)


def measure(sex):
    bones=whole.mesh(sex)
    atlas=bones['C1']; skull=bones['Head']; c7=bones['C7']
    # Most posterior bone vertex, same lower landmark as the neck-link audit.
    lower=c7[np.argmin(c7[:,2])]
    mid=atlas[:,0].mean()
    # The superior and lateral C1 articular surfaces face the skull. The
    # closest pair is only a geometric contact estimate, not a clinical label.
    upper=atlas[:,1]>=np.quantile(atlas[:,1],.5)
    skull_tree=cKDTree(skull)
    contacts={}
    for side,sign in [('L',1),('R',-1)]:
        candidates=atlas[upper & ((atlas[:,0]-mid)*sign>.008)]
        if len(candidates)==0: raise ValueError(f'{sex} {side}: no atlas candidates')
        distances,indices=skull_tree.query(candidates)
        index=int(np.argmin(distances)); atlas_point=candidates[index]
        skull_point=skull[indices[index]]
        contact=(atlas_point+skull_point)/2
        contacts[side]={'c1':atlas_point,'skull':skull_point,'contact':contact,
                        'gap_mm':float(distances[index]*1000),
                        'vertical_cm':float((contact[1]-lower[1])*100),
                        'chord_cm':float(np.linalg.norm(contact-lower)*100)}
    return lower,contacts


def main():
    rows=[]
    measured={}
    for sex,subject in [('male','N197925'),('female','N186416')]:
        lower,contacts=measure(sex)
        measured[sex]=contacts
        for side in ('L','R'):
            c=contacts[side]
            row={'subject':subject,'sex':sex,'side':side}
            for label,point in [('c7',lower),('c1_contact',c['c1']),
                                ('skull_contact',c['skull']),('contact_midpoint',c['contact'])]:
                for axis,value in zip('xyz',point):row[f'{label}_{axis}_m']=f'{value:.8f}'
            for label in ('gap_mm','vertical_cm','chord_cm'):row[label]=f'{c[label]:.6f}'
            rows.append(row)
        print(sex, 'L/R vertical cm',*[f"{contacts[x]['vertical_cm']:.3f}" for x in ('L','R')],
              'mean',f"{np.mean([c['vertical_cm'] for c in contacts.values()]):.3f}")
    path=whole.OUT/'SKELETON_NECK_SKULL_CONTACT.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0].keys(),delimiter='\t',lineterminator='\n')
        writer.writeheader(); writer.writerows(rows)
    print('wrote',path)
    # Surface neck link and skeletal contact are distinct; differences below
    # are merely requested arithmetic, not a clinical deviation or fit.
    comparisons=[]
    with tempfile.TemporaryDirectory() as temp:
        binary=Path(temp)/'profile'
        subprocess.run(['c++','-std=c++17','-O2','-I',str(whole.REPO/'native'),
                        str(HERE/'31-height-profile.cpp'),'-o',str(binary)],check=True)
        for sex,subject,height in [('male','N197925',170.0),('female','N186416',170.2)]:
            contact=measured[sex]
            average=np.mean([contact[side]['vertical_cm'] for side in ('L','R')])
            for model in ('male','female'):
                text=subprocess.check_output([str(binary),model,str(height)],text=True)
                predicted=float(dict(line.split('\t') for line in text.splitlines())['neck'])
                comparisons.append({'subject':subject,'sex':sex,'formula_sex':model,
                    'height_cm':f'{height:.1f}',
                    'left_cm':f"{contact['L']['vertical_cm']:.6f}",
                    'right_cm':f"{contact['R']['vertical_cm']:.6f}",
                    'proxy_cm':f'{average:.6f}','surface_prediction_cm':f'{predicted:.6f}',
                    'unmatched_difference_cm':f'{average-predicted:.6f}',
                    'unmatched_difference_percent':f'{100*(average/predicted-1):.4f}'})
    path=whole.OUT/'SKELETON_NECK_SKULL_VS_SURFACE.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=comparisons[0].keys(),delimiter='\t',lineterminator='\n')
        writer.writeheader();writer.writerows(comparisons)
    print('wrote',path)


if __name__=='__main__': main()
