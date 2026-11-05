#!/usr/bin/env python3
"""Fit the full-resolution mesh around exported default-profile bones.

Output is an experimental canonical correction, not proof of collision-free
skin or clearance at other sizes/poses. Requires numpy, scipy, trimesh, rtree
and networkx. Run on the left foot exported by export-fit.cpp.
"""
import trimesh,numpy as np,networkx as nx
from pathlib import Path
from scipy.spatial.distance import cdist
import sys
folder=Path(sys.argv[1]); skin=trimesh.load_mesh(folder/'skin.obj',process=False);nv=len(skin.vertices)
edges=skin.edges[trimesh.grouping.group_rows(skin.edges_sorted,require_count=1)];loops=nx.cycle_basis(nx.from_edgelist(edges));assert len(loops)==1
loop=loops[0]; edge_set={tuple(e) for e in edges.tolist()}; cap=[]
for i,a in enumerate(loop):
 b=loop[(i+1)%len(loop)];cap.append([b,a,nv] if (a,b) in edge_set else [a,b,nv])
faces=np.vstack([skin.faces,cap]); bones=[]
for f in folder.glob('*.obj'):
 if f.name.startswith('skin'):continue
 v=trimesh.load_mesh(f,process=False).vertices
 bones.extend(v)
bones=np.array(bones);vert=skin.vertices.copy();floor=vert[:,1].min()
for step in range(24):
 center=vert[loop].mean(axis=0); mesh=trimesh.Trimesh(vertices=np.vstack([vert,center]),faces=faces,process=False)
 outside=bones[~mesh.contains(bones)]
 if len(outside)==0:break
 closest,dist,ids=trimesh.proximity.closest_point(trimesh.Trimesh(vertices=vert,faces=skin.faces,process=False),outside)
 keep=(dist>.00002)&(dist<.025); outside=outside[keep];closest=closest[keep];dist=dist[keep]
 if not len(outside):break
 vectors=(outside-closest)*(1+.00045/np.maximum(dist,1e-9))[:,None]
 ds=cdist(vert,closest); radius=.006
 weights=np.exp(-(ds/radius)**2*1.5); weights[ds>radius*2]=0
 displace=(weights@vectors)/np.maximum(weights.sum(axis=1),1)[:,None]
 # A single damped update avoids large steps and overlapping toe webs.
 vert+=displace*.8
 print(folder.name,step,'outside',len(outside),'max mm',round(max(dist)*1000,2),'move mm',round(np.linalg.norm(displace,axis=1).max()*1000,2),flush=True)
offsets=vert-skin.vertices
np.savetxt(folder/'offsets.txt',offsets,fmt='%.12g')
if folder.name in ('m_L','f_L'):
    sex='male' if folder.name=='m_L' else 'female'
    size=bones[:,2].max()-bones[:,2].min()
    if size<=0: raise ValueError('invalid bone span')
    a=offsets/size
    lines=[f'{{{{{x:.9f}f,{y:.9f}f,{z:.9f}f}}}},' for x,y,z in a]
    out=['#pragma once','// Canonical full-resolution fitting offsets, normalized by skeletal foot length.',
         '#include <array>','namespace gtc::native::makehuman_foot_data {',
         f'inline constexpr std::array<std::array<float,3>,{len(a)}> {sex}Corrections={{{{']
    out+=['    '+''.join(lines[i:i+4]) for i in range(0,len(lines),4)]
    out+=['}};','} // namespace gtc::native::makehuman_foot_data','']
    (Path(__file__).resolve().parent/f'correction-{sex}.h').write_text('\n'.join(out))
trimesh.Trimesh(vertices=vert,faces=skin.faces,process=False).export(folder/'skin-fitted.obj')
