#!/usr/bin/env python3
"""Simplify a fitted full-resolution foot, retaining its source correspondence.

Input is skin-fitted.obj produced by fit-bone-clearance.py. It must have the
same topology as the extracted full-resolution mesh. The mapping allows a
profile-specific high-resolution fit to be sampled before rendering its
reduced topology. Requires numpy, scipy, trimesh, rtree, networkx, python-fcl and
fast-simplification.
"""
import argparse
import importlib.util
from pathlib import Path
import fast_simplification
import numpy as np
import trimesh

parser=argparse.ArgumentParser()
parser.add_argument('folder',type=Path,help='m_L or f_L directory from the offline fit')
parser.add_argument('--target',type=int,default=1400)
parser.add_argument('--fitted',default='skin-fitted.obj',help='fitted mesh filename within folder')
args=parser.parse_args()
sex={'m_L':'male','f_L':'female'}.get(args.folder.name)
if not sex: raise SystemExit('expected a left-foot m_L or f_L directory')
high=trimesh.load_mesh(args.folder/args.fitted,process=False)
if high.vertices.shape[0] not in (1096,1094):raise SystemExit('unexpected source topology')
low,faces=fast_simplification.simplify(np.asarray(high.vertices),np.asarray(high.faces),target_count=args.target)
# Reduction may move triangles inward. Enforce the same surface clearance
# again on the reduced geometry before recording its correspondence.
spec=importlib.util.spec_from_file_location('surface_clearance',Path(__file__).with_name('surface-clearance.py'))
clearance=importlib.util.module_from_spec(spec);spec.loader.exec_module(clearance)
low=clearance.fit_clearance(low,faces,clearance.load_bones(args.folder))
closest,dist,triangle=trimesh.proximity.closest_point(high,low)
triangles=high.faces[triangle]
bary=trimesh.triangles.points_to_barycentric(high.triangles[triangle],closest)
bones=[]
for f in args.folder.glob('*.obj'):
    if f.name.startswith('skin'):continue
    bones.extend(trimesh.load_mesh(f,process=False).vertices)
bones=np.array(bones); size=bones[:,2].max()-bones[:,2].min()
offset=(low-closest)/size
out=['#pragma once','// Generated from a fitted (not source) MakeHuman foot.','#include "reduced-foot-type.h"',
     'namespace gtc::native::makehuman_foot_data {',
     f'inline constexpr std::array<ReducedVertex,{len(low)}> {sex}ReducedVertices={{{{']
lines=[]
for ids,w,d in zip(triangles,bary,offset):
    ii=','.join(str(int(x)) for x in ids)
    ww=','.join(f'{x:.9f}f' for x in w)
    dd=','.join(f'{x:.9f}f' for x in d)
    lines.append(f'{{{{{{{ii}}}}},{{{{{ww}}}}},{{{{{dd}}}}}}},')
out+=['    '+''.join(lines[i:i+3]) for i in range(0,len(lines),3)]
out+=['}};',f'inline constexpr std::array<std::array<int,3>,{len(faces)}> {sex}ReducedFaces={{{{']
lines=[f'{{{{{a},{b},{c}}}}},' for a,b,c in faces]
out+=['    '+''.join(lines[i:i+6]) for i in range(0,len(lines),6)]
out+=['}};','} // namespace gtc::native::makehuman_foot_data','']
Path(__file__).with_name(f'reduced-foot-{sex}.h').write_text('\n'.join(out))
print(f'{sex}: fitted {len(high.faces)} triangles -> {len(faces)}; max approximation {1000*dist.max():.3f} mm')
