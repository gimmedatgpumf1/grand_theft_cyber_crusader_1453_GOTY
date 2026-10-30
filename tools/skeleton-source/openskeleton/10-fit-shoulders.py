"""Estimate each humeral-head center from the subject's source bone surface.

The humerus surface nearest the anatomically connected scapula supplies a head
candidate. A robust sphere is fitted to that candidate; this is a geometric
approximation, not a validated clinical landmark or a motion constraint.
"""
from pathlib import Path
import json
import runpy
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial import cKDTree

HERE = Path(__file__).resolve().parent
BASE = HERE.parent
hips = runpy.run_path(str(HERE/'06-fit-hips.py'))


def fit(humerus, scapula, cutoff=10):
    d = cKDTree(scapula).query(humerus)[0]
    points = humerus[d < cutoff]
    if len(points) < 50: raise ValueError('insufficient humeral head candidates')
    mean = points.mean(axis=0); p = points-mean
    sol, _, rank, _ = np.linalg.lstsq(np.c_[2*p, np.ones(len(p))], np.sum(p*p, axis=1), rcond=None)
    if rank != 4: raise ValueError('degenerate humeral head fit')
    center = mean+sol[:3]; radius = np.sqrt(max(0, sol[3]+sol[:3]@sol[:3]))
    fit = least_squares(lambda x: np.linalg.norm(points-x[:3], axis=1)-x[3],
                        np.r_[center, radius], loss='soft_l1', f_scale=1, max_nfev=100)
    center, radius = fit.x[:3], float(fit.x[3])
    rms = float(np.sqrt(np.mean((np.linalg.norm(points-center,axis=1)-radius)**2)))
    if not fit.success or not 15 < radius < 35 or rms > 3:
        raise ValueError('humeral sphere failed geometric checks')
    return center, radius, rms, len(points)


def main():
    for sex, filename, subject in (('male','male-N197925.npz','N197925'),('female','skelly-out.npz','N186416')):
        rows = ['# Humeral head estimates, metres in the original registered source pose.',
                '# Geometric surface fit; not clinically validated.']
        doc = {'subject': subject, 'clinical_validation': False,
               'method': 'robust sphere on humeral vertices near connected scapula', 'shoulders': {}}
        with np.load(HERE/'data'/filename) as source:
            shift = hips['common_shift'](source)
            for side in ('L','R'):
                h = source['v_Humerus_'+side].astype(float); s = source['v_Scapula_'+side].astype(float)
                c, r, rms, n = fit(h,s)
                alternate, _, _, _ = fit(h,s,15)
                game = np.array([c[0],c[2],-c[1]])/1000+shift
                doc['shoulders'][side] = {'source_center_mm': c.tolist(), 'center_m': game.tolist(),
                    'radius_mm': r, 'rms_mm': rms, 'surface_vertices': n,
                    'cutoff_15mm_center_difference_mm': float(np.linalg.norm(c-alternate))}
                rows.append(side+' '+' '.join(f'{v:.9f}' for v in [*game,r/1000]))
        (BASE/f'skeleton-{sex}.shoulders').write_text('\n'.join(rows)+'\n')
        (BASE/f'skeleton-{sex}-shoulder-fit.json').write_text(json.dumps(doc,indent=2)+'\n')
        print(sex, ', '.join(f"{s}: radius {doc['shoulders'][s]['radius_mm']:.2f} mm" for s in ('L','R')))


if __name__ == '__main__': main()
