#!/usr/bin/env python3
"""Aggregate NHANES III infant surface data paired with recumbent length."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import numpy as np
import pandas as pd
import zstandard

SHA='3bfa4d4f1bffcedc5523a69973e01c26790a8b6eebba6aaed2600dc4ef1d236e'
OUT=Path(__file__).resolve().parents[1]/'anthropometry'/'INFANT_NHANES_III_LENGTH_BINS.tsv'


def main(path):
    if hashlib.sha256(path.read_bytes()).hexdigest()!=SHA: raise ValueError('Incorrect source checksum')
    rows=[]
    with path.open('rb') as f, zstandard.ZstdDecompressor().stream_reader(f) as stream:
        for line in io.TextIOWrapper(stream,encoding='utf-8'):
            r=json.loads(line);age=r.get('MXPAXTMR');h=r.get('BMPRECUM');sex=r.get('HSSEX')
            if (r.get('DMPSTAT')!=2 or sex not in (1,2) or not isinstance(age,(int,float)) or
                not 2<=age<24 or r.get('BMPRECFL')!=0 or not isinstance(h,(int,float)) or h<=0 or h in (8888,88888,888888,9999,99999,999999)):
                continue
            for key,dim in [('BMPARML','upper_arm_surface'),('BMPHEAD','head_circumference')]:
                value=r.get(key)
                if not isinstance(value,(int,float)) or value<=0 or value in (8888,88888,888888,9999,99999,999999): continue
                rows.append({'sex':'male' if sex==1 else 'female','dimension':dim,'length_cm':h,'age_months':age,'value':value})
    df=pd.DataFrame(rows);df['bin_start_cm']=np.floor(df.length_cm/5)*5
    output=[]
    for (sex,dim,start),g in df.groupby(['sex','dimension','bin_start_cm'],sort=True):
        output.append(dict(sex=sex,dimension=dim,bin_start_cm=start,bin_end_exclusive_cm=start+5,n=len(g),
            length_mean_cm=g.length_cm.mean(),age_mean_months=g.age_months.mean(),dimension_mean_cm=g.value.mean(),
            dimension_sd_cm=g.value.std(),ratio_mean=(g.value/g.length_cm).mean(),ratio_sd=(g.value/g.length_cm).std()))
    pd.DataFrame(output).to_csv(OUT,sep='\t',index=False,na_rep='NA',float_format='%.6f',lineterminator='\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('exam_jsonl_zst',type=Path);main(p.parse_args().exam_jsonl_zst)
