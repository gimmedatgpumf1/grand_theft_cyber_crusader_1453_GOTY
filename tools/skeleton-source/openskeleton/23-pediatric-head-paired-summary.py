#!/usr/bin/env python3
"""Transcribe Domljan et al. (2024) Table 5, original ratio direction."""
import csv
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / 'anthropometry/PEDIATRIC_PAIRED_HEAD_BODY_RATIOS.tsv'
# age_lower, male n/mean/sd/min/max, female n/mean/sd/min/max
ROWS = [
    (2,  (9,5.77,.21,5.50,6.11), (10,5.54,.24,5.16,5.80)),
    (3, (64,5.76,.25,5.19,6.31), (62,5.93,.28,5.27,6.43)),
    (4, (94,5.93,.31,5.25,6.77), (108,6.06,.32,5.47,6.79)),
    (5, (112,6.10,.32,5.45,6.94), (88,6.26,.35,5.33,7.03)),
    (6, (82,6.25,.25,5.66,6.73), (81,6.47,.29,5.83,6.97)),
    (7, (33,6.26,.41,5.17,6.99), (22,6.51,.39,5.86,7.42)),
    (8, (30,6.48,.44,5.67,7.32), (25,6.30,.32,5.65,7.05)),
    (9, (41,6.56,.48,5.59,7.72), (26,6.74,.53,5.84,7.68)),
    (10,(30,6.61,.42,5.92,7.35), (55,6.99,.50,5.79,8.06)),
    (11,(36,6.96,.46,6.00,8.05), (38,7.22,.44,6.22,8.32)),
    (12,(37,7.39,.67,6.32,8.95), (29,7.50,.53,6.64,8.62)),
    (13,(34,7.45,.45,6.60,8.29), (36,7.53,.41,6.59,8.60)),
    (14,(38,7.58,.51,6.76,8.79), (24,7.69,.53,6.83,8.90)),
]

def main():
    with OUT.open('w', encoding='utf-8', newline='') as f:
        w=csv.writer(f, delimiter='\t', lineterminator='\n')
        w.writerow(['age_min_inclusive_years','age_max_exclusive_years','sex','n',
                    'mean_stature_over_head_height','sd_stature_over_head_height',
                    'minimum_stature_over_head_height','maximum_stature_over_head_height'])
        for age, male, female in ROWS:
            for sex, values in [('male', male), ('female', female)]:
                w.writerow([age,age+1,sex,*values])

if __name__ == '__main__':
    main()
