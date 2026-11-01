#!/usr/bin/env python3
"""Describe cross-subject orientation, not an anatomical normality test.

Both original OpenSkeleton NPZs use corresponding per-structure topology.
Kabsch fit maps female to male, after each bone's centroid is removed. Shape
variation influences the fit; its rotation is not a clinical posture measure.
"""
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation

BASE=Path(__file__).resolve().parent/'data'
male=np.load(BASE/'male-N197925.npz')
female=np.load(BASE/'skelly-out.npz')


def coords(data,bone):
    v=data['v_'+bone]
    return np.column_stack((v[:,0],v[:,2],-v[:,1])) # millimetres


def rigid_alignment(a,b):
    a=a-a.mean(axis=0); b=b-b.mean(axis=0)
    u,_,vt=np.linalg.svd(b.T@a)
    r=vt.T@np.diag([1,1,np.linalg.det(vt.T@u.T)])@u.T
    residual=np.sqrt(np.mean(np.sum((b@r.T-a)**2,axis=1)))
    return r,residual

for bone in ('C1','C7','T1','T2','Head','Jaw'):
    a,b=coords(male,bone),coords(female,bone)
    if len(a)!=len(b): raise ValueError('non-corresponding vertices')
    r,error=rigid_alignment(a,b)
    angles=Rotation.from_matrix(r).as_euler('xyz',degrees=True)
    print(f'{bone:5s} female_to_male_xyz_deg={angles.round(2)} rms_shape_residual_mm={error:.2f}')
for sex,data in [('male',male),('female',female)]:
    c7=coords(data,'C7'); tip=c7[np.argmin(c7[:,2])]
    print(f'{sex:6s} C7_posterior_tip_minus_centroid_mm={(tip-c7.mean(axis=0)).round(2)}')
