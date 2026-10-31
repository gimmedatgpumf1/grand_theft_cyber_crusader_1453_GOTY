#!/usr/bin/env python3
"""Fit single, tail-weighted height curves to pooled individual measurements.

Requires numpy, pandas and scipy. See POOLED_HEIGHT_MODELS.md for the
measurement substitutions, neonatal summary priors and population limits.
"""
from pathlib import Path
import hashlib
import math
import re
import sys

import numpy as np
import pandas as pd
from scipy.ndimage import gaussian_filter1d
from scipy.optimize import lsq_linear

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT/'tools/skeleton-source/anthropometry'
EXPECTED = {
    'NHANES_III_PAIRED_CM.csv': 'd366cff25da8a8c4a52b180cf85bc1d92d4e7dca1f731bede9ea06b8e08638f4',
    'NHANES_III_INFANT_PAIRED_CM.csv': 'a6d357b31bbd09d9f2727eae3f5c2bca63df8e2d88c253978759c76a1f58325b',
    'ANSUR_II_PAIRED_MM.csv': '1b1c0865c328a6aa297a3133f49585eff61002e9208fe3beb183c866c316fbee',
}
DIMENSIONS = ['sitting_height','upper_arm_surface','upper_leg_surface','shoulder_breadth',
              'pelvis_surface_breadth','head','neck','torso','arm','leg']
NHANES = dict(zip(DIMENSIONS[:5],['BMPSITHT','BMPARML','BMPLEG','BMPBIAC','BMPBIIL']))
# Study summaries, not manufactured individual records. A 50 cm target is a
# ratio-of-means extrapolation or approximate ratio, not a conditioned mean.
def neonatal_targets(sex):
    male = sex=='male'
    return {'sitting_height':50*(34.2/50.9 if male else 33.7/50.4),
            'upper_arm_surface':50*(.1900 if male else .1908),
            'upper_leg_surface':50*8.3/50.6,
            'shoulder_breadth':12.4, 'pelvis_surface_breadth':50*7.74/48.75,
            'head':12.5,  # rough 1:4 neonatal head/length convention, not individual measurements
            'arm':50*(.4286 if male else .4281),
            # Malleolus-to-floor term is separately extrapolated, not observed.
            'leg':50*(.4102 if male else .4150)+(2.2914998 if male else 2.1843401)}


def checked(name):
    path=DATA/name
    if hashlib.sha256(path.read_bytes()).hexdigest()!=EXPECTED[name]:
        raise ValueError(f'unexpected source hash: {path}')
    return pd.read_csv(path,low_memory=False)


def valid(x,y):
    x,y=np.asarray(x,dtype=float),np.asarray(y,dtype=float)
    ok=np.isfinite(x)&np.isfinite(y)&(x>0)&(y>0)&(y<x)
    return x[ok],y[ok]


def basis(x):
    t=(np.asarray(x)-50)/200
    return np.column_stack(((1-t)**2,2*t*(1-t),t*t))


def design(x):
    b=basis(x)
    return np.column_stack((np.ones(len(x)),b[:,1:].sum(axis=1),b[:,2]))


def density_weights(x):
    # Smooth kernel density approximated on a 1 cm grid, then cap influence of
    # isolated observations. This prioritizes sparsely sampled heights rather
    # than giving the common adult heights almost all the influence.
    hist,_=np.histogram(x,bins=np.arange(0,301))
    density=gaussian_filter1d(hist.astype(float),7,mode='constant')
    local=np.interp(x,np.arange(300)+.5,density)
    weights=np.minimum(10,(np.median(local)/np.maximum(local,.1))**.7)
    return weights/np.mean(weights)


def fit(child_x,child_y,adult_x,adult_y,infant_x=(),infant_y=(),anchor=None,baseline=False):
    cx,cy=valid(child_x,child_y); ax,ay=valid(adult_x,adult_y); ix,iy=valid(infant_x,infant_y)
    assert len(cx)>20 and len(ax)>20
    if baseline:
        ix,iy=ix[:0],iy[:0]  # previous estimator used no infant records
    x=np.r_[cx,ax,ix]; y=np.r_[cy,ay,iy]
    w=np.ones(len(x)) if baseline else density_weights(x)
    if not baseline:
        # Prioritize the upper tail of the adult sample as well as sparse
        # children; this is an observation weight, not an age transition.
        tall=np.quantile(ax,0.9)
        w*=1+6/(1+np.exp(np.clip(-(x-tall)/3,-50,50)))
        w/=np.mean(w)
    short=cx<=cx.min()+10
    ratio=np.mean(cy[short]/cx[short])
    # Where no neonatal reference exists, retain the previous 50 cm
    # extrapolation constraint rather than claim an unobserved measurement.
    lower,upper=.9*50*ratio,1.1*50*ratio
    matrix=design(x)
    if not baseline and anchor is not None:
        # The summary is one weighted loss term, not 3,000 individual infants.
        matrix=np.vstack((matrix,design(np.array([50.]))))
        y=np.r_[y,anchor]; w=np.r_[w,3000.]
        lower,upper=.01,49.99
    result=lsq_linear(matrix*np.sqrt(w[:,None]),y*np.sqrt(w),
                      bounds=(np.r_[lower,[.1,.1]],np.r_[upper,[np.inf,np.inf]]),tol=1e-12)
    if not result.success: raise ValueError(result.message)
    q=np.cumsum(result.x)
    c=np.array([q[0],(q[0]+2*q[1])/3,(2*q[1]+q[2])/3,q[2]])
    prediction=basis(x)@q
    return c,dict(children=len(cx),adults=len(ax),infants=len(ix),min_height_cm=x.min(),
                  max_height_cm=x.max(),rmse_cm=np.sqrt(np.mean((prediction-y[:len(x)])**2)),
                  short_child_ratio=ratio,short_child_n=int(short.sum()))


def evaluate(coeff,x):
    t=(np.asarray(x)-50)/200
    return coeff[0]*(1-t)**3+3*coeff[1]*t*(1-t)**2+3*coeff[2]*t*t*(1-t)+coeff[3]*t**3


def equivalent_generated(actual,expected):
    token=re.compile(r'(?<![A-Za-z0-9_])[-+]?(?:[0-9]+\.[0-9]+|[0-9]+)(?:[eE][-+]?[0-9]+)?')
    if token.sub('<number>',actual)!=token.sub('<number>',expected): return False
    left=np.array([float(v) for v in token.findall(actual)])
    right=np.array([float(v) for v in token.findall(expected)])
    return left.shape==right.shape and np.allclose(left,right,rtol=1e-9,atol=1e-9)


def pairs(frame,x,y,ids):
    return pd.DataFrame({'x':np.asarray(x,dtype=float),'y':np.asarray(y,dtype=float),
                         'id':np.asarray(ids,dtype=int)},index=frame.index)


def main():
    n=checked('NHANES_III_PAIRED_CM.csv')
    n=n[(n.BMPHTFLG==0)&n.HSSEX.isin([1,2])&n.MXPAXTMR.notna()]
    infants=checked('NHANES_III_INFANT_PAIRED_CM.csv')
    # A recorded head circumference longer than the child's total length is a
    # clear measurement inconsistency; retain the raw row but exclude it here.
    infants=infants[infants.BMPHEAD.isna() | (infants.BMPHEAD<infants.BMPRECUM)]
    a=checked('ANSUR_II_PAIRED_MM.csv'); a=a[a.Age>=18]
    models,report,audit,replication={},[],[],[]
    for sex,code in [('male',1),('female',2)]:
        subjects=n[n.HSSEX==code];child=subjects[subjects.MXPAXTMR<216];adult=subjects[subjects.MXPAXTMR>=216]
        infant=infants[infants.HSSEX==code]; army=a[a.Gender.str.lower()==sex]
        mean_head=22.3 if sex=='male' else 21.2
        factors={'head':mean_head/(army.headcircumference.mean()/10),
                 'neck':((army.stature-army.tragiontopofhead-army.cervicaleheight)/army.headcircumference).mean(),
                 'torso':((army.cervicaleheight-army.crotchheight)/army.sittingheight).mean(),
                 'arm':((army.sleeveoutseam+army.handlength)/army.acromionradialelength).mean(),
                 'leg':(army.trochanterionheight/army.subischialheight).mean()}
        pediatric={'head':'BMPHEAD','neck':'BMPHEAD','torso':'BMPSITHT','arm':'BMPARML','leg':'subischial_cm'}
        adult_values={'head':army.headcircumference/10*factors['head'],
                      'neck':(army.stature-army.tragiontopofhead-army.cervicaleheight)/10,
                      'torso':(army.cervicaleheight-army.crotchheight)/10,
                      'arm':(army.sleeveoutseam+army.handlength)/10,'leg':army.trochanterionheight/10}
        for dim in DIMENSIONS:
            if dim in NHANES:
                col=NHANES[dim]; fac=1.0
                c=pairs(child,child.BMPHT,child[col],child.SEQN)
                d=pairs(adult,adult.BMPHT,adult[col],adult.SEQN)
                source='NHANES III child + adult'
            else:
                col=pediatric[dim]; fac=factors[dim]
                c=pairs(child,child.BMPHT,child[col]*fac,child.SEQN)
                d=pairs(army,army.stature/10,adult_values[dim],army.SubjectId)
                source='NHANES child proxy + ANSUR adult'
            # Full-arm infant input remains an adult-ratio upper-arm proxy;
            # head circumference is NOT an infant head-height measurement.
            if col=='BMPARML':
                i=pairs(infant,infant.BMPRECUM,infant.BMPARML*fac,infant.SEQN)
            else:
                i=pd.DataFrame({'x':[],'y':[],'id':[]})
            anchor=neonatal_targets(sex).get(dim)
            coef,stats=fit(c.x,c.y,d.x,d.y,i.x,i.y,anchor)
            models[(sex,dim)]=coef
            report.append(dict(sex=sex,dimension=dim,source=source,proxy_factor=fac,
                               anchor_cm=anchor if anchor is not None else np.nan,**stats))
            # One deterministic identifier-level 80/20 holdout. The adult
            # calibration factors above use the full adult set; this is not
            # independent validation of the converted body proxies.
            for fold,output in ((0,audit),(1,replication)):
                train=[z[z.id%5!=fold] for z in (c,d,i)]
                test=pd.concat([z[z.id%5==fold] for z in (c,d,i)],ignore_index=True)
                new,_=fit(train[0].x,train[0].y,train[1].x,train[1].y,train[2].x,train[2].y,anchor)
                old,_=fit(train[0].x,train[0].y,train[1].x,train[1].y,baseline=True)
                for region,mask in [('short',test.x<90),('tall',test.x>=(190 if sex=='male' else 180)),
                                    ('all',np.ones(len(test),dtype=bool))]:
                    tx,ty=valid(test.x[mask],test.y[mask])
                    if len(tx)==0: continue
                    error_new=evaluate(new,tx)-ty; error_old=evaluate(old,tx)-ty
                    output.append(dict(sex=sex,dimension=dim,region=region,n=len(tx),
                                      old_bias_cm=np.mean(error_old),new_bias_cm=np.mean(error_new),
                                      old_mae_cm=np.mean(abs(error_old)),new_mae_cm=np.mean(abs(error_new)),
                                      old_rmse_cm=np.sqrt(np.mean(error_old**2)),
                                      new_rmse_cm=np.sqrt(np.mean(error_new**2))))
    rows=['// Generated by tools/skeleton-source/openskeleton/26-pooled-height-models.py; do not edit.',
          '#pragma once','','namespace gtc::native::pooled_height_data {',
          '// Quadratic Bernstein fit, degree-elevated to cubic; domain 50–250 cm.',
          'inline constexpr double coefficients[2][10][4] = {']
    for sex in ('male','female'):
        rows.append('    { // '+sex)
        for dim in DIMENSIONS:
            rows.append('        {'+', '.join(format(x,'.15g') for x in models[(sex,dim)])+'}, // '+dim)
        rows.append('    },')
    rows+=['};','} // namespace gtc::native::pooled_height_data','']
    outputs={ROOT/'native/client/pooled_height_coefficients.h':'\n'.join(rows),
             DATA/'POOLED_HEIGHT_MODELS.tsv':pd.DataFrame(report).to_csv(sep='\t',index=False,float_format='%.9f',na_rep='NA',lineterminator='\n'),
             DATA/'POOLED_HEIGHT_TAIL_HOLDOUT.tsv':pd.DataFrame(audit).to_csv(sep='\t',index=False,float_format='%.9f',lineterminator='\n'),
             DATA/'POOLED_HEIGHT_TAIL_REPLICATION.tsv':pd.DataFrame(replication).to_csv(sep='\t',index=False,float_format='%.9f',lineterminator='\n')}
    for path,content in outputs.items():
        if '--check' in sys.argv:
            if not path.exists() or not equivalent_generated(path.read_text(),content):
                raise SystemExit(f'generated file differs: {path}')
        else:
            path.write_text(content); print(path.relative_to(ROOT))


if __name__=='__main__': main()
