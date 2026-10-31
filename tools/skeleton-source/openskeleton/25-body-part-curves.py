#!/usr/bin/env python3
"""Make sex/age-stratified nonlinear body-part curves from paired surveys.

This is an exploratory surface-landmark model, not a skeleton deform/physics model.
See BODY_PART_CURVES.md. Dependencies: numpy, pandas, scipy.
"""
import argparse
import hashlib
from pathlib import Path
import numpy as np
import pandas as pd
from scipy.interpolate import PchipInterpolator

ROOT = Path(__file__).resolve().parents[1] / 'anthropometry'
EXPECTED = {
    'NHANES_III_PAIRED_CM.csv': 'd366cff25da8a8c4a52b180cf85bc1d92d4e7dca1f731bede9ea06b8e08638f4',
    'ANSUR_II_PAIRED_MM.csv': '1b1c0865c328a6aa297a3133f49585eff61002e9208fe3beb183c866c316fbee',
    'anthrokids.csv': 'ece6ccb1486915329c8b9f651b6ef21d9972cacfc5c4eae8fdac241fabed3a50',
}
NHANES = {'BMPSITHT':'sitting_height', 'subischial_cm':'height_minus_sitting',
          'BMPARML':'upper_arm_surface', 'BMPLEG':'upper_leg_surface',
          'BMPBIAC':'shoulder_breadth', 'BMPBIIL':'pelvis_surface_breadth'}
ANSUR = {'sittingheight':'sitting_height', 'subischialheight':'height_minus_sitting',
         'acromionradialelength':'upper_arm_surface', 'radialestylionlength':'forearm_surface',
         'handlength':'hand_surface', 'footlength':'foot_surface',
         'trochanter_to_tibiale_vertical':'thigh_vertical',
         'tibiale_to_malleolus_vertical':'lower_leg_vertical',
         'biacromialbreadth':'shoulder_breadth', 'bicristalbreadth':'pelvis_surface_breadth',
         'headbreadth':'head_breadth', 'headlength':'head_depth',
         'tragiontopofhead':'ear_to_crown'}
MIN_N = 20
MAX_GAP = 10.0  # centimetres between centres; do not bridge missing bins


def read_checked(path, expected):
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise ValueError(f'Unexpected SHA-256 for {path}')
    return pd.read_csv(path, low_memory=False)


def records(anthrokids):
    n=read_checked(ROOT/'NHANES_III_PAIRED_CM.csv', EXPECTED['NHANES_III_PAIRED_CM.csv'])
    n=n[(n.BMPHTFLG==0) & n.HSSEX.isin([1,2])].copy()
    n['sex']=n.HSSEX.map({1:'male',2:'female'}); n['age']=n.MXPAXTMR/12
    n['height']=n.BMPHT; n['age_group']=np.where(n.age<18,'under_18','adult_18_plus'); n['split_id']=n.SEQN
    a=read_checked(ROOT/'ANSUR_II_PAIRED_MM.csv', EXPECTED['ANSUR_II_PAIRED_MM.csv'])
    a=a[a.Age>=18].copy(); a['sex']=a.Gender.str.lower(); a['age']=a.Age
    a['height']=a.stature/10; a['age_group']='adult_18_plus'; a['split_id']=a.SubjectId
    for key in ANSUR: a[key]=a[key]/10
    c=read_checked(anthrokids, EXPECTED['anthrokids.csv'])
    # One exact duplicate row exists; repeated IDs can describe different
    # records. Remove exact duplicates and split any repeated ID as a unit.
    c=c.drop_duplicates(); c=c[(c.age>=2) & c.gender.isin(['M','F'])].copy(); c['split_id']=c.id
    c['sex']=c.gender.map({'M':'male','F':'female'}); c['age_group']=np.where(c.age<18,'under_18','adult_18_plus')
    return [('NHANES_III',n,NHANES),('ANSUR_II',a,ANSUR),('ANTHROKIDS_1977_EXCERPT',c,{'foot':'foot_surface'})]


def knots(data):
    d=data.copy(); d['bin']=np.floor(d.height/5)*5
    g=d.groupby('bin',sort=True).agg(n=('height','size'), x=('height','mean'), y=('value','mean'))
    return g[g.n>=MIN_N]


def in_support(x, curve):
    if len(curve)<2: return np.zeros(len(x),dtype=bool)
    coords=curve.x.to_numpy(); x=np.asarray(x)
    i=np.searchsorted(coords,x,side='right')-1
    valid=(x>=coords[0]) & (x<=coords[-1])
    i=np.clip(i,0,len(coords)-2)
    return valid & ((coords[i+1]-coords[i]) <= MAX_GAP)


def main(anthrokids):
    output=[]; validation=[]
    for source, frame, dims in records(anthrokids):
        # One subject-level partition shared by all dimensions; not per-measurement.
        rng=np.random.default_rng(1453); frame=frame.copy()
        ids=frame.split_id.unique(); assignment=dict(zip(ids,rng.random(len(ids))<0.8))
        frame['train']=frame.split_id.map(assignment)
        for sex in ('male','female'):
            for age_group in ('under_18','adult_18_plus'):
                subject=frame[(frame.sex==sex)&(frame.age_group==age_group)]
                for column, dim in dims.items():
                    d=subject[['height',column,'train']].rename(columns={column:'value'}).dropna()
                    d=d[np.isfinite(d.height)&np.isfinite(d.value)&(d.height>0)&(d.value>0)]
                    if d.empty: continue
                    group=knots(d)
                    for start,row in group.iterrows():
                        output.append(dict(source=source,sex=sex,age_group=age_group,dimension=dim,
                            source_column=column,bin_start_cm=start,n=int(row.n),
                            height_mean_cm=row.x,dimension_mean_cm=row.y))
                    train=d[d.train]; test=d[~d.train]; curve=knots(train)
                    mask=in_support(test.height.to_numpy(),curve); test=test.iloc[np.flatnonzero(mask)]
                    if len(curve)<2 or len(train)<30 or len(test)<20: continue
                    x=train.height.to_numpy(); y=train.value.to_numpy()
                    slope,intercept=np.polyfit(x,y,1)
                    pow_slope,pow_intercept=np.polyfit(np.log(x),np.log(y),1)
                    x_test=test.height.to_numpy(); actual=test.value.to_numpy()
                    pchip=PchipInterpolator(curve.x,curve.y,extrapolate=False)(x_test)
                    # This derived surface distance must complement sitting
                    # height exactly, even between knots.
                    if dim=='height_minus_sitting':
                        pchip=x_test-PchipInterpolator(curve.x,curve.x-curve.y,extrapolate=False)(x_test)
                    models={'linear':intercept+slope*x_test,
                            'power':np.exp(pow_intercept)*x_test**pow_slope,
                            'pchip_5cm_bin_means':pchip}
                    for method,pred in models.items():
                        err=pred-actual
                        validation.append(dict(source=source,sex=sex,age_group=age_group,dimension=dim,
                            method=method,n_train=len(train),n_test_supported=len(test),
                            total_test=len(d)-len(train),mae_cm=np.mean(abs(err)),
                            rmse_cm=np.sqrt(np.mean(err**2))))
    pd.DataFrame(output).to_csv(ROOT/'BODY_PART_CURVE_KNOTS.tsv',sep='\t',index=False,
                                float_format='%.6f',lineterminator='\n')
    pd.DataFrame(validation).to_csv(ROOT/'BODY_PART_CURVE_HOLDOUT.tsv',sep='\t',index=False,
                                    float_format='%.6f',lineterminator='\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('anthrokids_csv',type=Path)
    main(p.parse_args().anthrokids_csv)
