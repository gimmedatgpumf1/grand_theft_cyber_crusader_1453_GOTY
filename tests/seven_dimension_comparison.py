#!/usr/bin/env python3
"""Verify regenerated seven-dimension arithmetic and sample scatter."""
import csv
import importlib.util
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('seven',SRC/'openskeleton/35-seven-dimensions.py')
seven=importlib.util.module_from_spec(spec);spec.loader.exec_module(seven)
rows=list(csv.DictReader((SRC/'anthropometry/SEVEN_DIMENSION_COMPARISON.tsv').open(),delimiter='\t'))
assert len(rows)==28
stats=seven.stats()
for sex in ('male','female'):
    for model in ('male','female'):
        selected=[r for r in rows if r['sex']==sex and r['formula_sex']==model]
        assert set(r['dimension'] for r in selected)==set(seven.NAMES)
        for row in selected:
            pred=float(row['formula_cm'])
            if row['dimension'] in ('shoulder_width','arm'):
                assert row['measured_proxy_cm']==row['unmatched_difference_cm']==row['unmatched_difference_percent']=='NA'
            else:
                proxy=float(row['measured_proxy_cm'])
                assert abs(proxy-pred-float(row['unmatched_difference_cm']))<2e-6
                assert abs(100*(proxy/pred-1)-float(row['unmatched_difference_percent']))<1e-3
            n,mean,sd,source=stats[(model,row['dimension'])]
            assert int(row['bio_sample_n'])==n and row['bio_sd_source']==source
            assert abs(float(row['bio_sample_mean_cm'])-mean)<1e-6
            assert abs(float(row['bio_sample_sd_cm'])-sd)<1e-6
            if row['left_cm']!='NA':
                assert abs((float(row['left_cm'])+float(row['right_cm']))/2-proxy)<1e-6
    hip,shoulder=seven.widths(seven.whole.mesh(sex))
    expected={'male':(27.36,31.38),'female':(29.209,33.142)}[sex]
    assert np.allclose((hip,shoulder),expected,rtol=0,atol=1e-9)
print('seven-dimension cross-sex and biological SD checks passed')
