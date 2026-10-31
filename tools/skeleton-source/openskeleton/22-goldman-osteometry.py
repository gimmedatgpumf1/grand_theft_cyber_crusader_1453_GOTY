#!/usr/bin/env python3
"""Analyze paired long-bone measurements from an externally supplied Goldman CSV.

Input is the unmodified, pinned geanes/bioanth mirror; no subject-level records
are emitted. Anatomical and sampling limitations are in GOLDMAN_RESEARCH.md.
"""
import argparse
import hashlib
from pathlib import Path

import numpy as np
import pandas as pd

OUT = Path(__file__).resolve().parents[1] / 'anthropometry'
SHA = 'bc566cb3105330108ebae249bb778f8b4c888eb05b70b47cefeaa7aa1a87cb45'
BONES = ('HML', 'RML', 'FML', 'TML')
RATIOS = {
    'humerus / radius': (('HML',), ('RML',)),
    'radius / humerus': (('RML',), ('HML',)),
    'femur / tibia': (('FML',), ('TML',)),
    'tibia / femur': (('TML',), ('FML',)),
    'humerus / femur': (('HML',), ('FML',)),
    '(humerus + radius) / (femur + tibia)': (('HML','RML'), ('FML','TML')),
    'bi-iliac breadth / femur': (('BIB',), ('FML',)),
}


def col(side, x):
    return x if x == 'BIB' else side + x


def get(df, side, keys):
    return df[[col(side, k) for k in keys]].sum(axis=1, min_count=len(keys))


def quality_mask(df, keys):
    # A manual review flag, not a diagnostic threshold. Only check requested
    # variables for a ratio/fit; a missing opposite side is not a failure.
    mask = pd.Series(True, index=df.index)
    for key in set(keys) - {'BIB'}:
        left, right = df['L'+key], df['R'+key]
        mask &= (left - right).abs().le(20) | left.isna() | right.isna()
    return mask


def stats(data):
    return dict(n=len(data), mean=data.mean(), sd=data.std(ddof=1),
                p05=data.quantile(.05), median=data.median(), p95=data.quantile(.95))


def output(df, name):
    df.to_csv(OUT / name, sep='\t', index=False, float_format='%.6f', lineterminator='\n')


def main(path):
    if hashlib.sha256(path.read_bytes()).hexdigest() != SHA:
        raise ValueError('Goldman mirror CSV differs from the pinned SHA-256')
    d = pd.read_csv(path, encoding='latin-1', dtype={'Sex': str}, low_memory=False)
    if len(d) != 1538:
        raise ValueError('Unexpected input row count')
    for name in [s+b for s in 'LR' for b in BONES] + ['BIB']:
        d[name] = pd.to_numeric(d[name], errors='coerce')
        d.loc[d[name] <= 0, name] = np.nan
    d = d[d.Sex.isin(['0','1'])].copy()  # uncertain 0?/1? are excluded
    d['sex'] = d.Sex.map({'0':'male', '1':'female'})
    if d['sex'].isna().any():
        raise ValueError('unexpected sex code')
    summaries, bins, populations, fits = [], [], [], []
    for sex in ('male','female'):
        sexdata = d[d.sex == sex]
        for side in ('L','R'):
            fem = sexdata[side+'FML']
            for name, (num, den) in RATIOS.items():
                y, x = get(sexdata, side, num), get(sexdata, side, den)
                valid = x.notna() & y.notna() & (x > 0) & (y > 0)
                ratio = y[valid] / x[valid]
                keys = num + den
                for quality in ('all', 'review_flag_excluded'):
                    keep = valid if quality == 'all' else valid & quality_mask(sexdata, keys)
                    values = y[keep] / x[keep]
                    if len(values) < 2:
                        continue
                    summaries.append(dict(sex=sex,side=side,ratio=name,quality=quality,**stats(values)))
                    # Femur is an observed reference bone, not a surrogate for stature.
                    bin_keep = keep if quality == 'all' else keep & quality_mask(sexdata, ('FML',))
                    indices = (np.floor(fem[bin_keep] / 25) * 25).dropna()
                    for start, group in indices.groupby(indices):
                        v = values.loc[group.index]
                        if len(v) < 2: continue
                        bins.append(dict(sex=sex,side=side,ratio=name,quality=quality,
                                         femur_bin_start_mm=start,femur_bin_end_exclusive_mm=start+25,
                                         femur_mean_mm=fem.loc[group.index].mean(),**stats(v)))
                    # Expose different historical/geographic samples rather than only a pooled mean.
                    if quality == 'all':
                        groupings = sexdata.loc[keep, 'Location'].fillna('unknown')
                        for location, group in groupings.groupby(groupings):
                            if len(group) < 10: continue
                            v = values.loc[group.index]
                            populations.append(dict(sex=sex,side=side,ratio=name,location=location,**stats(v)))
            # Forward bone-on-femur OLS, not a stature model. Keep only same-side pairs.
            for bone in ('HML','RML','TML','BIB'):
                target=sexdata[col(side,bone)]
                valid=fem.notna() & target.notna() & (fem>0) & (target>0)
                for quality in ('all','review_flag_excluded'):
                    keep = valid if quality == 'all' else valid & quality_mask(sexdata,('FML',bone))
                    X, Y = fem[keep].to_numpy(), target[keep].to_numpy()
                    if len(X)<3 or np.std(X)==0: continue
                    slope = np.cov(X,Y,ddof=1)[0,1] / np.var(X,ddof=1)
                    intercept = Y.mean()-slope*X.mean()
                    resid = Y-(intercept+slope*X)
                    ss = ((Y-Y.mean())**2).sum()
                    fits.append(dict(sex=sex,side=side,response=bone,quality=quality,n=len(X),
                                     femur_min_mm=X.min(),femur_max_mm=X.max(),
                                     intercept_mm=intercept,slope_mm_per_mm=slope,
                                     r_squared=1-(resid@resid)/ss,
                                     residual_sd_mm=np.sqrt((resid@resid)/(len(X)-2))))
    output(pd.DataFrame(summaries),'GOLDMAN_PAIRED_RATIO_SUMMARY.tsv')
    output(pd.DataFrame(bins),'GOLDMAN_RATIOS_BY_FEMUR_BIN.tsv')
    output(pd.DataFrame(populations),'GOLDMAN_RATIOS_BY_LOCATION.tsv')
    output(pd.DataFrame(fits),'GOLDMAN_BONE_ON_FEMUR_FITS.tsv')


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('goldman_csv', type=Path)
    main(p.parse_args().goldman_csv)
