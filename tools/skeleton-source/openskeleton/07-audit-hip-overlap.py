"""Audit source femur/pelvis surface overlap; requires numpy, scipy, trimesh, rtree.

Point-in-solid checks are limited to femur vertices within 3 mm of the pelvis
surface and performed on the original labelled, unmodified OpenSkeleton meshes.
This is a diagnostic, not a clinical measure of full intersection volume.
"""
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
from source_mesh import source_mesh

HERE = Path(__file__).resolve().parent

def main():
    for sex, filename in (('male', 'male-N197925.npz'), ('female', 'skelly-out.npz')):
        with np.load(HERE / 'data' / filename) as source:
            for side in ('L', 'R'):
                femur = source['v_Femur_'+side]
                pelvis = source['v_Pelvis_'+side]
                mesh = source_mesh(pelvis, source['f_Pelvis_'+side])
                if not mesh.is_watertight or not mesh.is_winding_consistent:
                    raise ValueError(f'{sex} {side} pelvis is not a consistently oriented closed mesh')
                near = femur[cKDTree(pelvis).query(femur)[0] < 3]
                inside = sum(int(mesh.contains(near[i:i+16]).sum()) for i in range(0, len(near), 16))
                print(f'{sex} {side}: {inside} of {len(near)} femoral vertices within 3 mm of the pelvic surface are inside the pelvis')

if __name__ == '__main__':
    main()
