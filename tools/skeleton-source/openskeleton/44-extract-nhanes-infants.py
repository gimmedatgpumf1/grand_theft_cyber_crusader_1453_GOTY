#!/usr/bin/env python3
"""Extract paired NHANES III infant recumbent length/arm/head records."""
import argparse
import hashlib
import io
import json
from pathlib import Path

import pandas as pd
import zstandard

SHA = '3bfa4d4f1bffcedc5523a69973e01c26790a8b6eebba6aaed2600dc4ef1d236e'
OUT = Path(__file__).resolve().parents[1]/'anthropometry'/'NHANES_III_INFANT_PAIRED_CM.csv'


def extract(path):
    if hashlib.sha256(path.read_bytes()).hexdigest() != SHA:
        raise ValueError('Unexpected NHANES source checksum')
    rows = []
    with path.open('rb') as f, zstandard.ZstdDecompressor().stream_reader(f) as stream:
        for line in io.TextIOWrapper(stream,encoding='utf-8'):
            r = json.loads(line)
            age, h, sex = r.get('MXPAXTMR'), r.get('BMPRECUM'), r.get('HSSEX')
            if (r.get('DMPSTAT')!=2 or sex not in (1,2) or not isinstance(age,(int,float)) or
                    not 2<=age<24 or r.get('BMPRECFL')!=0 or not isinstance(h,(int,float)) or not 0<h<200):
                continue
            def valid(value):
                return value if isinstance(value,(int,float)) and 0<value<300 else None
            arm, head = valid(r.get('BMPARML')), valid(r.get('BMPHEAD'))
            if arm is not None or head is not None:
                rows.append(dict(SEQN=r['SEQN'],HSSEX=sex,MXPAXTMR=age,BMPRECUM=h,
                                 BMPARML=arm,BMPHEAD=head))
    pd.DataFrame(rows).sort_values('SEQN').to_csv(OUT,index=False,na_rep='',lineterminator='\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('exam_jsonl_zst',type=Path)
    extract(p.parse_args().exam_jsonl_zst)
