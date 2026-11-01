#!/usr/bin/env python3
"""Check extrema from actual T-pose scapula meshes and biological references."""
import csv
import importlib.util
import subprocess
import tempfile
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('scap',SRC/'openskeleton/40-measure-scapula-span.py')
scap=importlib.util.module_from_spec(spec);spec.loader.exec_module(scap)
rows=list(csv.DictReader((SRC/'anthropometry/TPOSE_SCAPULA_EXTREMA.tsv').open(),delimiter='\t'))
assert len(rows)==2
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'pose';formula=Path(d)/'formula'
    scap.tpose.compile_exporter(exe);scap.compile_formula(formula)
    pose=scap.tpose.matrices(exe);stats=scap.seven.stats()
    for sex,height in [('male',170.0),('female',170.2)]:
        m=scap.measure(sex,pose);r=next(r for r in rows if r['sex']==sex)
        source=scap.tpose.whole.mesh(sex)
        allverts=np.concatenate([scap.tpose.transform(source[f'Scapula_{side}'],
                                      pose[(sex,f'Scapula_{side}')]) for side in ('L','R')])
        assert np.isclose(m['minimum'][0],allverts[:,0].min(),atol=1e-12)
        assert np.isclose(m['maximum'][0],allverts[:,0].max(),atol=1e-12)
        for name in ('minimum','maximum'):
            assert np.allclose([float(r[f'{name}_{axis}_m']) for axis in 'xyz'],m[name],atol=1e-8)
        assert r['min_x_bone']==m['minimum_bone'] and r['max_x_bone']==m['maximum_bone']
        assert abs(float(r['scapula_x_width_cm'])-m['width_x'])<1e-6
        assert abs(float(r['selected_points_3d_cm'])-m['distance_3d'])<1e-6
        assert m['distance_3d']>=m['width_x']>0
        output=subprocess.check_output([str(formula),sex,str(height)],cwd=ROOT,text=True)
        pred=float(dict(line.split('\t') for line in output.splitlines())['shoulder_width'])
        assert abs(float(r['nhanes_prediction_cm'])-pred)<1e-6
        assert abs(float(r['unmatched_x_difference_cm'])-(m['width_x']-pred))<1e-6
        assert abs(float(r['unmatched_x_difference_percent'])-100*(m['width_x']/pred-1))<1e-4
        n,mean,sd,source=stats[(sex,'shoulder_width')]
        assert int(r['bio_sample_n'])==n and r['bio_source']==source
        assert abs(float(r['bio_sample_mean_cm'])-mean)<1e-6
        assert abs(float(r['bio_sample_sd_cm'])-sd)<1e-6
print('actual T-pose bilateral scapula extrema and reference arithmetic checks passed')
