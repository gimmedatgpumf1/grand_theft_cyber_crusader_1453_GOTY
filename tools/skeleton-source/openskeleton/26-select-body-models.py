#!/usr/bin/env python3
"""Choose body-part model families via training-only grouped cross-validation.

Writes evaluable supported-range models and a transparent 50 cm stress report.
No extrapolated values are authorized for use as infant predictions.
"""
import argparse
import importlib.util
from pathlib import Path
import numpy as np
import pandas as pd
from scipy.interpolate import PchipInterpolator

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('curves25',HERE/'25-body-part-curves.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
ROOT=base.ROOT
METHODS=['linear','log','power','quadratic','pchip']
FOLDS=5


def fit(method, data, derived):
    x=data.height.to_numpy(float); y=data.value.to_numpy(float)
    if derived: y=x-y # fit sitting height; subtract it at evaluation
    if method=='pchip':
        d=pd.DataFrame({'height':x,'value':y}); k=base.knots(d)
        if len(k)<2: return None
        return PchipInterpolator(k.x,k.y,extrapolate=False)
    if method=='linear':
        b,a=np.polyfit(x-150,y,1); return (a,b,0.)
    if method=='quadratic':
        c,b,a=np.polyfit(x-150,y,2); return (a,b,c)
    if method=='log':
        b,a=np.polyfit(np.log(x/150),y,1); return (a,b,0.)
    if method=='power':
        b,a=np.polyfit(np.log(x/150),np.log(y),1); return (np.exp(a),b,0.)
    raise ValueError(method)


def predict(method,model,x,derived):
    x=np.asarray(x,dtype=float)
    with np.errstate(over='ignore',invalid='ignore'):
        if method=='pchip': y=model(x)
        elif method=='linear': y=model[0]+model[1]*(x-150)
        elif method=='quadratic': y=model[0]+model[1]*(x-150)+model[2]*(x-150)**2
        elif method=='log': y=model[0]+model[1]*np.log(x/150)
        elif method=='power': y=model[0]*(x/150)**model[1]
        else: raise ValueError(method)
    return x-y if derived else y


def main(anthrokids):
    models=[]; comparisons=[]; audit=[]
    for source,frame,dims in base.records(anthrokids):
        rng=np.random.default_rng(1453);frame=frame.copy();ids=frame.split_id.unique()
        assign=dict(zip(ids,rng.random(len(ids))<0.8));frame['train']=frame.split_id.map(assign)
        train_ids=frame.loc[frame.train,'split_id'].unique(); rng.shuffle(train_ids)
        folds={v:i%FOLDS for i,v in enumerate(train_ids)}; frame['fold']=frame.split_id.map(folds)
        for sex in ('male','female'):
            for age in ('under_18','adult_18_plus'):
                g=frame[(frame.sex==sex)&(frame.age_group==age)]
                for column,dimension in dims.items():
                    d=g[['height',column,'train','fold']].rename(columns={column:'value'}).dropna(subset=['height','value'])
                    d=d[np.isfinite(d.height)&np.isfinite(d.value)&(d.height>0)&(d.value>0)]
                    allknots=base.knots(d); train=d[d.train];test=d[~d.train]
                    if len(allknots)<2 or len(train)<100: continue
                    derived=dimension=='height_minus_sitting'; errors={m:[] for m in METHODS}
                    for fold in range(FOLDS):
                        tr=train[train.fold!=fold]; val=train[train.fold==fold]
                        k=base.knots(tr);mask=base.in_support(val.height.to_numpy(),k);val=val.iloc[np.flatnonzero(mask)]
                        if len(k)<2 or val.empty: continue
                        xx=val.height.to_numpy();yy=val.value.to_numpy()
                        for method in METHODS:
                            model=fit(method,tr,derived)
                            pred=predict(method,model,xx,derived)
                            errors[method].extend(np.abs(pred-yy))
                    if min(map(len,errors.values()))<50: continue
                    # Reject numerically impossible fits anywhere on the supported
                    # interior, without forcing monotonicity or a global law.
                    trainknots=base.knots(train); tx=trainknots.x.to_numpy(); xk=allknots.x.to_numpy()
                    grid=np.concatenate([np.linspace(tx[i],tx[i+1],33)
                        for i in range(len(tx)-1) if tx[i+1]-tx[i]<=base.MAX_GAP])
                    eligible={}
                    for method in METHODS:
                        model=fit(method,train,derived); v=predict(method,model,grid,derived)
                        score=np.mean(errors[method])
                        eligible[method]=np.isfinite(v).all() and (v>0).all() and (v<grid).all() and np.isfinite(score)
                    scores={m:float(np.mean(errors[m])) for m in METHODS if eligible[m]}
                    if not scores: continue
                    best=min(scores,key=lambda m:(scores[m],METHODS.index(m)))
                    sorted_scores=sorted(scores.values());gap=sorted_scores[1]-sorted_scores[0] if len(sorted_scores)>1 else np.nan
                    # Unseen test data have no role in choosing a method.
                    holdknots=base.knots(train); mask=base.in_support(test.height.to_numpy(),holdknots)
                    hold=test.iloc[np.flatnonzero(mask)]; hx=hold.height.to_numpy();hy=hold.value.to_numpy()
                    full=fit(best,d,derived)
                    fullgrid=np.concatenate([np.linspace(xk[i],xk[i+1],33) for i in range(len(xk)-1) if xk[i+1]-xk[i]<=base.MAX_GAP])
                    fullv=predict(best,full,fullgrid,derived)
                    if not (np.isfinite(fullv).all() and (fullv>0).all() and (fullv<fullgrid).all()):
                        raise ValueError(f'Final selected model fails support sanity: {source} {sex} {age} {dimension}')
                    coef=(0.,0.,0.) if best=='pchip' else full
                    models.append(dict(source=source,sex=sex,age_group=age,dimension=dimension,method=best,
                                       coef0=coef[0],coef1=coef[1],coef2=coef[2]))
                    raw50=predict(best,full,[50.],derived)[0] if best!='pchip' else np.nan
                    audit.append(dict(source=source,sex=sex,age_group=age,dimension=dimension,
                        selected=best,n_total=len(d),n_train=len(train),n_cv=len(errors[best]),
                        cv_mae_cm=scores[best],runner_up_gap_cm=gap,n_holdout=len(hold),
                        holdout_mae_cm=np.mean(abs(predict(best,fit(best,train,derived),hx,derived)-hy)) if len(hold) else np.nan,
                        first_knot_cm=xk[0],last_knot_cm=xk[-1],diagnostic_untrusted_at_50_cm=raw50,
                        supported_at_50=bool(base.in_support(np.array([50.]),allknots)[0]),
                        support_note='Never use outside supported intervals; 50 cm is not supported'))
                    for method in METHODS:
                        comparisons.append(dict(source=source,sex=sex,age_group=age,dimension=dimension,
                            method=method,eligible=eligible[method],n_cv=len(errors[method]),
                            cv_mae_cm=np.mean(errors[method]),chosen=method==best,
                            n_holdout=len(hold),holdout_mae_cm=np.mean(abs(predict(method,fit(method,train,derived),hx,derived)-hy)) if len(hold) else np.nan))
    pd.DataFrame(models).to_csv(ROOT/'BODY_PART_SELECTED_MODELS.tsv',sep='\t',index=False,float_format='%.12g',lineterminator='\n')
    pd.DataFrame(audit).to_csv(ROOT/'BODY_PART_MODEL_SELECTION.tsv',sep='\t',index=False,float_format='%.9f',lineterminator='\n')
    pd.DataFrame(comparisons).to_csv(ROOT/'BODY_PART_MODEL_CV.tsv',sep='\t',index=False,float_format='%.9f',lineterminator='\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('anthrokids_csv',type=Path);main(p.parse_args().anthrokids_csv)
