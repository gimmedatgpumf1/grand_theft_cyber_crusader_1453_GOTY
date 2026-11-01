#!/usr/bin/env python3
"""Export source-attributed NHANES III measurements and published DXA summaries."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import pandas as pd
import numpy as np
import zstandard

ROOT = Path(__file__).resolve().parents[1] / 'anthropometry'
SHA256 = '3bfa4d4f1bffcedc5523a69973e01c26790a8b6eebba6aaed2600dc4ef1d236e'
FIELDS = ['SEQN', 'DMPSTAT', 'DMARETHN', 'DMARACER', 'DMAETHNR', 'HSSEX', 'MXPAXTMR',
          'SDPPHASE', 'SDPPSU6', 'SDPSTRA6', 'WTPFEX6', 'WTPFHX6',
          'BMPHT', 'BMPHTFLG', 'BMPSITHT', 'BMPARML', 'BMPLEG', 'BMPBIAC',
          'BMPBIIL', 'BMPHEAD', 'BMPWT']
DIMS = ['BMPSITHT', 'subischial_cm', 'BMPARML', 'BMPLEG', 'BMPBIAC', 'BMPBIIL', 'BMPHEAD']


def decode_nhanes(path):
    if hashlib.sha256(path.read_bytes()).hexdigest() != SHA256:
        raise ValueError('Input hash differs from pinned NHANES III mirror')
    rows = []
    with path.open('rb') as f, zstandard.ZstdDecompressor().stream_reader(f) as stream:
        for line in io.TextIOWrapper(stream, encoding='utf-8'):
            record = json.loads(line)
            if record.get('DMPSTAT') != 2 or record.get('HSSEX') not in (1, 2):
                continue
            age = record.get('MXPAXTMR')
            if not isinstance(age, (int, float)) or not 24 <= age <= 1080:
                continue
            # NHANES III examination uses field-width 8-fills for applicable missing values.
            row = {k: record.get(k) for k in FIELDS}
            for k in FIELDS[12:]:
                if row[k] in (8888, 88888, 888888, 9999, 99999, 999999):
                    row[k] = None
            if not isinstance(row['BMPHT'], (int, float)) or row['BMPHT'] <= 0:
                continue
            # Retain source flags in paired records; summaries use unflagged heights only.
            row['subischial_cm'] = (row['BMPHT']-row['BMPSITHT']
                                     if row['BMPSITHT'] is not None else None)
            rows.append(row)
    d = pd.DataFrame(rows).sort_values('SEQN')
    assert d.SEQN.is_unique
    d.to_csv(ROOT / 'NHANES_III_PAIRED_CM.csv', index=False, na_rep='', lineterminator='\n')
    return d


def make_bins(data):
    rows=[]
    for code,sex in [(1,'Male'),(2,'Female')]:
        for agegroup in ('under_18','18_plus','all_ages'):
            d = data[(data.HSSEX==code) & (data.BMPHTFLG==0)].copy()
            if agegroup=='under_18':
                d=d[d.MXPAXTMR<216]
            elif agegroup=='18_plus':
                d=d[d.MXPAXTMR>=216]
            for dim in DIMS:
                v=d.dropna(subset=[dim]).copy()
                v['bin']=(v.BMPHT//5)*5
                for start,g in v.groupby('bin'):
                    rows.append(dict(sex=sex,age_group=agegroup,dimension=dim,
                                     height_bin_start_cm=start,height_bin_end_exclusive_cm=start+5,
                                     n=len(g),height_mean_cm=g.BMPHT.mean(),
                                     age_mean_years=g.MXPAXTMR.mean()/12,
                                     dimension_mean_cm=g[dim].mean(),dimension_sd_cm=g[dim].std(),
                                     ratio_mean=(g[dim]/g.BMPHT).mean(),
                                     ratio_sd=(g[dim]/g.BMPHT).std()))
    pd.DataFrame(rows).to_csv(ROOT/'NHANES_III_HEIGHT_BINS.tsv',sep='\t',index=False,
                              na_rep='NA',float_format='%.6f',lineterminator='\n')


def anthrokids_foot(path):
    expected = 'ece6ccb1486915329c8b9f651b6ef21d9972cacfc5c4eae8fdac241fabed3a50'
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise ValueError('Input hash differs from pinned AnthroKids excerpt')
    data = pd.read_csv(path)
    data = data.dropna(subset=['gender','age','height','foot'])
    data = data[(data.age >= 2) & (data.height > 0) & (data.foot > 0)]
    rows=[]
    for code,sex in [('M','Male'),('F','Female')]:
        for agegroup in ('under_18','18_plus','all_ages'):
            d=data[data.gender==code].copy()
            if agegroup=='under_18':
                d=d[d.age<18]
            elif agegroup=='18_plus':
                d=d[d.age>=18]
            d['bin']=(d.height//5)*5
            for start,g in d.groupby('bin'):
                rows.append(dict(sex=sex,age_group=agegroup,height_bin_start_cm=start,
                                 height_bin_end_exclusive_cm=start+5,n=len(g),
                                 height_mean_cm=g.height.mean(),age_mean_years=g.age.mean(),
                                 foot_mean_cm=g.foot.mean(),foot_sd_cm=g.foot.std(),
                                 foot_ratio_mean=(g.foot/g.height).mean(),
                                 foot_ratio_sd=(g.foot/g.height).std()))
    pd.DataFrame(rows).to_csv(ROOT/'ANTHROKIDS_FOOT_HEIGHT_BINS.tsv',sep='\t',index=False,
                              na_rep='NA',float_format='%.6f',lineterminator='\n')


def dxa_table():
    # Table 1: n, then (mean, SD) of stature, acromion-epicondyle, radius, femur, tibia, in cm.
    source = {
        'Male': [
            ('6-8',19,[(125.6,6.0),(22.9,1.6),(18.5,1.6),(29.5,2.0),(27.9,2.0)]),
            ('8-10',22,[(137.1,8.5),(24.7,1.7),(20.1,1.9),(33.1,2.8),(31.1,3.1)]),
            ('10-12',39,[(146.2,8.6),(26.9,2.0),(22.0,2.1),(36.1,3.0),(34.1,3.3)]),
            ('12-14',50,[(155.6,8.7),(28.6,1.8),(23.9,1.9),(38.9,2.8),(36.5,2.6)]),
            ('14-16',20,[(163.1,9.2),(30.2,1.4),(25.3,1.5),(41.2,2.4),(38.9,2.8)]),
            ('16-18',20,[(170.8,12.0),(31.0,1.6),(26.3,1.7),(42.1,3.0),(39.8,3.0)])],
        'Female': [
            ('6-8',21,[(124.5,8.0),(23.0,2.0),(17.9,1.8),(30.0,2.7),(27.7,2.6)]),
            ('8-10',43,[(138.8,8.0),(25.4,1.8),(20.5,1.8),(34.0,2.6),(32.0,2.5)]),
            ('10-12',52,[(149.8,7.6),(27.4,1.8),(22.6,1.7),(37.5,2.2),(35.2,2.2)]),
            ('12-14',61,[(158.5,8.8),(29.0,2.1),(24.1,2.1),(39.4,2.8),(37.2,2.9)]),
            ('14-16',39,[(164.3,6.6),(29.8,1.3),(24.9,1.5),(40.9,2.0),(38.0,2.5)]),
            ('16-18',27,[(163.2,5.9),(30.1,1.9),(25.1,1.9),(40.5,2.2),(37.2,2.1)])]}
    dimensions=['stature','acromion_lateral_epicondyle','radius_head_styloid',
                'femur_trochanter_lateral_epicondyle','tibia_condyle_malleolus']
    rows=[]
    for sex,groups in source.items():
        for age,n,values in groups:
            for dimension,(mean,sd) in zip(dimensions,values):
                rows.append(dict(sex=sex,age_years=age,n=n,dimension=dimension,mean_cm=mean,sd_cm=sd))
    pd.DataFrame(rows).to_csv(ROOT/'PEDIATRIC_DXA_STUDY_SUMMARY.tsv',sep='\t',index=False,lineterminator='\n')
    # Table 2: mean and SD of individual ratios, expressed by the paper in percent.
    ratios={'Male':[(18.3,0.8),(15.2,0.8),(24.6,1.1),(23.2,1.1)],
            'Female':[(18.3,0.8),(15.1,0.8),(24.8,0.8),(23.2,1.0)]}
    rows=[]
    for sex,vals in ratios.items():
        for dim,(mean,sd) in zip(dimensions[1:],vals):
            rows.append(dict(sex=sex,dimension=dim,n=170 if sex=='Male' else 243,
                             ratio_mean=mean/100,ratio_sd=sd/100))
    pd.DataFrame(rows).to_csv(ROOT/'PEDIATRIC_DXA_POOLED_RATIOS.tsv',sep='\t',index=False,lineterminator='\n')
    # Published direction Y=A*X+B; never algebraically invert to claim a forward regression.
    equations=[('stature','femur',3.196,31.774,.901,3.348,25.847,.921),
               ('humerus','radius',.991,4.901,.965,.974,5.468,.956),
               ('femur','tibia',1.012,1.729,.969,.999,2.436,.934),
               ('femur','humerus',1.480,-3.637,.905,1.355,-.008,.874),
               ('tibia','radius',1.445,1.992,.894,1.299,5.443,.867),
               ('tibia','humerus',1.417,-4.053,.877,1.278,-.291,.832)]
    rows=[]
    for y,x,am,bm,rm,af,bf,rf in equations:
        for sex,a,b,r in [('Male',am,bm,rm),('Female',af,bf,rf)]:
            rows.append(dict(sex=sex,predicted_y=y,predictor_x=x,slope=a,intercept_cm=b,r_squared=r))
    pd.DataFrame(rows).to_csv(ROOT/'PEDIATRIC_DXA_PUBLISHED_EQUATIONS.tsv',sep='\t',index=False,lineterminator='\n')


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--exam-zst',type=Path,required=True)
    p.add_argument('--anthrokids-csv',type=Path)
    args=p.parse_args()
    data=decode_nhanes(args.exam_zst)
    make_bins(data)
    dxa_table()
    if args.anthrokids_csv:
        anthrokids_foot(args.anthrokids_csv)


if __name__=='__main__':
    main()
