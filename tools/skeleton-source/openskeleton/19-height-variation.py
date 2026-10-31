#!/usr/bin/env python3
"""Compare measured dimensions against stature, without changing game code."""
from pathlib import Path
import argparse
import numpy as np
import pandas as pd
from scipy.stats import linregress

ROOT = Path(__file__).resolve().parents[1] / 'anthropometry'


def army_stability():
    a = pd.read_csv(ROOT / 'ANSUR_II_PAIRED_MM.csv')
    excluded = {'SubjectId', 'Gender', 'Age', 'stature'}
    dimensions = [c for c in a.columns if c not in excluded]
    rows = []
    for sex in ('Male', 'Female'):
        d = a[(a.Gender == sex) & (a.Age >= 18)]
        for dim in dimensions:
            good = d[['stature', dim]].dropna()
            h = good.stature.to_numpy() / 10
            y = good[dim].to_numpy() / 10
            fit = linregress(h, y)
            y160, y175 = fit.intercept + fit.slope * np.array([160., 175.])
            if y160 <= 0 or y175 <= 0:
                continue
            resid = y - (fit.intercept + fit.slope * h)
            rows.append(dict(sex=sex, dimension=dim, n=len(h), height_min_cm=h.min(),
                             height_max_cm=h.max(), intercept_cm=fit.intercept,
                             slope_cm_per_cm=fit.slope, fitted_at_160_cm=y160,
                             fitted_at_175_cm=y175, delta_cm=y175-y160,
                             relative_change_percent=100*(y175/y160-1),
                             elasticity_at_167_5=fit.slope*167.5/(fit.intercept+fit.slope*167.5),
                             ratio_at_160=y160/160, ratio_at_175=y175/175,
                             r_squared=fit.rvalue**2, residual_sd_cm=np.sqrt((resid@resid)/(len(h)-2))))
    out = pd.DataFrame(rows).sort_values(['sex', 'relative_change_percent', 'dimension'])
    out.to_csv(ROOT / 'ANSUR_ABSOLUTE_SIZE_CHANGE.tsv', sep='\t', index=False,
               float_format='%.6f', lineterminator='\n')


def illustrative_grid():
    models = pd.read_csv(ROOT / 'HEIGHT_FORWARD_REGRESSIONS.tsv', sep='\t')
    rows = []
    for _, m in models.iterrows():
        for h in range(100, 251, 5):
            y = m.intercept_cm + m.slope*h
            rows.append(dict(source=m.source, sex=m.sex, dimension=m.dimension, height_cm=h,
                             fitted_dimension_cm=y, fitted_dimension_over_height=y/h,
                             observed_min_cm=m.height_min_cm, observed_max_cm=m.height_max_cm,
                             outside_observed_range=int(h < m.height_min_cm or h > m.height_max_cm),
                             nonpositive_prediction=int(y <= 0)))
    pd.DataFrame(rows).to_csv(ROOT / 'ILLUSTRATIVE_MODEL_GRID_100_TO_250_CM.tsv', sep='\t',
                              index=False, float_format='%.6f', lineterminator='\n')


def nhanes_bins():
    a = pd.read_csv(ROOT / 'NHANES_2017_2018_PAIRED_CM.csv')
    rows = []
    for sex, code in [('Male', 1), ('Female', 2)]:
        for group in ('all_ages', 'under_18', '18_plus'):
            s = a[(a.RIAGENDR == code) & a.BMIHT.isna()].copy()
            if group == 'under_18':
                s = s[s.RIDAGEYR < 18]
            elif group == '18_plus':
                s = s[s.RIDAGEYR >= 18]
            for dim, flag in [('BMXARML', 'BMIARML'), ('BMXLEG', 'BMILEG')]:
                d = s[s[flag].isna() & s[dim].notna()].copy()
                d['bin_cm'] = np.floor(d.BMXHT / 5)*5
                for start, g in d.groupby('bin_cm'):
                    rows.append(dict(sex=sex, age_group=group, dimension=dim,
                                     height_bin_start_cm=start, height_bin_end_exclusive_cm=start+5,
                                     n=len(g), height_mean_cm=g.BMXHT.mean(),
                                     age_mean_years=g.RIDAGEYR.mean(),
                                     dimension_mean_cm=g[dim].mean(), dimension_sd_cm=g[dim].std(),
                                     ratio_mean=(g[dim]/g.BMXHT).mean(),
                                     ratio_sd=(g[dim]/g.BMXHT).std()))
    pd.DataFrame(rows).to_csv(ROOT / 'NHANES_ALL_AGES_HEIGHT_BINS.tsv', sep='\t', index=False,
                              float_format='%.6f', na_rep='NA', lineterminator='\n')


def korean_head(path):
    # This is a third-party unverified Korean-language compilation, NOT claimed to
    # be the Yeo et al. clinical dataset. Preserve parse failures as missing.
    raw = pd.read_csv(path, encoding='cp949')
    rows, bins = [], []
    for sex, code in [('Male','M'), ('Female','F')]:
        s = raw[raw['성별'] == code].copy()
        for c in ['키', '머리수직길이', '나이']:
            s[c] = pd.to_numeric(s[c], errors='coerce')
        s = s.dropna(subset=['키', '머리수직길이'])
        x, y = s['키'].to_numpy(), s['머리수직길이'].to_numpy()
        fit = linregress(x, y)
        residual = y - (fit.intercept + fit.slope * x)
        rows.append(dict(sex=sex, n=len(x), stature_min_cm=x.min(), stature_max_cm=x.max(),
                         age_min=s['나이'].min(), age_max=s['나이'].max(),
                         head_min_cm=y.min(), head_max_cm=y.max(), head_mean_cm=y.mean(),
                         intercept_cm=fit.intercept, slope_cm_per_cm=fit.slope,
                         slope_se=fit.stderr, r_squared=fit.rvalue**2,
                         residual_sd_cm=np.sqrt((residual@residual)/(len(x)-2))))
        s['bin_cm'] = np.floor(s['키'] / 5)*5
        for start, g in s.groupby('bin_cm'):
            bins.append(dict(sex=sex, height_bin_start_cm=start, height_bin_end_exclusive_cm=start+5,
                             n=len(g), stature_mean_cm=g['키'].mean(), age_mean=g['나이'].mean(),
                             head_mean_cm=g['머리수직길이'].mean(), head_sd_cm=g['머리수직길이'].std()))
    for name, data in [('EXPLORATORY_KOREAN_HEAD_FITS.tsv', rows),
                       ('EXPLORATORY_KOREAN_HEAD_BINS.tsv', bins)]:
        pd.DataFrame(data).to_csv(ROOT / name, sep='\t', index=False,
                                  float_format='%.6f', na_rep='NA', lineterminator='\n')


def head_study_table():
    # Yeo et al. Table 2, values and SD in mm; age-group counts from Table 1.
    table = {
        'head_height_vertex_menton': [(194,27),(211,28),(223,27),(190,24),(205,25),(212,25)],
        'upper_head_vertex_sellion': [(101,17),(103,16),(109,16),(100,16),(105,15),(107,15)],
        'lower_head_sellion_menton': [(93,12),(108,13),(114,14),(90,10),(100,12),(106,13)],
        'face_height_trichion_menton': [(164,24),(179,24),(188,24),(160,21),(170,23),(176,21)],
        'upper_face_trichion_sellion': [(71,18),(71,19),(74,19),(70,16),(70,17),(70,17)],
        'midface_sellion_stomion': [(59,8),(67,9),(72,9),(56,6),(62,7),(67,7)],
        'lower_face_stomion_menton': [(34,7),(41,9),(42,9),(34,6),(38,7),(39,7)],
        'head_length_glabella_inion': [(164,21),(174,20),(183,20),(162,18),(169,19),(173,19)],
        'face_length_rhinion_tragion': [(76,13),(85,12),(92,11),(74,11),(81,10),(85,10)],
        'head_breadth_euryon_euryon': [(141,18),(149,18),(157,18),(136,16),(142,17),(147,16)],
        'face_breadth_zygion_zygion': [(128,15),(139,17),(147,15),(124,13),(133,15),(138,15)],
    }
    cohorts = [('Male','8-10',158),('Male','14-16',209),('Male','20-24',260),
               ('Female','8-10',152),('Female','14-16',215),('Female','20-24',261)]
    rows = []
    for dimension, values in table.items():
        for (sex, age, n), (mean, sd) in zip(cohorts, values):
            rows.append(dict(dimension=dimension, sex=sex, age_years=age, n=n,
                             mean_mm=mean, sd_mm=sd))
    pd.DataFrame(rows).to_csv(ROOT / 'HEAD_AGE_STUDY_SUMMARY.tsv', sep='\t', index=False,
                              lineterminator='\n')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--exploratory-korean-csv', type=Path)
    args = p.parse_args()
    army_stability()
    illustrative_grid()
    nhanes_bins()
    head_study_table()
    if args.exploratory_korean_csv:
        korean_head(args.exploratory_korean_csv)


if __name__ == '__main__':
    main()
