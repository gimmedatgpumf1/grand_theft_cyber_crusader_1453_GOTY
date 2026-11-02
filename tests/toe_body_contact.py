#!/usr/bin/env python3
"""Strict diagnostic for exported experimental toes; nonzero means not collision-clean.

Requires numpy, trimesh, rtree and python-fcl. Expects output from
`toe_body_export <dir> [height_cm] [digit] [joint]`. Uses millimetres for
collision/containment queries, so small-profile numerical tolerances are sane.
A floor-touching bone is reported as a failure, not hidden as clearance.
"""
import glob
import os
import sys

import numpy as np
import trimesh


def load(path):
    mesh = trimesh.load(path, process=False)
    mesh.apply_scale(1000.0)
    return mesh


def audit(folder, sex):
    skin = load(os.path.join(folder, sex + '_skin.obj'))
    nails = load(os.path.join(folder, sex + '_nails.obj'))
    problems = 0
    for name, mesh in [('skin', skin), ('nails', nails)]:
        ok = mesh.is_watertight and mesh.is_winding_consistent and np.isfinite(mesh.vertices).all()
        print(f'{sex} {name}: faces={len(mesh.faces)} closed/winding={bool(ok)}')
        problems += not ok
    manager = trimesh.collision.CollisionManager()
    manager.add_object('skin', skin)
    nail_hit, nail_contacts = manager.in_collision_single(nails, return_data=True)
    print(f'{sex} nail/skin contacts: {len(nail_contacts)}')
    problems += nail_hit
    bones = sorted(glob.glob(os.path.join(folder, sex + '_Foot*.obj')))
    if len(bones) != 14:
        print(f'{sex} expected 14 phalanges, found {len(bones)}')
        problems += 1
    for path in bones:
        bone = load(path)
        outside = bone.vertices[~skin.contains(bone.vertices)]
        hit, contacts = manager.in_collision_single(bone, return_data=True)
        if len(outside) or hit:
            print(f'{os.path.basename(path)}: outside={len(outside)} '
                  f'surface_contacts={len(contacts)} minimum_y_mm={bone.vertices[:, 1].min():.6g}')
            problems += 1
    return problems


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: toe_body_contact.py <export-directory>')
    issues = sum(audit(sys.argv[1], sex) for sex in ('male', 'female'))
    print(f'problems={issues}; zero is required for clearance')
    sys.exit(bool(issues))
