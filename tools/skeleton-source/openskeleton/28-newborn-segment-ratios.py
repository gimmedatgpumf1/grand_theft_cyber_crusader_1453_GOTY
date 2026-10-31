#!/usr/bin/env python3
"""Transcribe Chellappan 2024 Table III published individual limb/length indices."""
import csv
from pathlib import Path

OUT=Path(__file__).resolve().parents[1]/'anthropometry'/'NEWBORN_SEGMENT_RATIO_REFERENCES.tsv'
# Per sex/week: cohort n; rows below are median, Q1, Q3 of individual ratios
# in percent, NOT ratios of separately reported group means.
N={'male':[46,74,49,26], 'female':[39,89,62,23]}
DATA={
'male':{
 'total_upper_limb':[(43,42.39,43.97),(42.86,42.02,43.75),(42.86,42,44.06),(43.14,41.84,44.12)],
 'upper_arm':[(19.56,18.97,19.79),(19.18,18.82,19.59),(19,18.79,19.61),(19.38,18.94,19.61)],
 'forearm':[(15.94,15.55,16.34),(15.92,15.51,16.33),(15.88,15.47,16),(15.69,15.38,16.01)],
 'hand':[(13.54,13.33,13.97),(13.54,13.23,13.83),(13.46,13.2,13.76),(13.5,13.25,13.73)],
 'total_lower_limb':[(41.3,40,42.39),(41.09,40.22,41.67),(41.02,40.2,42),(41.18,40.29,42)],
 'thigh':[(17.71,17.35,18.02),(17.53,17.16,17.72),(17.65,17.16,18),(17.65,17.31,17.78)],
 'lower_leg':[(19.57,19.16,20),(19.49,19.01,19.79),(19.57,19,20),(19.61,19,19.65)],
 'foot':[(16.1,15.63,16.43),(15.96,15.55,16.33),(15.96,15.66,16.26),(15.88,15.63,16.33)]},
'female':{
 'total_upper_limb':[(42.55,41.67,43.7),(42.71,41.89,43.75),(42.81,42,43.81),(43.75,42.11,44.26)],
 'upper_arm':[(19.39,19,19.57),(19.39,19,19.79),(19.08,18.91,19.59),(19.39,18.78,20)],
 'forearm':[(15.68,15.31,16.13),(15.96,15.47,16.3),(15.63,15.31,16.01),(15.96,15.46,16.33)],
 'hand':[(13.67,13.4,13.96),(13.62,13.33,13.83),(13.47,13.27,13.81),(13.67,13.54,14.08)],
 'total_lower_limb':[(40.82,40.22,41.74),(41.05,40,42),(41.5,40.77,42.37),(41.24,40.82,42.86)],
 'thigh':[(17.71,17.35,18.09),(17.55,17.19,17.94),(17.71,17.34,18),(17.71,17.35,18.37)],
 'lower_leg':[(19.57,19.15,20),(19.57,19.08,19.8),(19.39,19,19.8),(19.6,19.2,20)],
 'foot':[(15.96,15.63,16.33),(15.96,15.63,16.3),(16,15.6,16.34),(16,15.88,16.33)]}}

with OUT.open('w',newline='',encoding='utf8') as f:
    w=csv.writer(f,delimiter='\t',lineterminator='\n')
    w.writerow(['sex','gestational_week','dimension','n','median_percent','q1_percent','q3_percent'])
    for sex in ('male','female'):
        for i,week in enumerate(range(37,41)):
            for dimension,groups in DATA[sex].items():
                median,q1,q3=groups[i]
                assert q1<=median<=q3
                w.writerow([sex,week,dimension,N[sex][i],f'{median:.2f}',f'{q1:.2f}',f'{q3:.2f}'])
