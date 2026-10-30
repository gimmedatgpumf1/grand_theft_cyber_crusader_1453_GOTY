"""Reproducible *unvalidated* landmark-guided measurements, not diagnoses.

Requires numpy, scipy, matplotlib. Run from any directory; outputs CSV and
endpoint audit PNG next to this script's parent. Source is in mm. Game vertices
are inverse-transformed into the source frame before matching. See report for
full definitions, limitations and sensitivity of the window heuristic.
"""
import csv
import json
import runpy
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from scipy.spatial import cKDTree

HERE = Path(__file__).resolve().parent
BASE = HERE.parent
read_obj = runpy.run_path(str(HERE / '15-measure-bone-lengths.py'))['read_obj']
SUBJECTS = [('male', 'N197925', 'male-N197925.npz'),
            ('female', 'N186416', 'skelly-out.npz')]


def frame(vertices, medial, lateral):
    # The shaft's first principal axis, plus the fitted knee's medial direction.
    z = np.linalg.svd(vertices - vertices.mean(axis=0), full_matrices=False)[2][0]
    z *= np.sign(z[2])
    x = medial - lateral
    x -= z * (x @ z)
    x /= np.linalg.norm(x)
    return x, z


def leg_points(femur, tibia, hip, knee, window):
    head = np.array(hip['source_center_mm'])
    radius = hip['radius_mm']
    medial = np.array(knee['medial_epicondyle_mm'])
    lateral = np.array(knee['lateral_epicondyle_mm'])
    x, z = frame(femur, medial, lateral)
    fx, fz = femur @ x, femur @ z
    target = .6 * (medial @ x) + .4 * (lateral @ x)
    candidates = femur[(abs(fx-target) < window) & (fz < medial @ z + 20)]
    cap = femur[(np.linalg.norm(femur-head, axis=1) < radius+1) & (fz > head @ z)]
    if not len(candidates) or not len(cap):
        raise ValueError('empty femoral landmark search')
    fhead = cap[np.argmax(cap @ z)]
    condyle = candidates[np.argmin(candidates @ z)]
    femoral = (fhead, condyle, x, z)

    x, z = frame(tibia, medial, lateral)
    tx, tz = tibia @ x, tibia @ z
    upper = tibia[tz > tz.max()-20]
    lower = tibia[tz < tz.min()+50]
    ux, lx = upper @ x, lower @ x
    centre = (ux.min()+ux.max())/2
    plateau_x = (centre + ux.max())/2  # x increases medially
    plateau = upper[abs(ux-plateau_x) < window]
    plafond_x = (lx.min()+lx.max())/2
    plafond = lower[abs(lx-plafond_x) < window]
    if not len(plateau) or not len(plafond):
        raise ValueError('empty tibial landmark search')
    tibial = (plateau[np.argmax(plateau @ z)],
              plafond[np.argmin(plafond @ z)], x, z)
    return femoral, tibial


def projected(points):
    a, b, x, z = points
    return float(np.hypot((a-b) @ x, (a-b) @ z))


def game_points(points, game):
    # Nearest simplified mesh surface vertices, in source coordinates.
    tree = cKDTree(game)
    a, b, x, z = points
    distances, indices = tree.query(np.array([a, b]))
    return (game[indices[0]], game[indices[1]], x, z), distances


def source_frame(game_vertices, shift):
    g = np.asarray(game_vertices) - shift * 1000
    return np.column_stack((g[:, 0], -g[:, 2], g[:, 1]))


def toe_measure(vertices):
    axis = np.linalg.svd(vertices-vertices.mean(axis=0), full_matrices=False)[2][0]
    p = vertices @ axis
    a, b = vertices[np.argmin(p)], vertices[np.argmax(p)]
    return float(np.ptp(p)), a, b, axis


def main():
    rows, toes = [], []
    fig, axs = plt.subplots(4, 4, figsize=(13, 15), constrained_layout=True)
    for sex_index, (sex, subject, filename) in enumerate(SUBJECTS):
        hips = json.loads((BASE / f'skeleton-{sex}-hip-fit.json').read_text())['hips']
        knees = json.loads((BASE / f'skeleton-{sex}-knee-fit.json').read_text())['knees']
        shift = np.array(json.loads((BASE / f'skeleton-{sex}.parts.json').read_text())['global_translation_m'])
        game = read_obj(BASE / f'skeleton-{sex}.obj')
        with np.load(HERE / 'data' / filename) as store:
            for side_index, side in enumerate(('L', 'R')):
                r = sex_index * 2 + side_index
                f, t = (store[f'v_{name}_{side}'].astype(float) for name in ('Femur', 'Tibia'))
                pairs = leg_points(f, t, hips[side], knees[side], 5)
                alternatives = [leg_points(f, t, hips[side], knees[side], width) for width in (3, 8)]
                for j, (bone, vertices, points) in enumerate(zip(('Femur', 'Tibia'), (f, t), pairs)):
                    g = source_frame(game[f'{bone}_{side}'], shift)
                    gp, errors = game_points(points, g)
                    vals = [projected(alt[j]) for alt in alternatives]
                    a, b, x, z = points
                    rows.append([subject, sex, side, bone, f'{projected(points):.4f}',
                                 f'{projected(gp):.4f}', f'{vals[0]:.4f}', f'{vals[1]:.4f}',
                                 *(f'{v:.4f}' for v in np.r_[a, b]),
                                 *(f'{v:.4f}' for v in errors)])
                    ax = axs[r, j]
                    ax.scatter(vertices[::3] @ x, vertices[::3] @ z, s=.12, c='#aebbc8', rasterized=True)
                    ax.scatter([a@x, b@x], [a@z, b@z], s=22, c=['#d73027', '#225ea8'], zorder=4)
                    ax.plot([a@x, b@x], [a@z, b@z], color='#383838', lw=.7)
                    ax.set_aspect('equal'); ax.set_title(f'{sex} {side} {bone} {projected(points):.1f} mm')
                    ax.tick_params(labelsize=6); ax.set_xlabel('medial axis mm', fontsize=7)
                for j, bone in enumerate(('FootMeta3', 'FootProxP3')):
                    v = store[f'v_{bone}_{side}'].astype(float)
                    length, a, b, axis = toe_measure(v)
                    g = source_frame(game[f'{bone}_{side}'], shift)
                    game_span = float(np.ptp(g@axis))
                    toes.append([subject, sex, side, bone, f'{length:.4f}', f'{game_span:.4f}',
                                 *(f'{v:.4f}' for v in np.r_[a, b])])
                    # second direction chosen by PCA for a readable side view
                    second = np.linalg.svd(v-v.mean(axis=0), full_matrices=False)[2][1]
                    ax = axs[r, j+2]
                    ax.scatter(v @ axis, v @ second, s=.15, c='#9aabba', rasterized=True)
                    ax.scatter([a@axis, b@axis], [a@second, b@second], s=22, c=['#d73027', '#225ea8'])
                    ax.plot([a@axis, b@axis], [a@second, b@second], color='#383838', lw=.7)
                    ax.set_aspect('equal'); ax.set_title(f'{sex} {side} {bone} {length:.1f} mm')
                    ax.tick_params(labelsize=6); ax.set_xlabel('PCA long axis mm', fontsize=7)
    fig.suptitle('Source mesh projections and automatic endpoints — NOT expert validated', fontsize=13)
    fig.savefig(BASE / 'LANDMARK_AUDIT.png', dpi=160)
    plt.close(fig)
    endpoint_cols = ['p1_x_mm', 'p1_y_mm', 'p1_z_mm', 'p2_x_mm', 'p2_y_mm', 'p2_z_mm']
    for filename, header, data in [
        ('LEG_LANDMARK_ESTIMATES.csv', ['subject','sex','side','bone','source_projected_mm',
         'game_projected_mm','source_window3_mm','source_window8_mm', *endpoint_cols,
         'game_p1_nearest_mm','game_p2_nearest_mm'], rows),
        ('TOE_AXIS_ESTIMATES.csv', ['subject','sex','side','bone','source_axis_span_mm',
         'game_axis_span_mm', *endpoint_cols], toes)]:
        with (BASE / filename).open('w', newline='', encoding='utf-8') as output:
            writer = csv.writer(output, lineterminator='\n')
            writer.writerow(header); writer.writerows(data)
    print('Wrote LEG_LANDMARK_ESTIMATES.csv, TOE_AXIS_ESTIMATES.csv and LANDMARK_AUDIT.png')


if __name__ == '__main__':
    main()
