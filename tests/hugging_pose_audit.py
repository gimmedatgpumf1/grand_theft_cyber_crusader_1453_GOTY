#!/usr/bin/env python3
"""Verify source arm posture and real reference T-pose, including rollback."""
import csv
import gzip
import importlib.util
import tempfile
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('hug',SRC/'openskeleton/42-audit-hugging-pose.py')
hug=importlib.util.module_from_spec(spec);spec.loader.exec_module(hug)
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'pose';hug.tpose.compile_exporter(exe)
    poses={'registered_source':hug.tpose.matrices(exe,('--source',)), 'room_tpose':hug.tpose.matrices(exe)}
    rows=hug.calculate(poses)
saved=list(csv.DictReader((SRC/'anthropometry/HUGGING_POSE_AUDIT.tsv').open(),delimiter='\t'))
assert len(rows)==len(saved)==8
for row,record in zip(rows,saved):
    for k,v in row.items():
        if isinstance(v,(float,np.floating)): assert abs(float(record[k])-v)<1e-6
        else: assert record[k]==str(v)
    if row['pose']=='registered_source':
        assert row['elbow_shaft_deflection_deg']>90
        assert row['forearm_inward_x_cm']>0 and row['finger_tip_own_side_from_sternum_x_cm']<0
        assert row['wrist_ahead_of_shoulder_z_cm']>0
    else:
        assert row['elbow_shaft_deflection_deg']<5 and row['humerus_outward_axis_alignment']>.999
        assert row['forearm_outward_axis_alignment']>.99
# Archived matrices are a rejected experiment, not a shipped posture.
experimental={}
with gzip.open(SRC/'anthropometry/TPOSE_REJECTED_20_DEGREES.tsv.gz','rt') as f:
    for line in f:
        t=line.split();experimental[t[0],t[1]]=np.asarray(t[2:],float).reshape(4,4,order='F')
assert len(experimental)==364
for sex,expected in [('male',35.236430),('female',36.845242)]:
    mesh=hug.tpose.whole.mesh(sex)
    scap=np.vstack([hug.tpose.transform(mesh['Scapula_'+side],experimental[sex,'Scapula_'+side]) for side in 'LR'])
    assert abs(np.ptp(scap[:,0])*100-expected)<1e-6
print('Registered source hugging-like posture, T-pose arms, rejected experiment PASS')
