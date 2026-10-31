#!/usr/bin/env python3
"""Compare current room T-pose with d26c7b3; preserve raw mesh coordinates.

Forward +Z; nearest rib/vertebra vertices are an attachment proxy, not an
identified anatomical landmark. Surface/clinical norms are not diagnoses.
"""
import csv
import gzip
import sys
import tempfile
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import importlib.util
spec=importlib.util.spec_from_file_location('tpose',HERE/'38-measure-t-pose-arm.py')
tpose=importlib.util.module_from_spec(spec); spec.loader.exec_module(tpose)
OUT=tpose.whole.OUT
BASE=OUT/'TPOSE_BASELINE_d26c7b3.tsv.gz'

def baseline():
    result={}
    with gzip.open(BASE,'rt') as f:
        for line in f:
            t=line.rstrip().split('\t')
            if len(t)!=18 or (t[0],t[1]) in result: raise ValueError('bad baseline')
            result[t[0],t[1]]=np.asarray(t[2:],float).reshape((4,4),order='F')
    if len(result)!=364: raise ValueError('incomplete baseline')
    return result

def calculate(poses):
    rows=[]
    for sex in ('male','female'):
        mesh=tpose.whole.mesh(sex)
        for state,pose in poses.items():
            def bone(name): return tpose.transform(mesh[name],pose[sex,name])
            allcage=np.vstack([bone('Sternum')]+[bone(f'Rib{i}_{side}') for i in range(1,13) for side in ('L','R')])
            ext=np.ptp(allcage,axis=0)*100
            for i in range(1,13):
                vertebra=bone(f'T{i}')
                for side in ('L','R'):
                    rib=bone(f'Rib{i}_{side}')
                    distances,vi=cKDTree(vertebra).query(rib)
                    ri=int(np.argmin(distances)); pair=vertebra[vi[ri]]
                    # Mean of the most anterior 5% of vertices: a stable
                    # mesh-sampling proxy, not the cartilage endpoint.
                    front=rib[rib[:,2]>=np.quantile(rib[:,2],.95)].mean(axis=0)
                    rows.append(dict(sex=sex,pose=state,rib=i,side=side,
                        anterior_z_cm=front[2]*100,vertebra_contact_z_cm=pair[2]*100,
                        rib_contact_z_cm=rib[ri,2]*100,vertebra_gap_cm=distances[ri]*100,
                        front_minus_rib_contact_cm=(front[2]-rib[ri,2])*100,
                        front_minus_vertebra_contact_cm=(front[2]-pair[2])*100,
                        cage_width_cm=ext[0],cage_height_cm=ext[1],cage_depth_cm=ext[2]))
    return rows

def main():
    with tempfile.TemporaryDirectory() as d:
        exe=Path(d)/'export'; tpose.compile_exporter(exe)
        rows=calculate({'before':baseline(),'after':tpose.matrices(exe)})
    path=OUT/'RIB_PROJECTION_AUDIT.tsv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,rows[0].keys(),delimiter='\t',lineterminator='\n');writer.writeheader()
        for row in rows:
            writer.writerow({k:(f'{v:.6f}' if isinstance(v,(float,np.floating)) else v) for k,v in row.items()})
    print(path)
if __name__=='__main__': main()
