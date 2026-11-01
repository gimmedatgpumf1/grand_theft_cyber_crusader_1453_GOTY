#!/usr/bin/env python3
"""Measure the restored game pose and the registered source; never modify bones.

The posterior plane is an approximate thorax frame, NOT a reproduction of
Robinson et al.'s CT landmark protocol. Values cannot determine respiratory phase.
"""
import csv
import gzip
import importlib.util
import tempfile
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('tpose',HERE/'38-measure-t-pose-arm.py')
tpose=importlib.util.module_from_spec(spec); spec.loader.exec_module(tpose)
OUT=tpose.whole.OUT
BASE=OUT/'TPOSE_BASELINE_d26c7b3.tsv.gz'
CT={'male':(19.532,1.752,30.278,2.191,1225),
    'female':(17.766,1.517,27.543,1.982,494)}

def baseline():
    result={}
    with gzip.open(BASE,'rt') as f:
        for line in f:
            t=line.rstrip().split('\t')
            if len(t)!=18 or (t[0],t[1]) in result: raise ValueError('bad baseline')
            result[t[0],t[1]]=np.asarray(t[2:],float).reshape((4,4),order='F')
    if len(result)!=364: raise ValueError('incomplete baseline')
    return result

def frame(ribs, fraction=.01):
    # Posterior-Z centroids on each rib; fit z = a*x + b*y + c. This is
    # sensitive to how the meshes were segmented, unlike a clinical landmark.
    p=np.array([r[r[:,2]<=np.quantile(r[:,2],fraction)].mean(axis=0) for r in ribs])
    a,b,_=np.linalg.lstsq(np.c_[p[:,0],p[:,1],np.ones(len(p))],p[:,2],rcond=None)[0]
    normal=np.array([-a,-b,1.]); normal/=np.linalg.norm(normal)
    up=np.array([0.,1.,0.]); up-=np.dot(up,normal)*normal; up/=np.linalg.norm(up)
    right=np.cross(up,normal)
    return right,up,normal

def calculate(poses):
    projections=[]; cages=[]
    for sex in ('male','female'):
        mesh=tpose.whole.mesh(sex)
        for state,pose in poses.items():
            def bone(name): return tpose.transform(mesh[name],pose[sex,name])
            ribs=[bone(f'Rib{i}_{side}') for i in range(1,13) for side in ('L','R')]
            cage=np.vstack(ribs+[bone('Sternum')]);right,up,normal=frame(ribs)
            w,h,d=[np.ptp(cage@axis)*100 for axis in (right,up,normal)]
            raw=np.ptp(cage,axis=0)*100
            sensitivity=[np.ptp(cage@frame(ribs,q)[2])*100 for q in (0,.05)]
            ref,sd,refwidth,sdwidth,n=CT[sex]
            cages.append(dict(sex=sex,pose=state,depth_global_z_cm=raw[2],width_global_x_cm=raw[0],
                height_global_y_cm=raw[1],depth_posterior_plane_cm=d,width_posterior_plane_cm=w,
                height_posterior_plane_cm=h,depth_extreme_proxy_cm=sensitivity[0],
                depth_five_percent_proxy_cm=sensitivity[1],depth_width_ratio=d/w,
                ct_depth_mean_cm=ref,ct_depth_sd_cm=sd,ct_width_mean_cm=refwidth,ct_width_sd_cm=sdwidth,
                ct_sex_n=n,unmatched_depth_minus_ct_mean_cm=d-ref))
            for i in range(1,13):
                vertebra=bone(f'T{i}')
                for side in ('L','R'):
                    rib=bone(f'Rib{i}_{side}');dist,vi=cKDTree(vertebra).query(rib)
                    ri=int(np.argmin(dist)); pair=vertebra[vi[ri]]
                    front=rib[rib[:,2]>=np.quantile(rib[:,2],.95)].mean(axis=0)
                    projections.append(dict(sex=sex,pose=state,rib=i,side=side,
                        anterior_z_cm=front[2]*100,vertebra_contact_z_cm=pair[2]*100,
                        rib_contact_z_cm=rib[ri,2]*100,vertebra_gap_cm=dist[ri]*100,
                        front_minus_vertebra_global_z_cm=(front[2]-pair[2])*100,
                        front_minus_vertebra_plane_cm=np.dot(front-pair,normal)*100))
    return projections,cages

def write(path,rows):
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,rows[0].keys(),delimiter='\t',lineterminator='\n');writer.writeheader()
        for row in rows:
            writer.writerow({k:(f'{v:.6f}' if isinstance(v,(float,np.floating)) else v) for k,v in row.items()})
    print(path)

def main():
    with tempfile.TemporaryDirectory() as d:
        exe=Path(d)/'export';tpose.compile_exporter(exe)
        pose=tpose.matrices(exe); source=tpose.matrices(exe,('--source',))
        old=baseline()
        if not all(np.allclose(pose[k],old[k],atol=1e-10,rtol=0) for k in old):
            raise ValueError('T-pose no longer matches the requested d26c7b3 restoration')
        projections,cages=calculate({'registered_source':source,'restored_tpose':pose})
    write(OUT/'RIB_PROJECTION_AUDIT.tsv',projections)
    write(OUT/'RIB_CAGE_COMPARISON.tsv',cages)

if __name__=='__main__': main()
