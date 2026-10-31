#!/usr/bin/env python3
"""Remeasure OBJ proxies and compare arithmetically, NOT a clinical fit.

Requires NumPy and a C++17 compiler. Registered pose and measurement choices
are explained in WHOLE_BODY_HEIGHT_COMPARISON.md. Run from any directory.
"""
import csv
import hashlib
import subprocess
import tempfile
from pathlib import Path
import numpy as np

SOURCE=Path(__file__).resolve().parents[1]
REPO=SOURCE.parents[1]
OUT=SOURCE/'anthropometry'


def mesh(sex):
    groups={}; group=None
    with (SOURCE/f'skeleton-{sex}.obj').open() as f:
        for line in f:
            if line.startswith('g '):
                group=line.split()[1]
                if group in groups: raise ValueError('duplicate OBJ group '+group)
                groups[group]=[]
            elif group and line.startswith('v '):
                groups[group].append([float(x) for x in line.split()[1:4]])
    return {key:np.asarray(value,dtype=float) for key,value in groups.items()}


def joints(sex,kind):
    result={}
    for line in (SOURCE/f'skeleton-{sex}.{kind}').read_text().splitlines():
        if line and not line.startswith('#'):
            tokens=line.split(); result[tokens[0]]=np.asarray([float(x) for x in tokens[1:4]])
    if set(result)!={'L','R'}: raise ValueError('invalid joint file')
    return result


def distances(points):
    return sum(np.linalg.norm(b-a) for a,b in zip(points,points[1:]))*100


def measure(sex):
    d=mesh(sex); hip=joints(sex,'hips'); shoulder=joints(sex,'shoulders')
    elbow=joints(sex,'elbows'); knee=joints(sex,'knees')
    # Vertex/menton-like extrema in registered pose, not actual skin landmarks.
    head=(d['Head'][:,1].max()-d['Jaw'][:,1].min())*100
    # C1-C7 vertical extent includes spinous processes; not a neck-link value.
    neck=(d['C1'][:,1].max()-d['C7'][:,1].min())*100
    # Posterior-most C7 point and bottom-most pelvis mesh, vertical projection.
    c7=d['C7'][np.argmin(d['C7'][:,2])]
    bottom=min(d['Pelvis_L'][:,1].min(),d['Pelvis_R'][:,1].min())
    torso=(c7[1]-bottom)*100
    arms=[]; legs=[]
    for side in ('L','R'):
        radius=d[f'Radius_{side}']
        axis=np.linalg.svd(radius-radius.mean(axis=0),full_matrices=False)[2][0]
        projection=radius@axis
        if abs(projection.max()-elbow[side]@axis)<abs(projection.min()-elbow[side]@axis): axis=-axis
        projection=radius@axis
        wrist=radius[projection>np.quantile(projection,.98)].mean(axis=0)
        pts=[shoulder[side],elbow[side],wrist]+[d[f'Hand{part}3_{side}'].mean(axis=0)
                                                 for part in ('Meta','ProxP','MidP','DistP')]
        finger=d[f'HandDistP3_{side}']; direction=pts[-1]-pts[-2]
        direction/=np.linalg.norm(direction)
        pts.append(finger[np.argmax(finger@direction)])
        arms.append(distances(pts))
        fibula=d[f'Fibula_{side}']
        axis=np.linalg.svd(fibula-fibula.mean(axis=0),full_matrices=False)[2][0]
        projection=fibula@axis
        if abs(projection.max()-knee[side]@axis)<abs(projection.min()-knee[side]@axis): axis=-axis
        ankle=fibula[np.argmax(fibula@axis)]
        # Add a vertical malleolus-to-bottom-of-calcaneus bone proxy. The
        # registered pose and missing plantar tissue mean this is not a floor
        # measurement, even though the reference uses trochanterion height.
        heel_y=d[f'Cal_{side}'][:,1].min()
        legs.append(distances([hip[side],knee[side],ankle])+100*(ankle[1]-heel_y))
    return {'head':(head,None,None),'neck':(neck,None,None),'torso':(torso,None,None),
            'arm':(sum(arms)/2,*arms),'leg':(sum(legs)/2,*legs)}


def verify_fits():
    # Independently recompute the three adult forward fits from paired data.
    rows=list(csv.DictReader((OUT/'ANSUR_II_PAIRED_MM.csv').open()))
    expected={
        'male': {'torso':(9.824626889078571,.3263433653317256),
                 'arm':(-1.944431609432535,.45885024218626663),
                 'leg':(-16.99483649702122,.6097644465657162)},
        'female': {'torso':(8.831677988700205,.32241520461333356),
                   'arm':(-3.3344315178830297,.465454824499171),
                   'leg':(-12.69145825107915,.5970413778726937)}}
    for sex in ('male','female'):
        rs=[r for r in rows if r['Gender'].lower()==sex and float(r['Age'])>=18]
        h=np.array([float(r['stature'])/10 for r in rs])
        for name,(one,two) in {'arm':('sleeveoutseam','handlength'),
                               'leg':('trochanterionheight',None),
                               'torso':('cervicaleheight','crotchheight')}.items():
            sign=1 if name=='arm' else -1
            y=np.array([(float(r[one])+(sign*float(r[two]) if two else 0))/10 for r in rs])
            slope,intercept=np.polyfit(h,y,1)
            if not np.allclose([intercept,slope],expected[sex][name],rtol=0,atol=1e-10):
                raise AssertionError(f'{sex} {name} coefficients changed')


def main():
    verify_fits()
    output=[]
    with tempfile.TemporaryDirectory() as td:
        binary=Path(td)/'profile'
        subprocess.run(['c++','-std=c++17','-O2','-I',str(REPO/'native'),
                        str(SOURCE/'openskeleton/31-height-profile.cpp'),'-o',str(binary)],check=True)
        for sex,subject,h in [('male','N197925',170.0),('female','N186416',170.2)]:
            dimensions=measure(sex)
            for model in ('male','female'):
                text=subprocess.check_output([str(binary),model,str(h)],text=True)
                predictions={k:float(v) for k,v in (line.split('\t') for line in text.splitlines())}
                for name,(value,left,right) in dimensions.items():
                    pred=predictions[name]
                    # No arithmetic for neck: source did not specify its landmarks.
                    diff='NA' if name=='neck' else f'{value-pred:.6f}'
                    pct='NA' if name=='neck' else f'{100*(value/pred-1):.4f}'
                    output.append(dict(subject=subject,sex=sex,formula_sex=model,height_cm=f'{h:.1f}',
                                       dimension=name,proxy_cm=f'{value:.6f}',
                                       left_cm=f'{left:.6f}' if left is not None else 'NA',
                                       right_cm=f'{right:.6f}' if right is not None else 'NA',
                                       formula_cm=f'{pred:.6f}',unmatched_difference_cm=diff,
                                       unmatched_difference_percent=pct))
    path=OUT/'WHOLE_BODY_SKELETON_PROXIES.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=output[0].keys(),delimiter='\t',lineterminator='\n')
        writer.writeheader(); writer.writerows(output)
    for sex in ('male','female'):
        p=SOURCE/f'skeleton-{sex}.obj';print(sex,hashlib.sha256(p.read_bytes()).hexdigest())
    print('wrote',path)

if __name__=='__main__': main()
