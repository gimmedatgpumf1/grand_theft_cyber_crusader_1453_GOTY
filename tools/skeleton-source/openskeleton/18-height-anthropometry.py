#!/usr/bin/env python3
"""Extract public paired measurements and fit forward sex-specific OLS models.

See anthropometry/README.md for acquisition, provenance, units and exclusions.
"""
import argparse
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.stats import t

ROOT = Path(__file__).resolve().parents[1] / 'anthropometry'
ANSUR_MODELS = [
    'sittingheight', 'subischialheight', 'acromionradialelength',
    'radialestylionlength', 'shoulderelbowlength', 'forearmhandlength',
    'span', 'handlength', 'palmlength', 'headlength', 'headbreadth',
    'headcircumference', 'tragiontopofhead', 'mentonsellionlength',
    'biacromialbreadth', 'bideltoidbreadth', 'hipbreadth', 'waistbacklength',
    'waistfrontlengthsitting', 'crotchheight', 'functionalleglength',
    'trochanterionheight', 'tibialheight', 'lateralmalleolusheight',
    'trochanter_to_tibiale_vertical', 'tibiale_to_malleolus_vertical',
    'footlength', 'footbreadthhorizontal', 'acromialheight', 'cervicaleheight',
    'waistheightomphalion', 'vertex_to_cervicale_vertical', 'eyeheightsitting',
    'vertex_to_eye_sitting', 'shoulderlength', 'kneeheightmidpatella',
]


def export_ansur(input_dir):
    data = []
    for sex in ('FEMALE', 'MALE'):
        frame = pd.read_csv(input_dir / f'ANSUR II {sex} Public.csv', encoding='latin1')
        frame = frame.rename(columns={'subjectid': 'SubjectId'})
        # The 93 directly measured variables are consecutive in the source file.
        fields = list(frame.columns[1:94])
        assert len(fields) == 93 and 'stature' in fields and 'weightkg' in fields
        # Weight is in hectograms in the public CSV, despite its name. Omit it.
        fields.remove('weightkg')
        frame = frame[['SubjectId', 'Gender', 'Age'] + fields].copy()
        # The public release stores interpupillary breadth in tenths of a mm.
        frame['interpupillarybreadth'] = frame.interpupillarybreadth / 10
        assert (frame.Gender.str.upper() == sex).all()
        frame.insert(3, 'subischialheight', frame.stature - frame.sittingheight)
        frame.insert(4, 'trochanter_to_tibiale_vertical', frame.trochanterionheight - frame.tibialheight)
        frame.insert(5, 'tibiale_to_malleolus_vertical', frame.tibialheight - frame.lateralmalleolusheight)
        frame.insert(6, 'vertex_to_cervicale_vertical', frame.stature - frame.cervicaleheight)
        frame.insert(7, 'vertex_to_eye_sitting', frame.sittingheight - frame.eyeheightsitting)
        data.append(frame)
    frame = pd.concat(data, ignore_index=True)
    frame.to_csv(ROOT / 'ANSUR_II_PAIRED_MM.csv', index=False, lineterminator='\n')
    return frame


def export_nhanes(input_dir):
    demo_cols = ['SEQN', 'RIAGENDR', 'RIDAGEYR', 'RIDEXAGM', 'WTMEC2YR', 'SDMVSTRA', 'SDMVPSU']
    exam_cols = ['SEQN', 'BMXHT', 'BMIHT', 'BMXARML', 'BMIARML', 'BMXLEG', 'BMILEG']
    demo = pd.read_csv(input_dir / 'demographics.csv', usecols=demo_cols)
    exam = pd.read_csv(input_dir / 'examination.csv', usecols=exam_cols, low_memory=False)
    frame = demo.merge(exam, on='SEQN', validate='one_to_one')
    frame = frame[(frame.RIDAGEYR >= 2) & frame.BMXHT.notna() &
                  (frame.BMXARML.notna() | frame.BMXLEG.notna())].copy()
    frame.to_csv(ROOT / 'NHANES_2017_2018_PAIRED_CM.csv', index=False,
                 lineterminator='\n', float_format='%.6f')
    return frame


def fit(x, y, source, sex, name):
    good = np.isfinite(x) & np.isfinite(y) & (x > 0) & (y > 0)
    x, y = x[good], y[good]
    n = len(x)
    meanx, meany = x.mean(), y.mean()
    sxx = np.sum((x - meanx) ** 2)
    slope = np.sum((x - meanx) * (y - meany)) / sxx
    intercept = meany - slope * meanx
    resid = y - (intercept + slope * x)
    rmse = np.sqrt(np.sum(resid**2) / (n - 2))
    slope_se = rmse / np.sqrt(sxx)
    margin = t.ppf(.975, n - 2) * slope_se
    sst = np.sum((y - meany)**2)
    ratios = y / x
    return dict(source=source, sex=sex, dimension=name, n=n,
                height_min_cm=x.min(), height_max_cm=x.max(), height_mean_cm=meanx,
                dimension_mean_cm=meany, intercept_cm=intercept, slope=slope,
                slope_ci95_low=slope-margin, slope_ci95_high=slope+margin,
                r_squared=1 - np.sum(resid**2) / sst, residual_sd_cm=rmse,
                residual_p025_cm=np.quantile(resid,.025), residual_p975_cm=np.quantile(resid,.975),
                ratio_mean=ratios.mean(), ratio_sd=ratios.std(ddof=1))


def make_models(ansur, nhanes):
    records = []
    for sex in ('Male', 'Female'):
        a = ansur[(ansur.Gender == sex) & (ansur.Age >= 18)]
        for dimension in ANSUR_MODELS:
            records.append(fit(a.stature.to_numpy(float) / 10,
                               a[dimension].to_numpy(float) / 10, 'ANSUR_II', sex, dimension))
        h = nhanes[(nhanes.RIAGENDR == (1 if sex == 'Male' else 2)) &
                   (nhanes.RIDAGEYR >= 18)]
        for dimension, flag in [('BMXARML','BMIARML'), ('BMXLEG','BMILEG')]:
            # Blank means no measurement comment; retain flagged data in raw CSV only.
            clean = h[h.BMIHT.isna() & h[flag].isna()]
            records.append(fit(clean.BMXHT.to_numpy(float), clean[dimension].to_numpy(float),
                               'NHANES_2017_2018', sex, dimension))
    pd.DataFrame(records).to_csv(ROOT / 'HEIGHT_FORWARD_REGRESSIONS.tsv', sep='\t',
                                 index=False, lineterminator='\n', float_format='%.6f')


def write_ratios(ansur, nhanes):
    pairs = [('sittingheight', 'stature'), ('subischialheight', 'stature'),
             ('acromionradialelength', 'stature'), ('radialestylionlength', 'stature'),
             ('acromionradialelength', 'radialestylionlength'), ('span', 'stature'),
             ('footlength', 'stature'), ('handlength', 'stature'),
             ('headlength', 'stature'), ('headbreadth', 'headlength'),
             ('tragiontopofhead', 'stature'),
             ('trochanter_to_tibiale_vertical', 'tibiale_to_malleolus_vertical')]
    records = []
    for sex in ('Male', 'Female'):
        a = ansur[(ansur.Gender == sex) & (ansur.Age >= 18)]
        h = nhanes[(nhanes.RIAGENDR == (1 if sex == 'Male' else 2)) &
                   (nhanes.RIDAGEYR >= 18)]
        for source, frame, xname, num, den in (
                [('ANSUR_II', a, 'stature', x, y) for x, y in pairs] +
                [('NHANES_2017_2018', h[h.BMIHT.isna() & h.BMIARML.isna() & h.BMILEG.isna()],
                  'BMXHT', 'BMXARML', 'BMXLEG')]):
            valid = frame[list(dict.fromkeys([xname, num, den]))].dropna()
            valid = valid[(valid[num] > 0) & (valid[den] > 0)]
            height = valid[xname].to_numpy(float) / (10 if source == 'ANSUR_II' else 1)
            ratio = (valid[num] / valid[den]).to_numpy(float)
            model = fit(height, ratio, source, sex, num + '/' + den)
            records.append(dict(source=source, sex=sex, numerator=num, denominator=den,
                                n=model['n'], height_min_cm=model['height_min_cm'],
                                height_max_cm=model['height_max_cm'], ratio_mean=ratio.mean(),
                                ratio_sd=ratio.std(ddof=1), ratio_intercept=model['intercept_cm'],
                                ratio_slope_per_cm=model['slope'], r_squared=model['r_squared'],
                                residual_sd=model['residual_sd_cm']))
    pd.DataFrame(records).to_csv(ROOT / 'PAIRED_RATIOS_VS_HEIGHT.tsv', sep='\t', index=False,
                                 lineterminator='\n', float_format='%.6f')


def write_bins(ansur):
    dimensions = ['sittingheight', 'subischialheight', 'acromionradialelength',
                  'radialestylionlength', 'span', 'headlength', 'tragiontopofhead',
                  'trochanter_to_tibiale_vertical', 'tibiale_to_malleolus_vertical', 'footlength']
    rows = []
    for sex in ('Male', 'Female'):
        frame = ansur[(ansur.Gender == sex) & (ansur.Age >= 18)].copy()
        frame['height_bin_cm'] = np.floor(frame.stature / 50) * 5
        for start, g in frame.groupby('height_bin_cm', sort=True):
            for dim in dimensions:
                y = g[dim] / 10
                x = g.stature / 10
                ratios = y / x
                rows.append(dict(sex=sex, dimension=dim, height_bin_start_cm=start,
                                 height_bin_end_exclusive_cm=start+5, n=len(g),
                                 height_mean_cm=x.mean(), dimension_mean_cm=y.mean(),
                                 dimension_sd_cm=y.std(ddof=1), ratio_mean=ratios.mean(),
                                 ratio_sd=ratios.std(ddof=1)))
    pd.DataFrame(rows).to_csv(ROOT / 'ANSUR_HEIGHT_BINS.tsv', sep='\t', index=False,
                              lineterminator='\n', float_format='%.6f', na_rep='NA')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ansur-dir', type=Path, required=True)
    parser.add_argument('--nhanes-dir', type=Path, required=True)
    args = parser.parse_args()
    ROOT.mkdir(parents=True, exist_ok=True)
    ansur = export_ansur(args.ansur_dir)
    nhanes = export_nhanes(args.nhanes_dir)
    make_models(ansur, nhanes)
    write_ratios(ansur, nhanes)
    write_bins(ansur)


if __name__ == '__main__':
    main()
