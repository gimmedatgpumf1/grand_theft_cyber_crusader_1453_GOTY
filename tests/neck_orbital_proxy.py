#!/usr/bin/env python3
"""Regression test for geometric proxy and its clearly unmatched comparison.

Run after 31-measure-whole-body.py. Requires numpy and scipy.
"""
import csv
import importlib.util
import numpy as np
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('whole',SOURCE/'openskeleton/31-measure-whole-body.py')
whole=importlib.util.module_from_spec(spec)
spec.loader.exec_module(whole)
whole.verify_fits()
rows=list(csv.DictReader((SOURCE/'anthropometry/WHOLE_BODY_SKELETON_PROXIES.tsv').open(),delimiter='\t'))
for sex,formula in [('male','male'),('female','female')]:
    row=next(r for r in rows if r['sex']==sex and r['formula_sex']==formula and r['dimension']=='neck')
    avg,left,right=whole.measure(sex)['neck']
    expected={'male':12.0801056642044,'female':13.690671054883264}[sex]
    assert abs(avg-expected)<1e-8
    assert abs(avg-(left+right)/2)<1e-12
    for actual,key in [(avg,'proxy_cm'),(left,'left_cm'),(right,'right_cm')]:
        assert abs(actual-float(row[key]))<1e-6
    predicted=float(row['formula_cm']); difference=avg-predicted
    assert abs(difference-float(row['unmatched_difference_cm']))<2e-6
    assert abs(100*difference/predicted-float(row['unmatched_difference_percent']))<1e-3
    assert 5<avg<25 and 5<predicted<20
# The mathematical re-levelling removes a rigid sagittal head rotation about
# the same pivot. This is not evidence that our proxy is a real Frankfort plane.
for sex in ('male','female'):
    d=whole.mesh(sex); neck=whole.NECK
    orbital=neck.orbital_points(sex); condyles=neck.jaw_condyles(d['Head'],d['Jaw'])
    pivot=d['C1'].mean(axis=0); c7=d['C7'][d['C7'][:,2].argmin()]
    _,baseline=neck.relevel(orbital,condyles,pivot,c7)
    for angle in np.deg2rad([-15.,-7.,7.,15.]):
        def rotate(point):
            q=point-pivot; c=np.cos(angle); s=np.sin(angle)
            return pivot+np.array([q[0],c*q[1]-s*q[2],s*q[1]+c*q[2]])
        o={side:rotate(point) for side,point in orbital.items()}
        c={side:rotate(point) for side,point in condyles.items()}
        _,rotated=neck.relevel(o,c,pivot,c7)
        for side in ('L','R'): assert abs(rotated[side]-baseline[side])<1e-10
print('neck proxy and arithmetic regression passed; anatomical validation remains outstanding')
