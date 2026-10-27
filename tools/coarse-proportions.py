#!/usr/bin/env python3
"""Estimate coarse 3D proportions before fine render fitting.

This is the cheap image-to-3D proportion stage: development-side MediaPipe X/Y/Z
landmarks are aligned to the reusable MakeHuman anatomical basis. Only bounded
slider values are exported. There is no person mesh, texture transfer, runtime
model, or runtime ML dependency.

Usage:
  PYTHONPATH=.cache/python python3 tools/coarse-proportions.py [preset ...]
  RENDER_FIT_CANDIDATES=build/coarse-candidates.json \
    PYTHONPATH=.cache/python python3 tools/render-fit.py [preset ...]
"""
import importlib.util
import json
import os
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / 'build'
BUILD.mkdir(exist_ok=True)
MODULE_PATH = REPO / 'tools' / 'render-fit.py'
SPEC = importlib.util.spec_from_file_location('render_fit', MODULE_PATH)
RF = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RF)

# Low-dimensional proportions first. Fine lids, lip corners, nostril angles and
# similar controls deliberately remain for the subsequent render-measured pass.
COARSE = [
    'headWidth', 'headHeight', 'headDepth', 'faceShape', 'faceFullness',
    'forehead', 'foreheadTemple', 'cheekBones', 'cheekFullness', 'cheekHeight',
    'jawWidth', 'chinHeight', 'chinProjection', 'chinBones', 'headOval',
    'headSquare', 'headTriangle', 'eyeSpacing', 'eyeVertical', 'eyeSize',
    'noseWidth', 'noseLength', 'noseProjection', 'noseVertical',
    'mouthWidth', 'mouthVertical',
]
FIXED = {'age', 'african', 'asian'}
DEFINITIONS = {spec['key']: spec for spec in RF.SPECS}


def align(source, target, weights):
    """Weighted rigid 3D alignment; scale stays fixed by interocular distance."""
    total = weights.sum()
    source_center = np.sum(source * weights[:, None], axis=0) / total
    target_center = np.sum(target * weights[:, None], axis=0) / total
    centered = source - source_center
    covariance = (centered * weights[:, None]).T @ (target - target_center)
    u, _, vt = np.linalg.svd(covariance)
    rotation = vt.T @ u.T
    if np.linalg.det(rotation) < 0:
        vt[-1] *= -1
        rotation = vt.T @ u.T
    return centered @ rotation.T + target_center


def fit_person(key, baseline):
    male = baseline['male']
    model = RF.mesh('male' if male else 'female')
    ids = np.array([index for index in RF.FIT if model.map_distance[index] < .34])
    faces, barycentric, _ = model.surface_map or model.build_surface_map()
    selected = faces[ids]
    unique, inverse = np.unique(selected.reshape(-1), return_inverse=True)
    morphs = {spec['key']: 0 for spec in RF.SPECS}
    for name in FIXED:
        morphs[name] = baseline['morphs'].get(name, 0)
    base = model.points(unique, morphs)
    names = [name for name in COARSE if name in DEFINITIONS]
    directions = []
    for name in names:
        spec = DEFINITIONS[name]
        positive = np.array([model.raw_morph(spec['positive']).get(index, np.zeros(3))
                             for index in unique]) if spec['positive'] else np.zeros((len(unique), 3))
        negative = np.array([model.raw_morph(spec['negative']).get(index, np.zeros(3))
                             for index in unique]) if spec['negative'] else np.zeros((len(unique), 3))
        directions.append((negative, positive))

    reference_raw = np.array(RF.REFS[key]['points'], dtype=float)[:, :3]
    eye_midpoint = (reference_raw[33] + reference_raw[263]) * .5
    eye_scale = np.linalg.norm((reference_raw[263] - reference_raw[33])[:2])
    reference = (reference_raw - eye_midpoint) / eye_scale
    base_weights = RF.landmark_weights(ids)
    eye_l = list(ids).index(33)
    eye_r = list(ids).index(263)
    # MediaPipe depth is useful for coarse nose/chin placement but less reliable
    # than image X/Y, especially around the silhouette.
    axis_weights = np.array([1.0, 1.0, .35])
    initial = np.array([baseline['morphs'].get(name, 0) / 100 for name in names])
    # This is intentionally a fresh coarse stage, not a continuation of the old
    # fine fit. Only age and broad heritage macro controls carry over.
    start = np.zeros_like(initial)
    lower = np.array([DEFINITIONS[name]['min'] / 100 for name in names])
    upper = np.array([DEFINITIONS[name]['max'] / 100 for name in names])

    def points(values):
        shaped = base.copy()
        for value, (negative, positive) in zip(values, directions):
            shaped += (positive if value >= 0 else negative) * abs(value)
        triangles = shaped[inverse.reshape(selected.shape)]
        surface = np.sum(triangles * barycentric[ids, :, None], axis=1)
        center = (surface[eye_l] + surface[eye_r]) * .5
        scale = np.linalg.norm((surface[eye_r] - surface[eye_l])[:2])
        normalized = (surface - center) / scale
        # MakeHuman Y points up and its front is +Z; MediaPipe image Y points
        # down and nearer facial features have negative Z.
        return normalized * np.array([1.0, -1.0, -1.0])

    def residual(values):
        shaped = points(values)
        aligned = align(shaped, reference[ids], base_weights)
        difference = (aligned - reference[ids]) * np.sqrt(base_weights[:, None]) * axis_weights
        prior = values * .055
        return np.r_[difference.ravel(), prior]

    initial_cost = float(np.sum(residual(start) ** 2))
    result = least_squares(residual, start, bounds=(lower, upper), loss='soft_l1',
                           f_scale=.035, max_nfev=180, x_scale='jac', ftol=1e-8,
                           xtol=1e-8, gtol=1e-8)
    final_cost = float(np.sum(residual(result.x) ** 2))
    for name, value in zip(names, result.x):
        morphs[name] = int(np.clip(np.round(value * 100), DEFINITIONS[name]['min'], DEFINITIONS[name]['max']))
    report = {'initialCost': initial_cost, 'finalCost': final_cost,
              'iterations': result.nfev, 'coarse': {name: morphs[name] for name in names}}
    print(key, '3D cost', round(initial_cost, 5), '->', round(final_cost, 5),
          report['coarse'], flush=True)
    return {'male': male, 'morphs': morphs}, report


def main():
    selected = [key for key in (sys.argv[1:] or list(RF.REFS)) if key in RF.REFS]
    candidate_file = os.environ.get('COARSE_BASELINE')
    baseline = json.loads((REPO / candidate_file).read_text()) if candidate_file else RF.export_initial()
    candidates, reports = {}, {}
    for key in selected:
        candidates[key], reports[key] = fit_person(key, baseline[key])
    (BUILD / 'coarse-candidates.json').write_text(json.dumps(candidates, indent=2))
    (BUILD / 'coarse-report.json').write_text(json.dumps(reports, indent=2))
    print('Wrote build/coarse-candidates.json; fine controls remain zero for render-fit.py.')


if __name__ == '__main__':
    main()
