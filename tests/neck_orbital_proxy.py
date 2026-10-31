#!/usr/bin/env python3
"""Regression test for geometric proxy and its clearly unmatched comparison.

Run after 31-measure-whole-body.py. Requires numpy and scipy.
"""
import csv
import importlib.util
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
    assert abs(avg-(left+right)/2)<1e-12
    for actual,key in [(avg,'proxy_cm'),(left,'left_cm'),(right,'right_cm')]:
        assert abs(actual-float(row[key]))<1e-6
    predicted=float(row['formula_cm']); difference=avg-predicted
    assert abs(difference-float(row['unmatched_difference_cm']))<2e-6
    assert abs(100*difference/predicted-float(row['unmatched_difference_percent']))<1e-3
    assert 5<avg<25 and 5<predicted<20
print('neck proxy and arithmetic regression passed; anatomical validation remains outstanding')
