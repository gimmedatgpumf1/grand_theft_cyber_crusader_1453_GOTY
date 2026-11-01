#!/usr/bin/env python3
"""Triangle intersection audit for room shoulder bones (requires python-fcl)."""
import importlib.util
import tempfile
from pathlib import Path
import numpy as np
import fcl
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'tools/skeleton-source'
spec=importlib.util.spec_from_file_location('tpose',SRC/'openskeleton/38-measure-t-pose-arm.py')
tpose=importlib.util.module_from_spec(spec);spec.loader.exec_module(tpose)

def meshes(sex):
    vertices=[];faces={};group=None
    with (SRC/f'skeleton-{sex}.obj').open() as f:
        for line in f:
            if line.startswith('g '): group=line.split()[1]
            elif line.startswith('v '): vertices.append([float(x) for x in line.split()[1:4]])
            elif line.startswith('f ') and group:
                faces.setdefault(group,[]).append([int(x.split('/')[0])-1 for x in line.split()[1:4]])
    return np.asarray(vertices),faces

def model(vertices,faces,matrix):
    triangles=np.asarray(faces,dtype=np.int32);ids,inverse=np.unique(triangles,return_inverse=True)
    points=tpose.transform(vertices[ids],matrix)
    mesh=fcl.BVHModel();mesh.beginModel(len(points),len(triangles))
    mesh.addSubModel(points,inverse.reshape(-1,3).astype(np.int32));mesh.endModel()
    return fcl.CollisionObject(mesh)

with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'pose';tpose.compile_exporter(exe);pose=tpose.matrices(exe)
for sex in ('male','female'):
    vertices,faces=meshes(sex); objects={}
    def obj(name):
        if name not in objects: objects[name]=model(vertices,faces[name],pose[sex,name])
        return objects[name]
    for side in 'LR':
        for part in ('Clavicula','Scapula'):
            name=f'{part}_{side}'
            others=[f'Rib{i}_{side}' for i in range(1,13)]+['Sternum']
            others += [f'Scapula_{side}' if part=='Clavicula' else f'Humerus_{side}']
            for other in others:
                result=fcl.CollisionResult()
                assert not fcl.collide(obj(name),obj(other),fcl.CollisionRequest(num_max_contacts=1),result), (sex,name,other)
print('T-pose shoulder/ipsilateral ribs, sternum, AC and shoulder mesh surface intersections: none')
