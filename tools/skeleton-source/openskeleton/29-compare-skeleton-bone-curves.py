#!/usr/bin/env python3
"""Remeasure the shipped OBJs and compare bone spans with SVAD bone curves.

This is a geometry/provenance audit, not a clinical measurement or a
comparison with the NHANES skin-landmark curves. Requires numpy.
"""
import csv
import hashlib
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'anthropometry'
BONES = ('Femur', 'Tibia', 'Fibula', 'Humerus', 'Radius', 'Ulna')
SUBJECTS = (('male', 'N197925', 170.0), ('female', 'N186416', 170.2))


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_bones(path):
    wanted = {f'{bone}_{side}' for bone in BONES for side in ('L', 'R')}
    groups = {k: [] for k in wanted}
    current = None
    seen = set()
    with path.open(encoding='utf-8') as f:
        for line in f:
            if line.startswith('g '):
                name = line.split()[1]
                if name in wanted:
                    if name in seen:
                        raise ValueError(f'Duplicate group {name} in {path}')
                    seen.add(name)
                current = name if name in wanted else None
            elif current and line.startswith('v '):
                groups[current].append([float(v) * 100 for v in line.split()[1:4]])
    spans = {}
    for name, coordinates in groups.items():
        p = np.asarray(coordinates, dtype=float)
        if len(p) < 4 or not np.isfinite(p).all():
            raise ValueError(f'Invalid or missing group {name} in {path}')
        axis = np.linalg.svd(p-p.mean(axis=0), full_matrices=False)[2][0]
        spans[name] = float(np.ptp(p @ axis))
    return spans


def main():
    fits_path = OUT / 'SVAD_BONE_ON_STATURE_FITS.tsv'
    with fits_path.open(newline='', encoding='utf-8') as f:
        fits = {(r['sex'], r['dimension']): r for r in csv.DictReader(f, delimiter='\t')
                if r['age_group'] == '18_to_under_21'}
    output = []
    hashes = {}
    for sex, subject, stature in SUBJECTS:
        obj = ROOT / f'skeleton-{sex}.obj'
        spans = read_bones(obj)
        hashes[sex] = file_hash(obj)
        for bone in BONES:
            f = fits[(sex, bone.lower()+'_length')]
            low, high = float(f['stature_min_cm']), float(f['stature_max_cm'])
            if not low <= stature <= high:
                raise ValueError(f'{subject} outside the {bone} observed stature range')
            predicted = float(f['intercept_cm'])+float(f['slope_cm_per_cm'])*stature
            if predicted <= 0:
                raise ValueError(f'Invalid fitted {bone} length')
            for side in ('L', 'R'):
                measured = spans[f'{bone}_{side}']
                output.append(dict(subject=subject, sex=sex, side=side, bone=bone,
                    recorded_cadaver_length_cm=f'{stature:.1f}',
                    game_mesh_pca_span_cm=f'{measured:.4f}',
                    svad_fitted_bone_length_cm=f'{predicted:.4f}',
                    difference_cm=f'{measured-predicted:.4f}',
                    difference_percent=f'{100*(measured/predicted-1):.3f}',
                    fit_n=f['n'], fit_residual_sd_cm=f['residual_sd_cm'],
                    fit_stature_min_cm=f['stature_min_cm'], fit_stature_max_cm=f['stature_max_cm']))
    out = OUT / 'SKELETON_BONE_CURVE_COMPARISON.tsv'
    with out.open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=output[0].keys(), delimiter='\t', lineterminator='\n')
        writer.writeheader(); writer.writerows(output)
    print(f'{len(output)} comparisons written to {out}')
    print('SHA256 male OBJ', hashes['male'])
    print('SHA256 female OBJ', hashes['female'])
    print('SHA256 SVAD fits', file_hash(fits_path))

if __name__ == '__main__':
    main()
