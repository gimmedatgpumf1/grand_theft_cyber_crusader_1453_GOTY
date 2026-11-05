#!/usr/bin/env python3
"""Store a successful reduced-mesh rest-space correction as C++ data."""
import argparse
import re
from pathlib import Path
import numpy as np
import trimesh

parser=argparse.ArgumentParser()
parser.add_argument('rest_pose',type=Path,help='default left-rest reduced pose export')
parser.add_argument('offsets',type=Path,help='output of fit-poses.py')
parser.add_argument('--add-existing',action='store_true',help='add to existing correction header')
args=parser.parse_args()
sex={'m0_L-rest':'male','f0_L-rest':'female'}.get(args.rest_pose.name)
if not sex: raise ValueError('expected a default m0_L-rest or f0_L-rest directory')
mesh=trimesh.load_mesh(args.rest_pose/'skin.obj',process=False)
delta=np.loadtxt(args.offsets)
if delta.shape!=mesh.vertices.shape: raise ValueError('incorrect displacement count')
length=float((args.rest_pose/'skin-matrices.txt').read_text().splitlines()[0])
rows=delta/length
header=Path(__file__).with_name(f'reduced-pose-correction-{sex}.h')
if args.add_existing:
    text=header.read_text()
    values=[float(x) for x in re.findall(r'([-+]?\d+\.\d+)f',text)]
    old=np.asarray(values).reshape(-1,3)
    if old.shape!=rows.shape: raise ValueError('existing correction does not match mesh')
    rows+=old
out=['#pragma once','// Additional reduced-mesh multi-pose offsets, normalized by foot length.',
     '#include <array>','namespace gtc::native::makehuman_foot_data {',
     f'inline constexpr std::array<std::array<float,3>,{len(rows)}> {sex}ReducedPoseCorrections={{{{']
lines=[f'{{{{{x:.9f}f,{y:.9f}f,{z:.9f}f}}}},' for x,y,z in rows]
out+=['    '+''.join(lines[i:i+4]) for i in range(0,len(lines),4)]
out+=['}};','} // namespace gtc::native::makehuman_foot_data','']
header.write_text('\n'.join(out))
