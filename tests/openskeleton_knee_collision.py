"""Optional integration regression; requires NumPy, SciPy and python-fcl."""
from pathlib import Path
import importlib.util
import json
import numpy as np

BASE = Path(__file__).resolve().parents[1] / 'tools/skeleton-source'
spec = importlib.util.spec_from_file_location('audit', BASE/'openskeleton/09-audit-knee-motion.py')
audit = importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)
# The source format is one-based; reject a misleading zero-based mesh.
try:
    audit.collision_object(np.eye(3), np.array([[0,1,2]]))
    raise AssertionError('accepted zero-based faces')
except ValueError: pass

expected = {'male': {'L': None, 'R': 40}, 'female': {'L': None, 'R': 120}}
for sex, name in (('male','male-N197925'), ('female','skelly-out')):
    knees = json.loads((BASE / f'skeleton-{sex}-knee-fit.json').read_text())['knees']
    with np.load(BASE/f'openskeleton/data/{name}.npz') as data:
        for side in ('L','R'):
            rows = audit.audit(data, side, knees[side])
            assert rows[0]['intersecting_triangle_pairs_up_to_10000'] == 0
            assert rows[0]['surface_distance_mm'] > 0
            intersect = [r['relative_degrees'] for r in rows if r['intersecting_triangle_pairs_up_to_10000']]
            assert (intersect[0] if intersect else None) == expected[sex][side]
print('Source knee surface sweep: PASS (diagnostic, not functional/clinical validation)')
