#!/usr/bin/env python3
"""Estimate orbital aperture centres from the actual simplified skull meshes.

Orthographic anterior depth reveals the two recessed eye sockets. This is a
reproducible geometric estimate, not an expert-labelled clinical landmark.
Requires numpy and scipy; only the small resulting .orbits files are shipped.
"""
from pathlib import Path
import numpy as np
from scipy import ndimage

BASE = Path(__file__).resolve().parents[1]
SIZE = 600


def load_head(path):
    vertices, faces, group = [], [], ''
    with path.open() as src:
        for line in src:
            if line.startswith('g '):
                group = line.split()[1]
            elif line.startswith('v '):
                vertices.append([float(x) for x in line.split()[1:4]])
            elif line.startswith('f ') and group == 'Head':
                faces.append([int(v.split('/')[0]) - 1 for v in line.split()[1:4]])
    return np.asarray(vertices), np.asarray(faces)


def anterior_depth(vertices, faces):
    head = vertices[np.unique(faces)]
    low, high = head[:, :2].min(axis=0), head[:, :2].max(axis=0)
    scale = (SIZE - 1) / (high - low)
    depth = np.full((SIZE, SIZE), -1e9)
    for t in vertices[faces]:
        xy = (t[:, :2] - low) * scale
        x0, y0 = np.maximum(0, np.floor(xy.min(axis=0)).astype(int))
        x1, y1 = np.minimum(SIZE - 1, np.ceil(xy.max(axis=0)).astype(int))
        a, b, c = xy
        den = (b[1]-c[1])*(a[0]-c[0]) + (c[0]-b[0])*(a[1]-c[1])
        if abs(den) < 1e-9:
            continue
        x, y = np.meshgrid(np.arange(x0, x1+1), np.arange(y0, y1+1))
        u = ((b[1]-c[1])*(x-c[0]) + (c[0]-b[0])*(y-c[1])) / den
        v = ((c[1]-a[1])*(x-c[0]) + (a[0]-c[0])*(y-c[1])) / den
        w = 1-u-v
        inside = (u >= -1e-8) & (v >= -1e-8) & (w >= -1e-8)
        z = u*t[0, 2] + v*t[1, 2] + w*t[2, 2]
        patch = depth[y0:y1+1, x0:x1+1]
        patch[inside] = np.maximum(patch[inside], z[inside])
    return np.flipud(depth), low, high


def fit(vertices, faces):
    depth, low, high = anterior_depth(vertices, faces)
    depth = np.where(depth < -1, -0.5, depth)
    # Opening is a recessed area surrounded by anterior bone. Morphology
    # removes raster noise; the largest two cavities in the face are orbits.
    nearby = ndimage.maximum_filter(depth, size=155)
    mask = ndimage.binary_opening(nearby-depth > 0.022, structure=np.ones((3, 3)))
    mask = ndimage.binary_closing(mask, structure=np.ones((7, 7)))
    y, x = np.indices(mask.shape)
    mask &= (y > SIZE*.36) & (y < SIZE*.70) & (x > SIZE*.18) & (x < SIZE*.85)
    labels, count = ndimage.label(mask)
    regions = []
    for label in range(1, count+1):
        region = labels == label
        rows, cols = np.where(region)
        if len(cols) < 5000:
            continue
        ring = ndimage.binary_dilation(region, iterations=2) & ~region & (depth > -0.4)
        if not np.any(ring):
            continue
        cx = low[0] + cols.mean()/(SIZE-1)*(high[0]-low[0])
        cy = low[1] + (SIZE-1-rows.mean())/(SIZE-1)*(high[1]-low[1])
        regions.append((len(cols), (cx, cy, depth[ring].mean())))
    if len(regions) < 2:
        raise ValueError('Unable to locate both orbital apertures')
    return sorted((p for _, p in sorted(regions, reverse=True)[:2]), key=lambda p: p[0])


for sex in ('male', 'female'):
    vertices, faces = load_head(BASE / f'skeleton-{sex}.obj')
    centres = fit(vertices, faces)
    path = BASE / f'skeleton-{sex}.orbits'
    path.write_text('# Estimated orbital aperture centres (m), from the supplied Head mesh\n' +
                    ''.join(f'{p[0]:.8f} {p[1]:.8f} {p[2]:.8f}\n' for p in centres))
    print(path.name, centres)
