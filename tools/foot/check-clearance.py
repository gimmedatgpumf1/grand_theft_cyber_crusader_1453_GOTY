#!/usr/bin/env python3
"""Reject exported poses that violate the 0.1 mm surface clearance.

Checks actual triangles, including edge/face separation, and checks each bone
component is inside the temporarily closed skin. This only covers exported
meshes, not poses or profiles that were not exported.
"""
import argparse
import importlib.util
from pathlib import Path
import fcl
import numpy as np
import trimesh

parser=argparse.ArgumentParser()
parser.add_argument('poses',type=Path,help='directory produced by export-pose.cpp')
args=parser.parse_args()
spec=importlib.util.spec_from_file_location('clearance',Path(__file__).with_name('surface-clearance.py'))
clearance=importlib.util.module_from_spec(spec);spec.loader.exec_module(clearance)
folders=sorted(p for p in args.poses.iterdir() if p.is_dir())
if not folders:raise ValueError('no poses')
minimum=float('inf');failed=[]
for folder in folders:
    mesh=trimesh.load_mesh(folder/'skin.obj',process=False)
    skin=clearance.collision_object(mesh.vertices,mesh.faces)
    bones=clearance.load_bones(folder)
    closed=clearance.closed_mesh(mesh.vertices,mesh.faces)
    representatives=[]
    for name,bone in bones + clearance.load_ankle_bones(folder):
        other=clearance.collision_object(bone.vertices,bone.faces)
        result=fcl.CollisionResult()
        if fcl.collide(skin,other,fcl.CollisionRequest(),result):
            failed.append(f'{folder.name}/{name}: intersection')
            continue
        result=fcl.DistanceResult()
        distance=fcl.distance(skin,other,fcl.DistanceRequest(),result)
        minimum=min(minimum,distance)
        if distance < clearance.MINIMUM:
            failed.append(f'{folder.name}/{name}: {distance*1000:.6f} mm')
        if (folder/'ankle-bones'/f'{name}.obj').exists():
            continue  # Tibia and fibula extend through the open ankle rim.
        for part in bone.split(only_watertight=False): representatives.append((name,part.vertices[0]))
    if representatives:
        inside=closed.contains(np.array([point for _,point in representatives]))
        for (name,_),yes in zip(representatives,inside):
            if not yes: failed.append(f'{folder.name}/{name}: outside skin')
for problem in failed:print(problem)
print(f'{len(folders)} exported poses, {len(failed)} failures; minimum non-intersecting surface gap {minimum*1000:.6f} mm')
if failed:raise SystemExit(1)
