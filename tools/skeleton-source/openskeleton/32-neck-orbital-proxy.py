#!/usr/bin/env python3
"""Approximate orbitale height in the registered skull, not a tragion label.

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


def orbital_heights(sex, threshold=.022):
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
        height=low[1]+(size-1-bottom)/(size-1)*(high[1]-low[1])
        regions.append((int(region.sum()),float(x_centre),float(height)))
    if len(regions)<2:
        raise ValueError('Could not locate both orbital apertures')
    eyes=sorted(sorted(regions,reverse=True)[:2],key=lambda row:row[1])
    # +X is the fitted anatomical left shoulder for both source subjects.
    return {'L':eyes[1][2], 'R':eyes[0][2]}
