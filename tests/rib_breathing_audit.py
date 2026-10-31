#!/usr/bin/env python3
"""Verify full pose rollback and regenerate the independent rib measurements."""
import csv
import importlib.util
import tempfile
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('audit',SRC/'openskeleton/41-audit-rib-breathing.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'pose';audit.tpose.compile_exporter(exe)
    pose=audit.tpose.matrices(exe);source=audit.tpose.matrices(exe,('--source',));old=audit.baseline()
    assert set(pose)==set(old) and len(pose)==364
    # The ENTIRE room pose, not just the shoulder, is back to d26c7b3.
    assert all(np.allclose(pose[k],old[k],atol=1e-10,rtol=0) for k in old)
    assert all(np.allclose(m,np.eye(4),atol=1e-10,rtol=0) for m in source.values())
    projections,cages=audit.calculate({'registered_source':source,'restored_tpose':pose})
for name,computed in [('RIB_PROJECTION_AUDIT.tsv',projections),('RIB_CAGE_COMPARISON.tsv',cages)]:
    saved=list(csv.DictReader((audit.OUT/name).open(),delimiter='\t'))
    assert len(saved)==len(computed)
    for row,record in zip(computed,saved):
        for key,value in row.items():
            if isinstance(value,(float,np.floating)): assert abs(float(record[key])-value)<1e-6
            else: assert record[key]==str(value)
for sex,expected in [('male',(20.808,21.309302,19.532,1.752)),
                     ('female',(16.444,17.851628,17.766,1.517))]:
    for n in range(1,13):
        for side in 'LR':
            assert np.allclose(pose[sex,f'Rib{n}_{side}'],pose[sex,'Sternum'],atol=1e-10,rtol=0)
    row=next(r for r in cages if r['sex']==sex and r['pose']=='restored_tpose')
    actual=[row[x] for x in ('depth_global_z_cm','depth_posterior_plane_cm','ct_depth_mean_cm','ct_depth_sd_cm')]
    assert np.allclose(actual,expected,atol=1e-6,rtol=0)
    # The source-to-T-pose change is a rigid trunk rotation, not a breath.
    prior=next(r for r in cages if r['sex']==sex and r['pose']=='registered_source')
    assert abs(row['depth_posterior_plane_cm']-prior['depth_posterior_plane_cm'])<1e-8
    # Tests the effect of alternative robust-posterior selections, not clinical validity.
    assert abs(row['depth_extreme_proxy_cm']-row['depth_posterior_plane_cm'])<0.02
    assert abs(row['depth_five_percent_proxy_cm']-row['depth_posterior_plane_cm'])<0.09
print('Whole-pose restoration and reproducible rib-cage measurements PASS')
