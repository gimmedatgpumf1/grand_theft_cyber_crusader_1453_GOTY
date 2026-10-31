#!/usr/bin/env python3
"""Regression checks for bilateral bone contact proxy and exported distances."""
import csv
import importlib.util
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('neck_contact',SOURCE/'openskeleton/34-measure-neck-skull-contact.py')
neck=importlib.util.module_from_spec(spec)
spec.loader.exec_module(neck)
rows=list(csv.DictReader((SOURCE/'anthropometry/SKELETON_NECK_SKULL_CONTACT.tsv').open(),delimiter='\t'))
expected={'male':(9.44325,10.6568115),'female':(11.41425,13.241848)}
for sex in ('male','female'):
    c7,contacts=neck.measure(sex)
    assert set(contacts)=={'L','R'}
    for side,record in contacts.items():
        row=next(r for r in rows if r['sex']==sex and r['side']==side)
        assert np.allclose(record['contact'],(record['c1']+record['skull'])/2,atol=1e-12)
        assert abs(record['gap_mm']-np.linalg.norm(record['c1']-record['skull'])*1000)<1e-9
        assert 0<=record['gap_mm']<2
        for key in ('vertical_cm','chord_cm','gap_mm'):
            assert abs(float(row[key])-record[key])<1e-6
        assert abs(record['vertical_cm']-(record['contact'][1]-c7[1])*100)<1e-10
        assert abs(record['chord_cm']-np.linalg.norm(record['contact']-c7)*100)<1e-10
    means=[np.mean([v[k] for v in contacts.values()]) for k in ('vertical_cm','chord_cm')]
    assert np.allclose(means,expected[sex],atol=1e-6,rtol=0)
print('bilateral skull-to-C7 contact proxy and TSV checks passed')
