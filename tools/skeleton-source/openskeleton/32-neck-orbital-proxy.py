#!/usr/bin/env python3
"""Explore approximate skull/jaw landmarks, not actual tragion labels.

Uses the existing orbital cavity detector and its 600-pixel anterior depth map.
Needs NumPy and SciPy. See WHOLE_BODY_HEIGHT_COMPARISON.md before interpreting.
"""
import importlib.util
from pathlib import Path
import numpy as np
from scipy import ndimage

HERE=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('orbit_fit', HERE/'14-fit-orbits.py')
ORB=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ORB)


def orbital_points(sex, threshold=.022):
    vertices,faces=ORB.load_head(HERE.parent/f'skeleton-{sex}.obj')
    depth,low,high=ORB.anterior_depth(vertices,faces)
    depth=np.where(depth < -1,-.5,depth)
    nearby=ndimage.maximum_filter(depth,size=155)
    mask=ndimage.binary_opening(nearby-depth>threshold,structure=np.ones((3,3)))
    mask=ndimage.binary_closing(mask,structure=np.ones((7,7)))
    y,x=np.indices(mask.shape); size=ORB.SIZE
    mask &= (y>size*.36)&(y<size*.70)&(x>size*.18)&(x<size*.85)
    labels,count=ndimage.label(mask)
    regions=[]
    for label in range(1,count+1):
        region=labels==label
        if region.sum()<5000: continue
        ring=ndimage.binary_dilation(region,iterations=2)&~region&(depth>-.4)
        rows,cols=np.where(ring)
        if len(rows)==0: continue
        bottom=rows.max()
        x_centre=cols.mean()
        col=int(np.median(cols[rows==bottom]))
        point=np.array([low[0]+col/(size-1)*(high[0]-low[0]),
                        low[1]+(size-1-bottom)/(size-1)*(high[1]-low[1]),
                        depth[bottom,col]])
        regions.append((int(region.sum()),float(x_centre),point))
    if len(regions)<2:
        raise ValueError('Could not locate both orbital apertures')
    eyes=sorted(sorted(regions,reverse=True)[:2],key=lambda row:row[1])
    # +X is the fitted anatomical left shoulder for both source subjects.
    return {'L':eyes[1][2], 'R':eyes[0][2]}


def jaw_condyles(head, jaw):
    # The max-Y point on the posterior 15% of each jaw half is a geometric
    # condylion proxy. Not expert labelled; global Y is pose-dependent.
    mid=(head[:,0].min()+head[:,0].max())/2
    points={}
    for side,sign in [('L',1),('R',-1)]:
        half=jaw[(jaw[:,0]-mid)*sign>.02]
        if len(half)<30: raise ValueError('insufficient jaw vertices')
        posterior=half[half[:,2]<=np.quantile(half[:,2],.15)]
        points[side]=posterior[np.argmax(posterior[:,1])]
    return points


def relevel(orbital,condyles,pivot,c7):
    # Estimate sagittal pitch by setting the mean orbital-condylar line level.
    # A condyle is not porion: level is an assumption, not a validated FH plane.
    eye=(orbital['L']+orbital['R'])/2
    cond=(condyles['L']+condyles['R'])/2
    dy=eye[1]-cond[1]; dz=eye[2]-cond[2]
    if dz<=0.01: raise ValueError('orbital/condylar geometry is implausible')
    angle=np.arctan2(dy,dz); c=np.cos(angle);s=np.sin(angle)
    def height(p):
        q=p-pivot
        return 100*(pivot[1]+c*q[1]-s*q[2]-c7[1])
    return angle,{side:height(condyles[side]) for side in ('L','R')}
