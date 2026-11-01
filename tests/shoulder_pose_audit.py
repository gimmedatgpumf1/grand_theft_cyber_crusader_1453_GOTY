#!/usr/bin/env python3
"""Check full-resolution versus simplified registered shoulder diagnostics."""
import csv
import importlib.util
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('audit',SRC/'openskeleton/36-audit-shoulder.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
rows=list(csv.DictReader((SRC/'anthropometry/SHOULDER_POSE_AUDIT.tsv').open(),delimiter='\t'))
assert len(rows)==8
for sex in ('male','female'):
    src=audit.audit(audit.registered_source(sex))
    obj=audit.audit(audit.whole.mesh(sex))
    assert abs(src['span']-obj['span'])<.2
    assert abs(obj['span_10']-obj['span'])>2
    for geometry,result in [('source_full_resolution',src),('game_simplified',obj)]:
        for side in ('L','R'):
            r=next(r for r in rows if r['sex']==sex and r['geometry']==geometry and r['side']==side)
            assert abs(float(r['span_cm'])-result['span'])<1e-6
            assert abs(float(r['hypothetical_10_degrees_cm'])-result['span_10'])<1e-6
            assert abs(float(r['hypothetical_20_degrees_cm'])-result['span_20'])<1e-6
            assert np.allclose([float(r[f'scapular_extreme_{axis}_m']) for axis in 'xyz'],
                               result['details'][side]['point'],atol=1e-8)
print('shoulder source, simplified mesh and pose-sensitivity checks passed')
