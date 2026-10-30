"""Compare atlas femoral estimates with 5 raters' repeated clinical landmarks.

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
knee = runpy.run_path(str(HERE.parent / 'openskeleton/08-fit-knees.py'))
labels = loadmat(HERE / 'femoral/template_landmarks.mat', simplify_cells=True)['landmarks']
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


errors, axis_errors, midpoint_errors = [], [], []
for row in raters[0]:
    subject, side = row[-2:]
    bones = loadmat(source / f'Bones/{subject}.mat', simplify_cells=True)['B']
    bones = {bone['name']: bone['mesh']['vertices'] for bone in bones
             if isinstance(bone.get('mesh'), dict) and 'vertices' in bone['mesh']}
    estimate = fit['fit_femoral_head'](bones['Femur_'+side], bones['Hip_'+side], side, vertices, head)
    references = []
    epicondyle_references = [[], []]
    for rater in raters:
        matches = [r for r in rater if r[-2] == subject and r[-1] == side]
        if len(matches) != 1: raise ValueError(f'missing manual annotations: {subject} {side}')
        for measurement in matches[0][:4]:
            references.append(manual_center(measurement))
            for i, landmark in enumerate(('Medial Epicondyle (MEC)', 'Lateral Epicondyle (LEC)')):
                epicondyle_references[i].append(next(r[1] for r in measurement if r[0] == landmark))
    if len(references) != 20: raise ValueError('expected five raters with four repetitions each')
    error = float(np.linalg.norm(estimate['center'] - np.mean(references, axis=0)))
    errors.append(error)
    knee_fit = knee['fit_epicondyles'](bones['Femur_'+side], vertices, labels, estimate)
    observed = [np.mean(rows, axis=0) for rows in epicondyle_references]
    predicted = [knee_fit['medial'], knee_fit['lateral']]
    true_axis = fit['unit'](observed[0] - observed[1])
    pred_axis = fit['unit'](predicted[0] - predicted[1])
    axis_error = float(np.degrees(np.arccos(np.clip(true_axis @ pred_axis, -1, 1))))
    midpoint_error = float(np.linalg.norm((predicted[0]+predicted[1]-observed[0]-observed[1])/2))
    axis_errors.append(axis_error)
    midpoint_errors.append(midpoint_error)
    print(f'{subject} {side}: head {error:.3f} mm; epicondyle midpoint {midpoint_error:.3f} mm; axis {axis_error:.3f} degrees')
for title, values, units in (('head center', errors, 'mm'), ('epicondyle midpoint', midpoint_errors, 'mm'),
                             ('epicondylar axis direction', axis_errors, 'degrees')):
    print(f'{len(values)} femora {title}: mean {np.mean(values):.3f} {units}, median {np.median(values):.3f} {units}, max {max(values):.3f} {units}')
