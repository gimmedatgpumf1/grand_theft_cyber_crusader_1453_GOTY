#!/usr/bin/env python3
"""Fit individual 3D mirror planes to voxelized source vertebra surfaces.

No body or hip landmarks enter the fits. Unlike the skull's exterior-only fit,
these use the whole vertebra surface; holes and thin processes are retained.
The distance-field objective and its search bounds are described in
skull_voxel_fit.py. A geometric fit is not a clinical anatomical midplane.
Requires NumPy, SciPy and trimesh.
"""
import argparse
from pathlib import Path
import numpy as np
import trimesh
from skull_voxel_fit import voxel_surface, register_voxels

ROOT = Path(__file__).resolve().parents[3]
NAMES = [f'{part}{i}' for part, count in (('C',7),('T',12),('L',5)) for i in range(1,count+1)]
PITCH = .001


def structures(path):
    vertices, faces, group, offset, result = [], [], None, 0, {}
    def save():
        if group in NAMES:
            if group in result:
                raise ValueError(f'Duplicate {group}')
            v, f = np.asarray(vertices), np.asarray(faces)
            if not len(v) or not len(f) or f.min() < 0 or f.max() >= len(v):
                raise ValueError(f'Invalid {group} mesh')
            result[group] = trimesh.Trimesh(vertices=v, faces=f, process=False)
    with path.open() as source:
        for line in source:
            if line.startswith('g '):
                save(); offset += len(vertices); vertices, faces = [], []
                group = line.split()[1]
            elif line.startswith('v '):
                vertices.append([float(x) for x in line.split()[1:4]])
            elif line.startswith('f '):
                faces.append([int(x.split('/')[0])-1-offset for x in line.split()[1:4]])
    save()
    if set(result)!=set(NAMES):
        raise ValueError('Missing vertebra structure')
    return result


def fit(sex):
    base=ROOT/'tools/skeleton-source'
    meshes=structures(base/f'skeleton-{sex}.obj')
    result=[]
    for name in NAMES:
        occupied, grid_origin=voxel_surface(meshes[name],PITCH)
        origin, normal, before, after=register_voxels(occupied,grid_origin,PITCH)
        if after>before+1e-15:
            raise ValueError(f'{sex} {name}: voxel objective worsened')
        print(f'{sex} {name}: {occupied.sum()} voxels; reflection objective '
              f'{before*1e6:.4f} -> {after*1e6:.4f} mm²',flush=True)
        result.append((name,origin,normal))
    return result


def text(rows):
    return ('# 3D planes fitted to source vertebra surface voxels (metres)\n' +
            '# bone origin_x origin_y origin_z normal_x normal_y normal_z\n' +
            ''.join(name+' '+' '.join(f'{x:.12f}' for x in np.r_[origin,normal])+'\n'
                    for name,origin,normal in rows))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--write',action='store_true')
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    for sex in ('male','female'):
        rows=fit(sex)
        path=ROOT/f'tools/skeleton-source/skeleton-{sex}.vertebra-planes'
        if args.write: path.write_text(text(rows))
        if args.check:
            stored=path.read_text().splitlines()
            lines=[s.split() for s in stored if s and not s.startswith('#')]
            if len(lines)!=len(rows): raise ValueError(f'{sex}: plane count')
            for fields,(name,origin,normal) in zip(lines,rows):
                if len(fields)!=7 or fields[0]!=name: raise ValueError(f'{sex}: plane order')
                values=np.array([float(s) for s in fields[1:]])
                angle=np.arccos(np.clip(values[3:]@normal,-1,1))
                offset=abs((values[:3]-origin)@normal)
                if angle>np.radians(.05) or offset>.0001:
                    raise ValueError(f'{sex} {name}: fitted plane changed')
            print(f'{sex}: vertebra voxel plane constants PASS',flush=True)

if __name__=='__main__': main()
