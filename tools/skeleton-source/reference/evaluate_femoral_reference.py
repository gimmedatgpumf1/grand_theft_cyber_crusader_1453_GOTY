"""Compare atlas head estimates with 5 raters' repeated clinical landmarks.

Inputs are separately obtained VSDFullBodyBoneModels MAT files (CC BY-NC-SA 4.0;
not included in this repository), pinned at the commit in femoral/README.md.
Usage: python3 evaluate_femoral_reference.py /path/to/medical/reference
The directory must contain Bones/{subject}.mat and ManualLandmarks/Femur/Rater{1..5}.mat.
"""
from pathlib import Path
import runpy
import sys
import numpy as np
from scipy.io import loadmat

HERE = Path(__file__).resolve().parent
source = Path(sys.argv[1])
fit = runpy.run_path(str(HERE.parent / 'openskeleton/06-fit-hips.py'))
template = loadmat(HERE / 'femoral/template.mat', simplify_cells=True)['template']
areas = loadmat(HERE / 'femoral/template_areas.mat', simplify_cells=True)['areas']
mask = next(row[1] for row in areas if row[0] == 'Head').astype(bool)
vertices = template['vertices']
head = vertices[np.unique(template['faces'][mask].astype(int)-1)]
raters = [loadmat(source / f'ManualLandmarks/Femur/Rater{i}.mat', simplify_cells=True)['Results']
          for i in range(1, 6)]


def manual_center(landmarks):
    points = np.array([row[1] for row in landmarks[:6]], dtype=float)
    mean = points.mean(axis=0)
    p = points - mean
    solution, _, rank, _ = np.linalg.lstsq(np.c_[2*p, np.ones(len(p))], np.sum(p*p, axis=1), rcond=None)
    if rank != 4: raise ValueError('degenerate manual head annotations')
    return mean + solution[:3]


errors = []
for row in raters[0]:
    subject, side = row[-2:]
    bones = loadmat(source / f'Bones/{subject}.mat', simplify_cells=True)['B']
    bones = {bone['name']: bone['mesh']['vertices'] for bone in bones
             if isinstance(bone.get('mesh'), dict) and 'vertices' in bone['mesh']}
    estimate = fit['fit_femoral_head'](bones['Femur_'+side], bones['Hip_'+side], side, vertices, head)
    references = []
    for rater in raters:
        matches = [r for r in rater if r[-2] == subject and r[-1] == side]
        if len(matches) != 1: raise ValueError(f'missing manual annotations: {subject} {side}')
        references.extend(manual_center(measurement) for measurement in matches[0][:4])
    if len(references) != 20: raise ValueError('expected five raters with four repetitions each')
    error = float(np.linalg.norm(estimate['center'] - np.mean(references, axis=0)))
    errors.append(error)
    print(f'{subject} {side}: {error:.3f} mm from mean expert sphere center')
print(f'{len(errors)} femora: mean {np.mean(errors):.3f} mm, median {np.median(errors):.3f} mm, max {max(errors):.3f} mm')
