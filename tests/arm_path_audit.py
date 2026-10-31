#!/usr/bin/env python3
"""Check arm path components, bilateral means and independent ANSUR refits."""
import csv
import importlib.util
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('arm',SRC/'openskeleton/37-audit-arm.py')
arm=importlib.util.module_from_spec(spec);spec.loader.exec_module(arm)
rows=list(csv.DictReader((SRC/'anthropometry/ARM_PATH_AUDIT.tsv').open(),delimiter='\t'))
assert len(rows)==6
for sex,height,expected in [('male',170.,72.666447),('female',170.2,68.300792)]:
    data=arm.profile(sex); ref,n=arm.fits(sex,height)
    old=arm.whole.measure(sex)['arm']
    assert abs(old[0]-expected)<1e-6
    for side in ('L','R'):
        p=data[side]
        assert abs(p['upper']+p['forearm']+p['hand']-p['total'])<1e-10
    for side in ('L','R','mean'):
        row=next(r for r in rows if r['sex']==sex and r['side']==side)
        expected_total=(data[side]['total'] if side!='mean' else old[0])
        assert abs(float(row['historic_bone_path_total_cm'])-expected_total)<1e-6
        candidate=(data[side]['candidate_to_elbow'] if side!='mean' else
                   (data['L']['candidate_to_elbow']+data['R']['candidate_to_elbow'])/2)
        assert abs(float(row['unverified_scapula_candidate_to_elbow_cm'])-candidate)<1e-6
        assert abs(float(row['ansur_outseam_plus_hand_fit_cm'])-ref['outseam_plus_hand'])<1e-6
        assert int(row['ansur_n'])==n
print('arm path, left/right mean and reference component checks passed')
