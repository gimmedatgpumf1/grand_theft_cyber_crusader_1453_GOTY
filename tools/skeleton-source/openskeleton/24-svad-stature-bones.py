#!/usr/bin/env python3
"""Aggregate paired subadult CT bone measurements against known stature.

Reads a pinned author-supplied RDS; does not emit individual-level records.
See SVAD_BONE_STATURE_RESEARCH.md for provenance and critical limitations.
"""
import argparse
import hashlib
from pathlib import Path

import numpy as np
import pandas as pd
import pyreadr

OUT = Path(__file__).resolve().parents[1] / 'anthropometry'
SHA = 'a79fc3d3deee9a3dac2681101e98a73a68c2888c8f0db1b7b504488e77b11c9b'
DIMS = {
    'FDL': 'femur_length', 'FMSB': 'femur_midshaft_breadth', 'FDB': 'femur_distal_breadth',
    'TDL': 'tibia_length', 'TPB': 'tibia_proximal_breadth', 'TMSB': 'tibia_midshaft_breadth',
    'TDB': 'tibia_distal_breadth', 'FBDL': 'fibula_length', 'HDL': 'humerus_length',
    'HPB': 'humerus_proximal_breadth', 'HMSB': 'humerus_midshaft_breadth',
    'HDB': 'humerus_distal_breadth', 'RDL': 'radius_length', 'RPB': 'radius_proximal_breadth',
    'RMSB': 'radius_midshaft_breadth', 'RDB': 'radius_distal_breadth', 'UDL': 'ulna_length',
    'UMSB': 'ulna_midshaft_breadth',
}
AGES = {'under_2': (0,2), '2_to_under_12': (2,12), '12_to_under_18': (12,18),
        '2_to_under_18': (2,18), '18_to_under_21': (18,21)}


def sd(values):
    return values.std(ddof=1) if len(values) > 1 else np.nan


def main(path):
    if hashlib.sha256(path.read_bytes()).hexdigest() != SHA:
        raise ValueError('Unexpected RDS input SHA-256; verify upstream version')
    raw = pyreadr.read_r(str(path))[None]
    if len(raw) != 1026:
        raise ValueError('Unexpected RDS row count')
    # Both duplicate records in this pinned file are exact repeats; refuse to
    # guess if a future file contains conflicting records with the same ID.
    exact = raw.drop_duplicates()
    if exact.SVAD_medrec.duplicated().any():
        raise ValueError('Conflicting duplicate identifiers; manual review needed')
    df = exact[exact.SEX.isin(['M','F']) & exact.stature.notna() & (exact.stature > 0)].copy()
    df['stature_cm'] = df.stature / 10
    for dim in DIMS:
        df.loc[df[dim] <= 0, dim] = np.nan
        df[dim] = df[dim] / 10  # RDS all bone dimensions in mm
    rows, fits = [], []
    for sex_code, sex in [('M','male'),('F','female')]:
        for agename, (amin, amax) in AGES.items():
            group = df[(df.SEX == sex_code) & df.agey.ge(amin) & df.agey.lt(amax)]
            for code, dimension in DIMS.items():
                data = group[['stature_cm','agey',code]].dropna()
                if len(data) >= 10 and data.stature_cm.nunique() >= 2:
                    x=data.stature_cm.to_numpy(); y=data[code].to_numpy()
                    slope=np.cov(x,y,ddof=1)[0,1]/np.var(x,ddof=1)
                    intercept=y.mean()-slope*x.mean()
                    residual=y-(intercept+slope*x)
                    ss=((y-y.mean())**2).sum()
                    fits.append(dict(sex=sex,age_group=agename,source_column=code,dimension=dimension,n=len(x),
                        stature_min_cm=x.min(),stature_max_cm=x.max(),mean_age_years=data.agey.mean(),
                        intercept_cm=intercept,slope_cm_per_cm=slope,
                        r_squared=1-residual@residual/ss if ss else np.nan,
                        residual_sd_cm=np.sqrt((residual@residual)/(len(x)-2))))
                for width in (5,10):
                    if data.empty: continue
                    bins=np.floor(data.stature_cm / width) * width
                    for start, index in bins.groupby(bins).groups.items():
                        cell=data.loc[index]; values=cell[code]; ratios=values/cell.stature_cm
                        rows.append(dict(sex=sex,age_group=agename,source_column=code,dimension=dimension,
                            bin_width_cm=width,bin_start_cm=start,bin_end_exclusive_cm=start+width,n=len(cell),
                            stature_mean_cm=cell.stature_cm.mean(),age_mean_years=cell.agey.mean(),
                            dimension_mean_cm=values.mean(),dimension_sd_cm=sd(values),
                            ratio_mean=ratios.mean(),ratio_sd=sd(ratios)))
    pd.DataFrame(rows).to_csv(OUT/'SVAD_BONE_STATURE_BINS.tsv',sep='\t',index=False,
                             float_format='%.6f',lineterminator='\n')
    pd.DataFrame(fits).to_csv(OUT/'SVAD_BONE_ON_STATURE_FITS.tsv',sep='\t',index=False,
                             float_format='%.6f',lineterminator='\n')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input_rds',type=Path)
    main(parser.parse_args().input_rds)
