#!/usr/bin/env python3
"""Fit the comparison's sex-specific height models to individual paired records.

Requires numpy, pandas and scipy. See POOLED_HEIGHT_MODELS.md for the important
landmark substitutions, extrapolation assumptions and source limitations.
"""
from pathlib import Path
import hashlib
import math
import re
import sys

import numpy as np
import pandas as pd
from scipy.optimize import lsq_linear

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'tools/skeleton-source/anthropometry'
EXPECTED = {
    'NHANES_III_PAIRED_CM.csv': 'd366cff25da8a8c4a52b180cf85bc1d92d4e7dca1f731bede9ea06b8e08638f4',
    'ANSUR_II_PAIRED_MM.csv': '1b1c0865c328a6aa297a3133f49585eff61002e9208fe3beb183c866c316fbee',
}
DIMENSIONS = ['sitting_height', 'upper_arm_surface', 'upper_leg_surface',
              'shoulder_breadth', 'pelvis_surface_breadth',
              'head', 'neck', 'torso', 'arm', 'leg']
NHANES = dict(zip(DIMENSIONS[:5], ['BMPSITHT', 'BMPARML', 'BMPLEG', 'BMPBIAC', 'BMPBIIL']))


def checked(name):
    path = DATA / name
    if hashlib.sha256(path.read_bytes()).hexdigest() != EXPECTED[name]:
        raise ValueError(f'unexpected source hash: {path}')
    return pd.read_csv(path, low_memory=False)


def fit(child_x, child_y, adult_x, adult_y):
    def clean(x, y):
        x, y = np.asarray(x, dtype=float), np.asarray(y, dtype=float)
        ok = np.isfinite(x) & np.isfinite(y) & (x > 0) & (y > 0) & (y < x)
        return x[ok], y[ok]
    cx, cy = clean(child_x, child_y)
    ax, ay = clean(adult_x, adult_y)
    x, y = np.r_[cx, ax], np.r_[cy, ay]
    assert len(cx) > 20 and len(ax) > 20
    # Bound the unobserved 50 cm endpoint using the ratio in the shortest
    # ten centimetres of measured children. This is an extrapolation prior,
    # not a neonatal observation. Each actual record is still weighted once.
    shortest = cx <= cx.min() + 10
    ratio = np.mean(cy[shortest] / cx[shortest])
    t = (x - 50) / 200
    b = np.column_stack([math.comb(3, i)*t**i*(1-t)**(3-i) for i in range(4)])
    matrix = np.column_stack([np.ones(len(x))] + [b[:, i:].sum(axis=1) for i in range(1, 4)])
    # Positive increments enforce positive slopes over the evaluation interval.
    result = lsq_linear(matrix, y, bounds=(np.r_[0.9*50*ratio, [0.001*200/3]*3],
                                              np.r_[1.1*50*ratio, [np.inf]*3]), tol=1e-12)
    if not result.success:
        raise ValueError(result.message)
    coeff = np.cumsum(result.x)
    prediction = b @ coeff
    return coeff, {'children':len(cx), 'adults':len(ax), 'min_height_cm':x.min(),
                   'max_height_cm':x.max(), 'rmse_cm':np.sqrt(np.mean((prediction-y)**2)),
                   'short_child_ratio':ratio, 'short_child_n':int(shortest.sum())}


def equivalent_generated(actual, expected):
    # Different BLAS/SciPy versions can change the last printed digits of a
    # constrained least-squares solution. Require identical structure and
    # numerically equivalent coefficients/statistics rather than byte identity.
    token = re.compile(r'(?<![A-Za-z0-9_])[-+]?(?:[0-9]+\.[0-9]+|[0-9]+)(?:[eE][-+]?[0-9]+)?')
    if token.sub('<number>', actual) != token.sub('<number>', expected):
        return False
    left = np.array([float(v) for v in token.findall(actual)])
    right = np.array([float(v) for v in token.findall(expected)])
    return left.shape == right.shape and np.allclose(left, right, rtol=1e-9, atol=1e-9)


def main():
    n = checked('NHANES_III_PAIRED_CM.csv')
    n = n[(n.BMPHTFLG == 0) & n.HSSEX.isin([1,2]) & n.MXPAXTMR.notna()]
    a = checked('ANSUR_II_PAIRED_MM.csv')
    a = a[a.Age >= 18]
    models, report = {}, []
    for sex, code in [('male',1), ('female',2)]:
        subjects = n[n.HSSEX == code]
        child, adult = subjects[subjects.MXPAXTMR < 216], subjects[subjects.MXPAXTMR >= 216]
        army = a[a.Gender.str.lower() == sex]
        for dim, column in NHANES.items():
            coef, stats = fit(child.BMPHT, child[column], adult.BMPHT, adult[column])
            models[(sex,dim)] = coef
            report.append(dict(sex=sex, dimension=dim, source='NHANES III child + adult',
                               proxy_factor=1.0, **stats))
        # These substitutions deliberately join distinct landmarks; none of the
        # converted paediatric values is a direct measurement of the adult
        # target. Each factor is computed only from paired ANSUR adult records.
        head_mean = 22.3 if sex == 'male' else 21.2  # Korean 20–24 group vertex–menton means, cm
        factors = {
            'head': head_mean/(army.headcircumference.mean()/10),
            'neck': ((army.stature-army.tragiontopofhead-army.cervicaleheight)/army.headcircumference).mean(),
            'torso': ((army.cervicaleheight-army.crotchheight)/army.sittingheight).mean(),
            'arm': ((army.sleeveoutseam+army.handlength)/army.acromionradialelength).mean(),
            'leg': (army.trochanterionheight/army.subischialheight).mean(),
        }
        pediatric = {'head':'BMPHEAD', 'neck':'BMPHEAD', 'torso':'BMPSITHT',
                     'arm':'BMPARML', 'leg':'subischial_cm'}
        adult_values = {
            'head': army.headcircumference/10*factors['head'],
            'neck': (army.stature-army.tragiontopofhead-army.cervicaleheight)/10,
            'torso': (army.cervicaleheight-army.crotchheight)/10,
            'arm': (army.sleeveoutseam+army.handlength)/10,
            'leg': army.trochanterionheight/10,
        }
        for dim in DIMENSIONS[5:]:
            coef, stats = fit(child.BMPHT, child[pediatric[dim]]*factors[dim],
                              army.stature/10, adult_values[dim])
            models[(sex,dim)] = coef
            report.append(dict(sex=sex, dimension=dim, source='NHANES child proxy + ANSUR adult',
                               proxy_factor=factors[dim], **stats))
    rows = ['// Generated by tools/skeleton-source/openskeleton/26-pooled-height-models.py; do not edit.',
            '#pragma once', '', 'namespace gtc::native::pooled_height_data {',
            '// Cubic Bernstein coefficients, domain 50–250 cm; rows male then female.',
            'inline constexpr double coefficients[2][10][4] = {']
    for sex in ('male','female'):
        rows.append('    { // '+sex)
        for dim in DIMENSIONS:
            rows.append('        {'+', '.join(format(x,'.15g') for x in models[(sex,dim)])+'}, // '+dim)
        rows.append('    },')
    rows += ['};', '} // namespace gtc::native::pooled_height_data', '']
    header = '\n'.join(rows)
    frame = pd.DataFrame(report)
    tsv = frame.to_csv(sep='\t', index=False, float_format='%.9f', lineterminator='\n')
    outputs = {ROOT/'native/client/pooled_height_coefficients.h':header,
               DATA/'POOLED_HEIGHT_MODELS.tsv':tsv}
    for path, content in outputs.items():
        if '--check' in sys.argv:
            if not path.exists() or not equivalent_generated(path.read_text(), content):
                raise SystemExit(f'generated file differs: {path}')
        else:
            path.write_text(content)
            print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
