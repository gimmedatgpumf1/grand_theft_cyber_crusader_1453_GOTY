"""Fit subject-specific femoral head centres using a CT-derived femoral atlas.

The atlas supplies an anatomically labelled head surface. A trimmed similarity
registration transfers that region onto each original OpenSkeleton femur; a
robust sphere is fitted to those *subject* vertices. The atlas, not the subject,
is scaled/reflected for fitting. Results are geometric estimates: this is NOT
Fischer et al.'s full non-rigid A&A method or a clinical validation, and the
source model does not supply neutral joint frames or motion data.
"""
from pathlib import Path
import json
import numpy as np
from scipy.io import loadmat
from scipy.spatial import cKDTree

HERE = Path(__file__).resolve().parent
BASE = HERE.parent
REFERENCE = BASE / 'reference/femoral'


def unit(a):
    n = np.linalg.norm(a)
    if n < 1e-10: raise ValueError('degenerate anatomical direction')
    return a / n


def sphere(points):
    if len(points) < 20: raise ValueError('insufficient femoral head points')
    center = points.mean(axis=0)
    p = points-center
    solution, _, rank, _ = np.linalg.lstsq(np.c_[2*p, np.ones(len(p))], np.sum(p*p, axis=1), rcond=None)
    if rank != 4: raise ValueError('degenerate head sphere')
    return center + solution[:3], np.sqrt(solution[3] + solution[:3] @ solution[:3])


def fit_femoral_head(femur, pelvis, side, template, head, offset=0):
    """Points in original OpenSkeleton millimetres. Side is the source label."""
    if side not in ('L', 'R'): raise ValueError('unknown side')
    mean, reference_mean = femur.mean(axis=0), template.mean(axis=0)
    _, _, axes = np.linalg.svd(femur-mean, full_matrices=False)
    proximal = axes[0] * np.sign(axes[0, 2])  # source uses z-up
    toward_midline = np.array([-1., 0., 0.]) if side == 'L' else np.array([1., 0., 0.])
    medial = unit(toward_midline - proximal*(proximal @ toward_midline))
    target_frame = np.column_stack((proximal, medial, np.cross(proximal, medial)))
    atlas_long = np.array([1., 0., 0.])
    atlas_medial = np.array([0., -1., 0.])
    atlas_frame = np.column_stack((atlas_long, atlas_medial, np.cross(atlas_long, atlas_medial)))
    scale = (np.ptp(np.percentile((femur-mean) @ proximal, [1, 99])) /
             np.ptp(np.percentile((template-reference_mean) @ atlas_long, [1, 99])))
    target = femur[offset::3]
    target_tree = cKDTree(target)
    template_sample = template[offset::3]
    results = []
    # Fit both handedness hypotheses of the atlas. The original CT bones are
    # never transformed or mirrored; the atlas hypothesis is selected by error.
    for reflected in (1, -1):
        rotation = target_frame @ np.diag([1, 1, reflected]) @ atlas_frame.T
        translation = mean - scale * (rotation @ reference_mean)
        for _ in range(80):
            q = scale*(template_sample @ rotation.T) + translation
            distances, indices = target_tree.query(q)
            keep = distances < np.quantile(distances, .85)
            q, matched = q[keep], target[indices[keep]]
            cq, cm = q.mean(axis=0), matched.mean(axis=0)
            u, _, vh = np.linalg.svd((q-cq).T @ (matched-cm))
            correction = vh.T @ np.diag([1, 1, np.linalg.det(vh.T @ u.T)]) @ u.T
            rotation = correction @ rotation
            translation = correction @ (translation-cq) + cm
        transformed_head = scale*(head @ rotation.T) + translation
        distances, indices = cKDTree(femur).query(transformed_head)
        mapped = np.unique(indices[distances < 10])
        if len(mapped) < 50: continue
        points = femur[mapped]
        center, radius = sphere(points)
        inliers = points[np.abs(np.linalg.norm(points-center, axis=1)-radius) < 2.0]
        try:
            for _ in range(6):
                center, radius = sphere(inliers)
                inliers = points[np.abs(np.linalg.norm(points-center, axis=1)-radius) < 1.5]
        except ValueError:
            continue
        error = np.sqrt(np.mean((np.linalg.norm(inliers-center, axis=1)-radius)**2))
        surface_rms = np.sqrt(np.mean(target_tree.query(scale*(template_sample @ rotation.T)+translation)[0]**2))
        nearest_pelvis = float(cKDTree(pelvis).query(center)[0])
        results.append(dict(center=center, radius=radius, head_rms=error, registration_rms=surface_rms,
                            mapped=len(mapped), inliers=len(inliers), nearest_pelvis=nearest_pelvis,
                            reflected=reflected, rotation=rotation, translation=translation, scale=scale))
    if not results: raise ValueError('no successful femoral atlas fit')
    best = min(results, key=lambda r: r['registration_rms'])
    if not (15 < best['radius'] < 35 and best['inliers'] >= 100 and best['head_rms'] < 1.0 and
            best['registration_rms'] < 8 and best['nearest_pelvis'] < best['radius']+10):
        raise ValueError(f'femoral atlas fit failed quality checks: {best}')
    return best


def common_shift(store):
    """Same common rigid unit/axis conversion and translation as 03-import.py."""
    names = sorted(k[2:] for k in store if k.startswith('v_'))
    points = {name: np.stack((p[:, 0], p[:, 2], -p[:, 1]), axis=1) / 1000
              for name in names for p in [store['v_'+name].astype(np.float64)]}
    lowest = min(p[:, 1].min() for p in points.values())
    pairs = [(name, name[:-2]+'_R') for name in names if name.endswith('_L') and name[:-2]+'_R' in points]
    midline = np.mean([.5 * (points[a][:, 0].mean() + points[b][:, 0].mean()) for a, b in pairs])
    pelvis = np.concatenate([points[name] for name in ('Pelvis_L', 'Pelvis_R', 'Sacrum')])
    return np.array([-midline, -lowest, -pelvis[:, 2].mean()])


def fit_subject(sex, template, head):
    source = 'male-N197925.npz' if sex == 'male' else 'skelly-out.npz'
    with np.load(HERE / 'data' / source) as store:
        shift = common_shift(store)
        output = {}
        for side in ('L', 'R'):
            femur = store['v_Femur_'+side].astype(np.float64)
            pelvis = store['v_Pelvis_'+side].astype(np.float64)
            fit = fit_femoral_head(femur, pelvis, side, template, head)
            original = fit.pop('center')
            position = np.array([original[0], original[2], -original[1]])/1000 + shift
            output[side] = {'center': position.tolist(), 'source_center_mm': original.tolist(),
                            'radius_mm': fit['radius'], 'fit_rms_mm': fit['head_rms'],
                            'registration_rms_mm': fit['registration_rms'],
                            'pelvis_surface_distance_mm': fit['nearest_pelvis'],
                            'mapped_vertices': fit['mapped'], 'inlier_vertices': fit['inliers'],
                            'atlas_reflection': fit['reflected']}
    return output


def main():
    template = loadmat(REFERENCE / 'template.mat', simplify_cells=True)['template']
    areas = loadmat(REFERENCE / 'template_areas.mat', simplify_cells=True)['areas']
    head_mask = next(row[1] for row in areas if row[0] == 'Head').astype(bool)
    head_indices = np.unique(template['faces'][head_mask].astype(int)-1)
    vertices = template['vertices'].astype(np.float64)
    head = vertices[head_indices]
    for sex in ('male', 'female'):
        result = fit_subject(sex, vertices, head)
        audit = {'subject': 'N197925' if sex == 'male' else 'N186416',
                 'method': 'CT femoral atlas head labels, trimmed rigid similarity fit, robust subject sphere',
                 'clinical_validation': False, 'hips': result}
        (BASE / f'skeleton-{sex}-hip-fit.json').write_text(json.dumps(audit, indent=2)+'\n')
        rows = ['# Subject femoral head estimates, metres in the original registered source pose.',
                '# Geometric atlas fit, NOT a clinically validated landmark or motion constraint.']
        for side in ('L', 'R'):
            values = result[side]['center']
            rows.append(f"{side} " + ' '.join(f'{v:.9f}' for v in values) + f" {result[side]['radius_mm']/1000:.9f}")
        (BASE / f'skeleton-{sex}.hips').write_text('\n'.join(rows)+'\n')
        print(sex, ', '.join(f"{side}: radius {result[side]['radius_mm']:.2f} mm, residual {result[side]['fit_rms_mm']:.2f} mm" for side in ('L', 'R')))


if __name__ == '__main__':
    main()
