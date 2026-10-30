"""Estimate subject femoral epicondyles from the CT femoral atlas.

The atlas registration and head fit are shared with 06-fit-hips.py. The
published atlas labels medial and lateral epicondyles; this method applies a
simplified rigid similarity transfer followed by local medial/lateral extrema.
It is NOT the full A&A algorithm, nor a complete model of the knee. The
resulting anatomical epicondylar line approximates a flexion axis, but the real
knee has posture-dependent rotation and translation, and the individual ROM
cannot be determined from a single registered bone shape.
"""
from pathlib import Path
import json
import runpy
import numpy as np
from scipy.io import loadmat
from scipy.spatial import cKDTree

HERE = Path(__file__).resolve().parent
BASE = HERE.parent
REFERENCE = BASE / 'reference/femoral'
HIP = runpy.run_path(str(HERE / '06-fit-hips.py'))


def atlas():
    template = loadmat(REFERENCE / 'template.mat', simplify_cells=True)['template']
    areas = loadmat(REFERENCE / 'template_areas.mat', simplify_cells=True)['areas']
    landmarks = loadmat(REFERENCE / 'template_landmarks.mat', simplify_cells=True)['landmarks']
    mask = next(row[1] for row in areas if row[0] == 'Head').astype(bool)
    vertices = template['vertices'].astype(np.float64)
    head = vertices[np.unique(template['faces'][mask].astype(int)-1)]
    return vertices, head, landmarks


def fit_epicondyles(femur, reference, landmarks, registration):
    """Return source vertex locations and mapping diagnostics, in source mm."""
    transform = lambda p: registration['scale'] * (registration['rotation'] @ p) + registration['translation']
    names = ('MedialEpicondyle', 'LateralEpicondyle')
    rough = [transform(reference[landmarks[name]-1]) for name in names]
    medial_axis = HIP['unit'](rough[0]-rough[1])
    tree = cKDTree(femur)
    refined = []
    counts = []
    for i, point in enumerate(rough):
        selected = tree.query_ball_point(point, 12.)
        if len(selected) < 3: raise ValueError('insufficient source vertices near femoral epicondyle')
        local = femur[selected]
        outward = medial_axis * (1 if i == 0 else -1)
        refined.append(local[np.argmax((local-point) @ outward)])
        counts.append(len(selected))
    medial, lateral = refined
    width = np.linalg.norm(medial-lateral)
    if not (45 < width < 110): raise ValueError('implausible epicondylar width')
    return {'medial': medial, 'lateral': lateral, 'rough': rough, 'width': width, 'candidate_counts': counts}


def subject(sex, vertices, head, labels):
    source = 'male-N197925.npz' if sex == 'male' else 'skelly-out.npz'
    with np.load(HERE / 'data' / source) as store:
        shift = HIP['common_shift'](store)
        output = {}
        for side in ('L', 'R'):
            femur = store['v_Femur_'+side].astype(np.float64)
            pelvis = store['v_Pelvis_'+side].astype(np.float64)
            reg = HIP['fit_femoral_head'](femur, pelvis, side, vertices, head)
            fit = fit_epicondyles(femur, vertices, labels, reg)
            medial, lateral = fit['medial'], fit['lateral']
            center = (medial+lateral)/2
            axis = HIP['unit'](medial-lateral)
            to_game = lambda x: np.array([x[0], x[2], -x[1]])/1000 + shift
            output[side] = {'medial_epicondyle_mm': medial.tolist(), 'lateral_epicondyle_mm': lateral.tolist(),
                            'axis_origin_mm': center.tolist(), 'axis_medial_direction': axis.tolist(),
                            'axis_origin_m': to_game(center).tolist(),
                            'axis_medial_direction_game': [float(axis[0]), float(axis[2]), float(-axis[1])],
                            'width_mm': float(fit['width']), 'candidate_counts': fit['candidate_counts'],
                            'registration_rms_mm': float(reg['registration_rms'])}
    return output


def main():
    vertices, head, labels = atlas()
    for sex in ('male', 'female'):
        result = subject(sex, vertices, head, labels)
        doc = {'subject': 'N197925' if sex == 'male' else 'N186416',
               'method': 'CT femoral atlas epicondylar labels, rigid fit and local extrema',
               'clinical_validation': False,
               'limitations': 'Anatomical epicondylar line only; knee motion, neutral pose and limits not fitted.',
               'knees': result}
        (BASE / f'skeleton-{sex}-knee-fit.json').write_text(json.dumps(doc, indent=2)+'\n')
        rows = ['# Femoral epicondylar estimates in source-pose metres.',
                '# An anatomical axis estimate, not a fitted joint-motion constraint.']
        for side in ('L', 'R'):
            row = result[side]
            numbers = row['axis_origin_m'] + row['axis_medial_direction_game'] + [row['width_mm']/1000]
            rows.append(side + ' ' + ' '.join(f'{x:.9f}' for x in numbers))
        (BASE / f'skeleton-{sex}.knees').write_text('\n'.join(rows)+'\n')
        print(sex, ', '.join(f"{side}: {result[side]['width_mm']:.2f} mm" for side in ('L', 'R')))


if __name__ == '__main__':
    main()
