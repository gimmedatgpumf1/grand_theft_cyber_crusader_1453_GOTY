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


def fit(child_x,child_y,adult_x,adult_y,infant_x=(),infant_y=(),anchor=None,baseline=False,boost=6.):
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
        w*=1+boost/(1+np.exp(np.clip(-(x-tall)/3,-50,50)))
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


def cubic_fit(child_x,child_y,adult_x,adult_y,infant_x,infant_y,anchor,penalty):
    """Constrained single cubic with a penalty on its third derivative."""
    cx,cy=valid(child_x,child_y); ax,ay=valid(adult_x,adult_y); ix,iy=valid(infant_x,infant_y)
    x=np.r_[cx,ax,ix]; y=np.r_[cy,ay,iy]
    w=density_weights(x)
    tall=np.quantile(ax,.9)
    w*=1+6/(1+np.exp(np.clip(-(x-tall)/3,-50,50)))
    w/=np.mean(w)
    t=(x-50)/200
    b=np.column_stack(((1-t)**3,3*t*(1-t)**2,3*t*t*(1-t),t**3))
    matrix=np.column_stack((np.ones(len(x)),b[:,1:].sum(axis=1),b[:,2:].sum(axis=1),b[:,3]))
    short=cx<=cx.min()+10
    ratio=np.mean(cy[short]/cx[short])
    lower,upper=.9*50*ratio,1.1*50*ratio
    if anchor is not None:
        matrix=np.vstack((matrix,[1.,0.,0.,0.]))
        y=np.r_[y,anchor]; w=np.r_[w,3000.]
        lower,upper=.01,49.99
    # The cubic's third derivative is proportional to increment1-2*increment2+increment3.
    matrix=np.vstack((matrix,[0.,1.,-2.,1.]))
    y=np.r_[y,0.]; w=np.r_[w,penalty]
    result=lsq_linear(matrix*np.sqrt(w[:,None]),y*np.sqrt(w),
                      bounds=(np.r_[lower,[.001*200/3]*3],np.r_[upper,[.999*200/3]*3]),tol=1e-12)
    if not result.success: raise ValueError(result.message)
    return np.cumsum(result.x)


def selected_fit(frames,anchor,kind):
    c,d,i=frames
    if kind=='quadratic' or str(kind).startswith('q'):
        boost=6. if kind=='quadratic' else float(str(kind)[1:])
        return fit(c.x,c.y,d.x,d.y,i.x,i.y,anchor,boost=boost)[0]
    return cubic_fit(c.x,c.y,d.x,d.y,i.x,i.y,anchor,float(kind))


def split(frames,excluded,include):
    return [z[z.id.mod(5).isin(excluded) if include else ~z.id.mod(5).isin(excluded)] for z in frames]


def input_frames(n,infants,army,sex,code,dimension,excluded):
    """Re-estimate proxy calibration on training adults only for each split."""
    subjects=n[n.HSSEX==code]; child=subjects[subjects.MXPAXTMR<216]; adult=subjects[subjects.MXPAXTMR>=216]
    infant=infants[infants.HSSEX==code]
    army=army[army.Gender.str.lower()==sex]
    calibration=army[~army.SubjectId.mod(5).isin(excluded)]
    mean_head=22.3 if sex=='male' else 21.2
    factors={'head':mean_head/(calibration.headcircumference.mean()/10),
             'neck':((calibration.stature-calibration.tragiontopofhead-calibration.cervicaleheight)/calibration.headcircumference).mean(),
             'torso':((calibration.cervicaleheight-calibration.crotchheight)/calibration.sittingheight).mean(),
             'arm':((calibration.sleeveoutseam+calibration.handlength)/calibration.acromionradialelength).mean(),
             'leg':(calibration.trochanterionheight/calibration.subischialheight).mean()}
    pediatric={'head':'BMPHEAD','neck':'BMPHEAD','torso':'BMPSITHT','arm':'BMPARML','leg':'subischial_cm'}
    adult_values={'head':army.headcircumference/10*factors['head'],
                  'neck':(army.stature-army.tragiontopofhead-army.cervicaleheight)/10,
                  'torso':(army.cervicaleheight-army.crotchheight)/10,
                  'arm':(army.sleeveoutseam+army.handlength)/10,'leg':army.trochanterionheight/10}
    if dimension in NHANES:
        col=NHANES[dimension]; factor=1.
        c=pairs(child,child.BMPHT,child[col],child.SEQN)
        d=pairs(adult,adult.BMPHT,adult[col],adult.SEQN)
        source='NHANES III child + adult'
    else:
        col=pediatric[dimension]; factor=factors[dimension]
        c=pairs(child,child.BMPHT,child[col]*factor,child.SEQN)
        d=pairs(army,army.stature/10,adult_values[dimension],army.SubjectId)
        source='NHANES child proxy + ANSUR adult'
    if col=='BMPARML':
        i=pairs(infant,infant.BMPRECUM,infant.BMPARML*factor,infant.SEQN)
    else:
        i=pd.DataFrame({'x':[],'y':[],'id':[]})
    return [c,d,i],source,factor


def score(coeff,frames,adult_threshold):
    test=pd.concat(frames,ignore_index=True)
    test=test[np.isfinite(test.x)&np.isfinite(test.y)&(test.x>0)&(test.y>0)&(test.y<test.x)]
    err=(evaluate(coeff,test.x)-test.y)**2
    groups=[err[test.x<105],err[test.x>=adult_threshold],err]
    return np.mean([np.mean(g) for g in groups if len(g)])


def head_ratio_errors(coef,sex,heights,study,age_folds):
    """Compare predicted average stature/head ratios to an external study.

    Age-matched heights are from a different survey; this is a population-level
    comparison, not a direct paired-individual error or clinical validation.
    """
    rows=[]
    for row in study[study.sex==sex].itertuples():
        age=int(row.age_min_inclusive_years)
        if age%5 not in age_folds: continue
        group=heights[(heights.MXPAXTMR>=12*age)&(heights.MXPAXTMR<12*(age+1))]
        x=np.asarray(group.BMPHT,dtype=float)
        x=x[np.isfinite(x)&(x>0)]
        if not len(x): continue
        predicted=np.mean(x/evaluate(coef,x))
        rows.append((age,row.n,row.mean_stature_over_head_height,predicted))
    return rows


def select_model(factory,anchor,dimension,outer_excluded,sex,heights,head_study):
    # Shoulder and whole-arm curves are long levers acting on hand vertices.
    # Limit their cubic curvature change to preserve observed mesh smoothness;
    # the restriction was chosen from mesh sweeps, not age-specific blending.
    candidates=['quadratic','q12','q24','q48']
    candidates+=([30,300,3000] if dimension in ('shoulder_breadth','arm') else [1,30,300,3000])
    scores={key:[] for key in candidates}
    tail_error={key:[] for key in candidates}
    external={key:[] for key in candidates}
    allowed_ages=[i for i in range(5) if i not in outer_excluded]
    for inner in range(5):
        if inner in outer_excluded: continue
        excluded=outer_excluded+[inner]
        frames,_,_=factory(excluded)
        train=split(frames,excluded,False)
        validation=split(frames,[inner],True)
        ax,_=valid(train[1].x,train[1].y)
        threshold=np.quantile(ax,.9)
        train_heights=heights[~heights.SEQN.mod(5).isin(excluded)]
        for candidate in candidates:
            coef=selected_fit(train,anchor,candidate)
            scores[candidate].append(score(coef,validation,threshold))
            test=pd.concat(validation,ignore_index=True)
            tx,ty=valid(test.x,test.y)
            mask=tx>=(190 if sex=='male' else 180)
            tail_error[candidate].extend(abs(evaluate(coef,tx[mask])-ty[mask]))
            if dimension=='head':
                ratios=head_ratio_errors(coef,sex,train_heights,head_study,allowed_ages)
                external[candidate].append(np.mean([(pred-observed)**2 for _,_,observed,pred in ratios]))
    averages={key:float(np.mean(value)) for key,value in scores.items()}
    if dimension=='head':
        # Circumference is not head height. Use published actual paired
        # stature/head-height ratio summaries to select among pooled candidates;
        # avoid a gross proxy fit regression. External age cells reserved by
        # the outer fold are excluded from this choice.
        external={key:float(np.mean(value)) for key,value in external.items()}
        eligible=[key for key in candidates if averages[key]<=1.20*averages['quadratic']]
        best=min(eligible,key=external.get)
        if external[best]>=.99*external['quadratic']: best='quadratic'
    else:
        # Protect the actual extreme tail, not just a broad 90th-percentile
        # validation region. This decision uses only inner folds; test data are
        # excluded. Very sparse cells cannot reliably inform this constraint.
        if len(tail_error['quadratic'])>=10:
            baseline_tail=np.mean(tail_error['quadratic'])
            eligible=[key for key in candidates if key=='quadratic' or np.mean(tail_error[key])<.995*baseline_tail]
        else:
            eligible=candidates
        best=min(eligible,key=averages.get)
        # Avoid changing the model when the in-training CV score changes only
        # slightly; outer test records are not used in model selection.
        if averages[best]>=.99*averages['quadratic']: best='quadratic'
    return best,averages[best],averages['quadratic']


def main():
    n=checked('NHANES_III_PAIRED_CM.csv')
    n=n[(n.BMPHTFLG==0)&n.HSSEX.isin([1,2])&n.MXPAXTMR.notna()]
    infants=checked('NHANES_III_INFANT_PAIRED_CM.csv')
    # A head circumference longer than total length is a clear inconsistency.
    # Retain the raw row but exclude it from fitting.
    infants=infants[infants.BMPHEAD.isna() | (infants.BMPHEAD<infants.BMPRECUM)]
    a=checked('ANSUR_II_PAIRED_MM.csv'); a=a[a.Age>=18]
    ratios=DATA/'PEDIATRIC_PAIRED_HEAD_BODY_RATIOS.tsv'
    if hashlib.sha256(ratios.read_bytes()).hexdigest()!='a8140a14926488685547f1a420bcc5eb2b6aa2ebd7d6c80c7a4e85c0ab01415a':
        raise ValueError('unexpected pediatric ratio summary hash')
    head_study=pd.read_csv(ratios,sep='\t')
    models,report,audit,head_audit={},[],[],[]
    for sex,code in [('male',1),('female',2)]:
        heights=n[n.HSSEX==code]
        for dim in DIMENSIONS:
            factory=lambda excluded:input_frames(n,infants,a,sex,code,dim,excluded)
            anchor=neonatal_targets(sex).get(dim)
            kind,cv,quad_cv=select_model(factory,anchor,dim,[],sex,heights,head_study)
            frames,source,fac=factory([])
            c,d,i=frames
            coef=selected_fit(frames,anchor,kind)
            _,stats=fit(c.x,c.y,d.x,d.y,i.x,i.y,anchor)
            allpairs=pd.concat(frames,ignore_index=True); tx,ty=valid(allpairs.x,allpairs.y)
            stats['rmse_cm']=np.sqrt(np.mean((evaluate(coef,tx)-ty)**2))
            models[(sex,dim)]=coef
            report.append(dict(sex=sex,dimension=dim,source=source,proxy_factor=fac,
                               anchor_cm=anchor if anchor is not None else np.nan,
                               model=kind,cv_mse=cv,quadratic_cv_mse=quad_cv,
                               c0=coef[0],c1=coef[1],c2=coef[2],c3=coef[3],**stats))
            # Nested identifier-level holdout: select using only the other
            # four fifths and calibrate adult proxy ratios without test adults.
            for fold in range(5):
                kind_fold,_,_=select_model(factory,anchor,dim,[fold],sex,heights,head_study)
                frames,_,_=factory([fold])
                train=split(frames,[fold],False)
                test=pd.concat(split(frames,[fold],True),ignore_index=True)
                new=selected_fit(train,anchor,kind_fold)
                old=selected_fit(train,anchor,'quadratic')
                if dim=='head':
                    # These study age groups were excluded from selection;
                    # NHANES heights come from the training portion only.
                    h=heights[heights.SEQN.mod(5)!=fold]
                    old_rows=head_ratio_errors(old,sex,h,head_study,[fold])
                    new_rows=head_ratio_errors(new,sex,h,head_study,[fold])
                    for (age,num,observed,old_ratio),(_,_,_,new_ratio) in zip(old_rows,new_rows):
                        head_audit.append(dict(fold=fold,sex=sex,age=age,n=num,observed_ratio=observed,
                                               old_ratio=old_ratio,new_ratio=new_ratio,model=kind_fold))
                for region,mask in [('short',test.x<90),('tall',test.x>=(190 if sex=='male' else 180)),
                                    ('all',np.ones(len(test),dtype=bool))]:
                    tx,ty=valid(test.x[mask],test.y[mask])
                    if len(tx)==0: continue
                    error_new=evaluate(new,tx)-ty; error_old=evaluate(old,tx)-ty
                    audit.append(dict(fold=fold,sex=sex,dimension=dim,model=kind_fold,region=region,n=len(tx),
                                      old_bias_cm=np.mean(error_old),new_bias_cm=np.mean(error_new),
                                      old_mae_cm=np.mean(abs(error_old)),new_mae_cm=np.mean(abs(error_new)),
                                      old_rmse_cm=np.sqrt(np.mean(error_old**2)),
                                      new_rmse_cm=np.sqrt(np.mean(error_new**2))))
    rows=['// Generated by tools/skeleton-source/openskeleton/26-pooled-height-models.py; do not edit.',
          '#pragma once','','namespace gtc::native::pooled_height_data {',
          '// Single Bernstein cubic (or degree-elevated quadratic); domain 50–250 cm.',
          'inline constexpr double coefficients[2][10][4] = {']
    for sex in ('male','female'):
        rows.append('    { // '+sex)
        for dim in DIMENSIONS:
            rows.append('        {'+', '.join(format(x,'.15g') for x in models[(sex,dim)])+'}, // '+dim)
        rows.append('    },')
    rows+=['};','} // namespace gtc::native::pooled_height_data','']
    audit=pd.DataFrame(audit)
    fmt=dict(sep='\t',index=False,float_format='%.9f',lineterminator='\n')
    outputs={ROOT/'native/client/pooled_height_coefficients.h':'\n'.join(rows),
             DATA/'POOLED_HEIGHT_MODELS.tsv':pd.DataFrame(report).to_csv(na_rep='NA',**fmt),
             DATA/'POOLED_HEIGHT_TAIL_HOLDOUT.tsv':audit[audit.fold==0].to_csv(**fmt),
             DATA/'POOLED_HEIGHT_TAIL_REPLICATION.tsv':audit[audit.fold==1].to_csv(**fmt),
             DATA/'POOLED_HEIGHT_NESTED_CV.tsv':audit.to_csv(**fmt),
             DATA/'POOLED_HEAD_RATIO_CV.tsv':pd.DataFrame(head_audit).to_csv(**fmt)}
    for path,content in outputs.items():
        if '--check' in sys.argv:
            if not path.exists() or not equivalent_generated(path.read_text(),content):
                raise SystemExit(f'generated file differs: {path}')
        else:
            path.write_text(content); print(path.relative_to(ROOT))


if __name__=='__main__': main()
