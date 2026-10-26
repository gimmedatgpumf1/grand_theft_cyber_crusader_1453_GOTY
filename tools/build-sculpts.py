#!/usr/bin/env python3
"""Create smooth geometry-only sculpt targets from development render landmarks.

The generated .target files contain vertex deltas only. References and photographs
remain development inputs and are never copied into runtime facial materials.
Usage: PYTHONPATH=.cache/python python3 tools/build-sculpts.py obama
"""
import importlib.util, json, os, sys
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np
from scipy.spatial import cKDTree

REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / 'build'
TARGETS = REPO / 'tools/human-source/targets/custom'
spec = importlib.util.spec_from_file_location('render_fit', REPO / 'tools/render-fit.py')
fit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fit)

CORE = {
    1, 4, 13, 14, 33, 133, 159, 145, 362, 263, 386, 374, 61, 291, 17, 0,
    70, 300, 63, 293, 105, 334, 66, 296, 107, 336, 55, 285, 65, 295,
    52, 282, 53, 283, 46, 276, 37, 39, 40, 267, 269, 270, 321, 405,
    314, 84, 181, 91, 146,
}
ANCHORS = sorted(set(range(0, 468, 3)) | CORE | fit.KEY)


def detect(face_mesh, key):
    image = cv2.imread(str(BUILD / f'render-fit-{key}.png'))
    result = face_mesh.process(cv2.cvtColor(image, cv2.COLOR_BGR2RGB))
    if not result.multi_face_landmarks:
        raise RuntimeError(f'No generated face detected for {key}')
    height, width = image.shape[:2]
    return np.array([[point.x * width, point.y * height]
                     for point in result.multi_face_landmarks[0].landmark[:468]])


def sculpt(key, candidate, points, accumulate=False):
    model = fit.mesh('male' if candidate['male'] else 'female')
    if model.surface_map is None:
        model.build_surface_map()
    surface_distance = model.surface_map[2]
    ids = np.array([index for index in ANCHORS if surface_distance[index] < .25])
    morphs = candidate['morphs']
    model_points = model.surface_points(ids, morphs)
    design = np.column_stack((model_points, np.ones(len(ids))))
    weights = fit.landmark_weights(ids)
    coefficients = np.linalg.lstsq(design * np.sqrt(weights)[:, None],
                                   points[ids] * np.sqrt(weights)[:, None], rcond=None)[0]
    _, rotation, scale = fit.normalize(points)
    reference = fit.normalize(np.array(fit.REFS[key]['points'])[:, :2])[0]
    midpoint = (points[33] + points[263]) / 2
    desired = (reference[ids] * scale) @ rotation + midpoint
    residual = desired - points[ids]
    # Minimum-norm raw-space displacement in the fitted camera plane.
    deltas = residual @ np.linalg.pinv(coefficients[:3])
    lengths = np.linalg.norm(deltas, axis=1)
    deltas *= np.minimum(1, .45 / np.maximum(lengths, 1e-9))[:, None]

    tree = cKDTree(model_points)
    distances, nearest = tree.query(model.vertices[model.head_ids], k=12)
    weights = np.exp(-((distances / .13) ** 2))
    weights[distances > .42] = 0
    denominator = weights.sum(axis=1)
    smoothed = np.einsum('ij,ijk->ik', weights, deltas[nearest]) / np.maximum(denominator[:, None], 1e-12)
    lengths = np.linalg.norm(smoothed, axis=1)
    smoothed *= np.minimum(1, .32 / np.maximum(lengths, 1e-9))[:, None]
    TARGETS.mkdir(parents=True, exist_ok=True)
    sex = 'male' if candidate['male'] else 'female'
    output = TARGETS / f'{key}-{sex}.target'
    if accumulate and output.exists():
        existing = fit.read_target(output)
        for position, index in enumerate(model.head_ids):
            smoothed[position] += existing.get(index, (0, 0, 0))
        lengths = np.linalg.norm(smoothed, axis=1)
        smoothed *= np.minimum(1, .32 / np.maximum(lengths, 1e-9))[:, None]
        lengths = np.linalg.norm(smoothed, axis=1)
    with output.open('w') as target:
        target.write('# Geometry-only development-fitted sculpt; no image or UV data.\n')
        for index, delta, weight in zip(model.head_ids, smoothed, denominator):
            if weight > 1e-5 and np.linalg.norm(delta) > 1e-6:
                target.write(f'{index} {delta[0]:.8f} {delta[1]:.8f} {delta[2]:.8f}\n')
    return {'anchors': len(ids), 'vertices': int(np.count_nonzero(denominator > 1e-5)),
            'meanDelta': float(np.mean(lengths[denominator > 1e-5])),
            'maxDelta': float(np.max(lengths)), 'file': str(output.relative_to(REPO))}


def main():
    accumulate = '--accumulate' in sys.argv
    keys = [key for key in (sys.argv[1:] or ['obama']) if key in fit.REFS]
    candidate_file = os.environ.get('SCULPT_CANDIDATES', 'build/render-fit-initial.json')
    candidates = json.loads((REPO / candidate_file).read_text())
    report = {}
    with mp.solutions.face_mesh.FaceMesh(static_image_mode=True, max_num_faces=1,
                                        refine_landmarks=True, min_detection_confidence=.3) as face_mesh:
        for key in keys:
            report[key] = sculpt(key, candidates[key], detect(face_mesh, key), accumulate)
            print(key, report[key], flush=True)
    (BUILD / 'sculpt-report.json').write_text(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
