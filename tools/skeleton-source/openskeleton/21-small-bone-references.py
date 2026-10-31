#!/usr/bin/env python3
"""Transcribe source tables for direct skeletal measurements; no individual records are inferred."""
import csv
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / 'anthropometry'

# El Morsi & Al Hawary, Table 2: mean, sample SD in mm; digits 1..5,
# except middle phalanx 1, which does not exist. Rows ordered right M/F, left M/F.
HAND = {
    'metacarpal': [
        [(48.44,2.74),(71.58,3.93),(68.62,3.72),(60.66,3.35),(56.72,3.35)],
        [(44.64,2.97),(65.34,3.43),(62.28,3.47),(54.44,3.11),(51.56,3.16)],
        [(49.02,2.86),(71.98,3.80),(68.76,3.72),(60.88,3.52),(56.46,3.22)],
        [(44.60,2.72),(65.30,3.18),(62.18,3.29),(54.68,2.75),(51.36,3.04)]],
    'proximal_phalanx': [
        [(32.96,2.24),(41.58,3.03),(46.72,2.77),(43.70,2.71),(34.90,2.26)],
        [(28.98,2.06),(38.92,2.97),(42.76,2.59),(40.44,2.13),(32.12,1.97)],
        [(33.00,2.18),(41.78,2.67),(46.98,2.79),(44.02,2.72),(34.94,2.23)],
        [(28.98,1.93),(38.72,2.15),(42.94,2.60),(40.34,2.03),(31.96,2.19)]],
    'middle_phalanx': [
        [None,(24.28,2.14),(29.82,2.33),(28.58,1.99),(20.62,1.73)],
        [None,(22.64,1.71),(27.20,2.71),(26.58,2.51),(18.52,2.36)],
        [None,(24.24,2.01),(29.80,2.26),(28.56,1.96),(20.58,1.83)],
        [None,(22.62,1.66),(27.12,2.08),(26.44,2.33),(18.66,1.74)]],
    'distal_phalanx': [
        [(23.14,1.81),(17.92,1.58),(19.26,1.81),(20.04,2.19),(17.80,1.52)],
        [(20.32,1.46),(16.44,1.92),(17.26,2.02),(17.78,1.68),(15.72,1.42)],
        [(22.68,2.10),(18.08,1.58),(19.26,1.71),(19.82,1.74),(17.80,1.57)],
        [(20.28,1.48),(16.34,1.43),(17.38,1.52),(17.90,1.37),(15.76,1.22)]]}

# Patil et al., tables 2–3: mean, SD in mm, sex M/F. Both feet were
# radiographed but the table does not state how bilateral data were combined.
FOOT = {
    'metatarsal': [[(56.42,4.41),(67.61,3.49),(65.88,4.41),(66.52,3.98),(64.32,3.57)],
                   [(50.09,3.06),(62.46,3.45),(60.92,2.99),(60.78,2.91),(58.34,2.90)]],
    'proximal_phalanx': [[(28.0,2.40),(26.3,2.08),(24.6,1.77),(22.8,1.67),(21.3,1.54)],
                         [(25.8,2.21),(24.2,1.62),(22.6,1.51),(20.9,1.44),(19.3,1.31)]],
    'middle_phalanx': [[None,(11.1,1.81),(9.7,2.07),(7.6,1.92),(5.8,0.97)],
                       [None,(9.3,2.06),(7.4,2.17),(5.8,1.44),(5.1,0.81)]],
    'distal_phalanx': [[(22.5,1.85),(6.9,1.41),(6.3,1.51),(5.6,1.44),(5.2,1.33)],
                       [(20.8,1.90),(6.9,1.32),(6.3,1.25),(5.3,1.23),(4.4,1.03)]]}

# Patil et al. Tables 4–7: pairwise correlations within each group,
# order (1,2),(1,3),...,(4,5), or the 2–5 combinations for middle phalanges.
FOOT_R = {
    'metatarsal': [[.631,.572,.600,.528,.850,.843,.742,.856,.802,.834],
                   [.739,.691,.665,.624,.903,.877,.820,.872,.815,.834]],
    'proximal_phalanx': [[.851,.751,.776,.733,.916,.912,.801,.934,.775,.821],
                         [.772,.717,.678,.614,.931,.798,.629,.918,.695,.824]],
    'middle_phalanx': [[.784,.635,.390,.806,.563,.634],
                       [.819,.746,.571,.778,.519,.774]],
    'distal_phalanx': [[.480,.471,.433,.282,.929,.871,.711,.871,.679,.822],
                       [.465,.592,.460,.440,.864,.712,.737,.885,.826,.853]]}

# 2025 CT study table 2 (mean height, mm); table 4 means of individual
# anterior/posterior (Ha/Hp) and central/posterior (Hc/Hp) ratios.
SPINE = [
('T1',15.69,14.35,.93,.94,.86,.86),('T2',17.11,15.56,.94,.96,.87,.84),
('T3',17.44,16.01,.96,.99,.87,.88),('T4',18.28,16.83,.96,.94,.87,.87),
('T5',19.14,17.61,.97,.98,.86,.85),('T6',19.49,17.89,.92,.93,.85,.83),
('T7',19.38,17.91,.92,.92,.86,.85),('T8',19.22,18.01,.97,.97,.90,.89),
('T9',20.47,18.89,.94,.96,.89,.87),('T10',22.13,20.36,.94,.94,.89,.85),
('T11',23.24,21.76,.88,.91,.86,.85),('T12',24.94,23.63,.87,.92,.87,.86),
('L1',26.84,25.30,.89,.93,.87,.88),('L2',27.26,26.20,.94,.97,.88,.88),
('L3',27.62,26.36,1.00,1.04,.89,.91),('L4',27.13,26.03,1.03,1.08,.94,.95),
('L5',26.13,25.03,1.20,1.19,1.01,1.01)]


def write(name, header, rows):
    with (OUT / name).open('w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f, delimiter='\t', lineterminator='\n')
        writer.writerow(header)
        writer.writerows(rows)


def main():
    rows = []
    for bone, groups in HAND.items():
        for (side, sex), values in zip([('R','male'),('R','female'),('L','male'),('L','female')], groups):
            for digit, pair in enumerate(values, 1):
                if pair is not None:
                    rows.append((bone,digit,side,sex,50,*pair))
    write('HAND_EGYPTIAN_XRAY_LENGTHS.tsv',
          ['bone','digit','side','sex','subjects_n','mean_length_mm','sd_length_mm'], rows)
    rows = []
    for bone, groups in FOOT.items():
        for sex, n, values in zip(['male','female'], [33,27], groups):
            for digit, pair in enumerate(values, 1):
                if pair is not None:
                    rows.append((bone,digit,'unspecified_bilateral_summary',sex,n,*pair))
    write('FOOT_INDIAN_XRAY_LENGTHS.tsv',
          ['bone','digit','side','sex','subjects_n','mean_length_mm','sd_length_mm'], rows)
    rows = []
    for bone, groups in FOOT_R.items():
        digits = range(2 if bone == 'middle_phalanx' else 1, 6)
        pairs = [(a,b) for a in digits for b in digits if a < b]
        for sex, n, values in zip(['male','female'], [33,27], groups):
            rows.extend((bone,a,b,sex,n,f'{r:.3f}') for (a,b),r in zip(pairs,values))
    write('FOOT_INDIAN_XRAY_CORRELATIONS.tsv',
          ['bone_group','digit_a','digit_b','sex','subjects_n','published_r'], rows)
    rows = []
    for level, mh, fh, mah, fah, mch, fch in SPINE:
        for sex, n, h, ah, ch in [('male',129,mh,mah,mch),('female',133,fh,fah,fch)]:
            rows.append((level,sex,n,f'{h:.2f}',f'{ah:.2f}',f'{ch:.2f}'))
    write('SPINE_ADULT_CT_LEVELS.tsv',
          ['vertebra','sex','cohort_n_not_level_n','published_mean_height_mm',
           'mean_individual_anterior_over_posterior','mean_individual_central_over_posterior'],rows)


if __name__ == '__main__':
    main()
