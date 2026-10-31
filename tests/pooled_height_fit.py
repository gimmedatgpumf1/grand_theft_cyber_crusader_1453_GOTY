#!/usr/bin/env python3
"""Verify reproducibility and that both raw cohorts affect the pooled fit."""
import importlib.util
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
script = root/'tools/skeleton-source/openskeleton/26-pooled-height-models.py'
try:
    import numpy as np
    import pandas as pd
    import scipy  # noqa: F401
except ImportError:
    print('Pooled individual-record fit: skipped (NumPy, pandas, SciPy optional)')
    raise SystemExit(0)
subprocess.run([sys.executable, str(script), '--check'], cwd=root, check=True)
spec = importlib.util.spec_from_file_location('pooled_fitter',script)
fitter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fitter)
n = fitter.checked('NHANES_III_PAIRED_CM.csv')
n = n[(n.BMPHTFLG == 0) & n.HSSEX.isin([1,2]) & n.MXPAXTMR.notna()]
for sex in (1,2):
    group=n[n.HSSEX == sex]
    children=group[group.MXPAXTMR < 216]
    adults=group[group.MXPAXTMR >= 216]
    for column in fitter.NHANES.values():
        x, y = children.BMPHT.to_numpy(), children[column].to_numpy()
        ax, ay = adults.BMPHT.to_numpy(), adults[column].to_numpy()
        full, stats = fitter.fit(x,y,ax,ay)
        # Shift each cohort separately and ensure it really contributes to the
        # fitted polynomial, rather than to a discarded or blended side model.
        changed_child, _ = fitter.fit(x,y+0.25,ax,ay)
        changed_adult, _ = fitter.fit(x,y,ax,ay+0.25)
        assert np.max(np.abs(full-changed_child)) > 1e-4
        assert np.max(np.abs(full-changed_adult)) > 1e-4
        assert stats['children'] > 1000 and stats['adults'] > 1000
# Real infant pairs must change the estimate; a neonatal summary is a separate
# soft observation, not fictitious replicated raw rows.
infant=fitter.checked('NHANES_III_INFANT_PAIRED_CM.csv')
infant=infant[(infant.HSSEX==1)&(infant.BMPHEAD.isna() | (infant.BMPHEAD<infant.BMPRECUM))]
children=n[(n.HSSEX==1)&(n.MXPAXTMR<216)]
adults=n[(n.HSSEX==1)&(n.MXPAXTMR>=216)]
x,y=children.BMPHT,children.BMPARML
ax,ay=adults.BMPHT,adults.BMPARML
with_infants,stats=fitter.fit(x,y,ax,ay,infant.BMPRECUM,infant.BMPARML,9.5)
shifted,_=fitter.fit(x,y,ax,ay,infant.BMPRECUM,infant.BMPARML+1,9.5)
shifted_anchor,_=fitter.fit(x,y,ax,ay,infant.BMPRECUM,infant.BMPARML,10.5)
assert stats['infants']>1000 and np.max(abs(with_infants-shifted))>0.01
assert abs(shifted_anchor[0]-with_infants[0])>0.1
report=pd.read_csv(root/'tools/skeleton-source/anthropometry/POOLED_HEIGHT_MODELS.tsv',sep='\t')
coeffs=fitter.ROOT/'native/client/pooled_height_coefficients.h'
lines=[line for line in coeffs.read_text().splitlines() if '}, //' in line]
assert len(lines)==len(report)==20
for row,line in zip(report.itertuples(),lines):
    c0=float(line.split('{')[1].split(',')[0])
    if np.isfinite(row.anchor_cm): assert abs(c0-row.anchor_cm)<0.6
# Two identifier-level holdouts check actual observations, including
# unusually tall subjects. Some per-dimension subsets are small or worse; do
# not interpret the aggregate as clinical validation.
holdout=pd.read_csv(root/'tools/skeleton-source/anthropometry/POOLED_HEIGHT_TAIL_HOLDOUT.tsv',sep='\t')
replication=pd.read_csv(root/'tools/skeleton-source/anthropometry/POOLED_HEIGHT_TAIL_REPLICATION.tsv',sep='\t')
for audit in (holdout,replication):
    for tail in ('short','tall'):
        h=audit[audit.region==tail]
        assert len(h)>=18 and h.n.sum()>100
        assert h.new_mae_cm.mean()<h.old_mae_cm.mean()
print('Pooled individual-record, neonatal-anchor and tail holdout audit: PASS')
