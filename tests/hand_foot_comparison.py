#!/usr/bin/env python3
"""Check real posed bone proxies and paired-sex reference calculations."""
import csv
import importlib.util
import tempfile
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('audit',SRC/'openskeleton/43-measure-hands-feet.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'pose';audit.tpose.compile_exporter(exe)
    pose=audit.tpose.matrices(exe);actual=audit.calculate(pose)
saved=list(csv.DictReader((SRC/'anthropometry/HAND_FOOT_COMPARISON.tsv').open(),delimiter='\t'))
assert len(saved)==len(actual)==24
for measured,stored in zip(actual,saved):
    for key,value in measured.items():
        if isinstance(value,(float,np.floating)): assert abs(float(stored[key])-value)<1e-6,(key,stored[key],value)
        else: assert stored[key]==str(value),(key,stored[key],value)
    if measured['side']=='mean':
        sides=[r for r in actual if r['sex']==measured['sex'] and r['dimension']==measured['dimension'] and r['side'] in ('L','R')]
        assert abs(sum(r['bone_proxy_cm'] for r in sides)/2-measured['bone_proxy_cm'])<1e-10
        assert measured['biological_n']==(1706 if measured['sex']=='male' else 651)
        assert measured['biological_sd_cm']>0
        if 'breadth' in measured['dimension']:
            assert measured['cap95_cm']<=measured['bone_proxy_cm']<=measured['cap85_cm']
    assert abs(measured['bone_proxy_cm']-measured['height_prediction_cm']-measured['unmatched_difference_cm'])<1e-10
print('Posed bilateral hand/foot proxies, cap sensitivity and ANSUR comparison: PASS')
