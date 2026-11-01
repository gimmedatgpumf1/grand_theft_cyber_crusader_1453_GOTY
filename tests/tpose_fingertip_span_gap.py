#!/usr/bin/env python3
"""Check fingertip span subtraction using the actual room T-pose geometry."""
import csv
import importlib.util
import tempfile
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('span',SRC/'openskeleton/39-measure-fingertip-span.py')
span=importlib.util.module_from_spec(spec);spec.loader.exec_module(span)
rows=list(csv.DictReader((SRC/'anthropometry/TPOSE_FINGERTIP_SPAN_GAP.tsv').open(),delimiter='\t'))
assert len(rows)==2
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'pose';span.tpose.compile_exporter(exe); matrices=span.tpose.matrices(exe)
    for sex in ('male','female'):
        m=span.measure(sex,matrices)
        r=next(r for r in rows if r['sex']==sex)
        for key in ('span','left','right','gap','span_x','start_gap_3d','start_gap_x'):
            assert abs(float(r[key+'_cm'])-m[key])<1e-6
        for name in ('left_tip','right_tip','left_start','right_start'):
            assert np.allclose([float(r[f'{name}_{axis}_m']) for axis in 'xyz'],m[name],atol=1e-8)
        assert r['left_finger_bone']==m['left_bone'] and r['right_finger_bone']==m['right_bone']
        assert abs(m['span']-m['left']-m['right']-m['gap'])<1e-10
        # 3D triangle inequality; horizontal projected subtraction is exact.
        assert m['gap']<=m['start_gap_3d']+1e-10
        x=m['span_x']-(m['left_tip'][0]-m['left_start'][0])*100-(m['right_start'][0]-m['right_tip'][0])*100
        assert abs(x-m['start_gap_x'])<1e-10
print('fingertip span, arm subtraction and geometric interpretation checks passed')
