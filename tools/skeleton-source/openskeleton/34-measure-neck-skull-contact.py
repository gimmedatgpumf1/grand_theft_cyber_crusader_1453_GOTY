#!/usr/bin/env python3
"""Estimate the C1-to-skull contacts and measure them to posterior C7.

This is a bone-to-bone geometric proxy in the original source pose, not the
ANSUR C7-to-tragion surface measurement. NumPy and SciPy are required.
"""
import csv
import importlib.util
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
    for sex,subject in [('male','N197925'),('female','N186416')]:
        lower,contacts=measure(sex)
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


if __name__=='__main__': main()
