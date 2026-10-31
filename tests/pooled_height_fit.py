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
print('Pooled individual-record fit and reproducibility: PASS')
