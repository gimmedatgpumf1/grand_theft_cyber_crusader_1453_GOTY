#!/usr/bin/env python3
"""Check the exact room T-pose, both hands, shoulder proxies and saved output."""
import csv
import importlib.util
import tempfile
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('tpose',SRC/'openskeleton/38-measure-t-pose-arm.py')
tpose=importlib.util.module_from_spec(spec);spec.loader.exec_module(tpose)
rows=list(csv.DictReader((SRC/'anthropometry/T_POSE_ARM_MEASUREMENTS.tsv').open(),delimiter='\t'))
assert len(rows)==6
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'pose';tpose.compile_exporter(exe);pose=tpose.matrices(exe)
    for sex,height in [('male',170.0),('female',170.2)]:
        measurements=tpose.measure(sex,pose);fit,n=tpose.audit.fits(sex,height)
        for side in ('L','R'):
            r=next(r for r in rows if r['sex']==sex and r['side']==side)
            m=measurements[side]
            assert abs(float(r['shoulder_proxy_to_fingertip_3d_cm'])-m['straight'])<1e-6
            assert abs(float(r['shoulder_proxy_to_fingertip_horizontal_cm'])-m['horizontal'])<1e-6
            assert abs(float(r['joint_center_to_tip_3d_cm'])-m['head_to_tip'])<1e-6
            assert abs(float(r['proximal_humerus_to_tip_3d_cm'])-m['humerus_end_to_tip'])<1e-6
            assert np.allclose([float(r[f'humerus_proximal_{ax}_m']) for ax in 'xyz'],m['humerus_end'],atol=1e-8)
            sign=1 if side=='L' else -1
            source=tpose.whole.mesh(sex)[f'Humerus_{side}']
            posed=tpose.transform(source,pose[(sex,f'Humerus_{side}')])
            assert abs(sign*m['humerus_end'][0]-(sign*posed[:,0]).min())<1e-10
            assert np.linalg.norm(m['humerus_end']-m['head'])<.04
            assert np.allclose([float(r[f'shoulder_proxy_{ax}_m']) for ax in 'xyz'],m['shoulder'],atol=1e-8)
            assert np.allclose([float(r[f'middle_bone_tip_{ax}_m']) for ax in 'xyz'],m['tip'],atol=1e-8)
            assert m['upper_alignment']>.999 and m['fore_alignment']>.99 and m['finger_alignment']>.9
            assert m['straight']>=m['horizontal'] and int(r['ansur_n'])==n
        r=next(r for r in rows if r['sex']==sex and r['side']=='mean')
        value=(measurements['L']['straight']+measurements['R']['straight'])/2
        ref=fit['outseam_plus_hand']
        assert abs(float(r['shoulder_proxy_to_fingertip_3d_cm'])-value)<1e-6
        proximal=(measurements['L']['humerus_end_to_tip']+measurements['R']['humerus_end_to_tip'])/2
        assert abs(float(r['proximal_humerus_to_tip_3d_cm'])-proximal)<1e-6
        assert abs(float(r['ansur_height_prediction_cm'])-ref)<1e-6
        assert abs(float(r['raw_unmatched_difference_cm'])-(value-ref))<1e-6
        assert abs(float(r['raw_unmatched_difference_percent'])-100*(value/ref-1))<1e-4
print('actual T-pose arm-to-fingertip measurement checks passed')
