#!/usr/bin/env python3
"""Save a successful canonical high-resolution pose fit and its C++ offsets."""
import argparse
from pathlib import Path
import numpy as np
import trimesh

parser = argparse.ArgumentParser()
parser.add_argument('fit', type=Path, help='m_L/f_L exporter directory with skin-fitted.obj')
parser.add_argument('rest_pose', type=Path, help='matching default left-rest pose export')
parser.add_argument('offsets', type=Path, help='output of fit-poses.py')
args = parser.parse_args()
sex = {'m_L': 'male', 'f_L': 'female'}.get(args.fit.name)
if not sex:
    raise ValueError('expected m_L or f_L directory')
mesh = trimesh.load_mesh(args.fit / 'skin-fitted.obj', process=False)
posed = trimesh.load_mesh(args.rest_pose / 'skin.obj', process=False)
delta = np.loadtxt(args.offsets)
length = float((args.rest_pose / 'skin-matrices.txt').read_text().splitlines()[0])
if delta.shape != mesh.vertices.shape or posed.vertices.shape != mesh.vertices.shape:
    raise ValueError('incompatible mesh or displacement size')
if np.linalg.norm(posed.vertices-mesh.vertices,axis=1).max() > 1e-6:
    raise ValueError('pose export does not match the fitted mesh; regenerate exports')
rows = delta / length
out = ['#pragma once', '// Additional multi-pose offsets, normalized by skeletal foot length.',
       '#include <array>', 'namespace gtc::native::makehuman_foot_data {',
       f'inline constexpr std::array<std::array<float,3>,{len(rows)}> {sex}PoseCorrections={{{{']
lines = [f'{{{{{x:.9f}f,{y:.9f}f,{z:.9f}f}}}},' for x,y,z in rows]
out += ['    '+''.join(lines[i:i+4]) for i in range(0,len(lines),4)]
out += ['}};', '} // namespace gtc::native::makehuman_foot_data', '']
Path(__file__).with_name(f'pose-correction-{sex}.h').write_text('\n'.join(out))
trimesh.Trimesh(vertices=mesh.vertices+delta, faces=mesh.faces, process=False).export(args.fit / 'skin-pose-fitted.obj')
