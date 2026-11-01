#!/usr/bin/env python3
"""Record game bind-pose pelvis span and fitted bilateral joint spacing.

Joint centre and pelvis geometry are not skin-surface anthropometry; see
SKELETON_GIRDLE_WIDTHS.md. Uses only the Python standard library.
"""
import csv
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'anthropometry'/'SKELETON_GIRDLE_WIDTHS.tsv'


def read_centres(path):
    centres = {}
    with path.open(encoding='utf-8') as f:
        for line in f:
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            side, x, y, z, radius = line.split()
            if side not in ('L', 'R') or side in centres:
                raise ValueError(f'Invalid or duplicate side in {path}')
            coords = tuple(float(v) for v in (x, y, z, radius))
            if not all(math.isfinite(v) for v in coords) or coords[3] <= 0:
                raise ValueError(f'Invalid centre in {path}')
            centres[side] = coords[:3]
    if set(centres) != {'L', 'R'}:
        raise ValueError(f'Missing centre in {path}')
    return centres


def pelvis_x(path):
    group = None
    seen = set()
    values = []
    with path.open(encoding='utf-8') as f:
        for line in f:
            if line.startswith('g '):
                group = line.split()[1]
                if group in ('Pelvis_L', 'Pelvis_R'):
                    if group in seen:
                        raise ValueError(f'Duplicate pelvis group in {path}')
                    seen.add(group)
            elif group in ('Pelvis_L', 'Pelvis_R') and line.startswith('v '):
                x, y, z = (float(v) for v in line.split()[1:4])
                if not all(math.isfinite(v) for v in (x, y, z)):
                    raise ValueError(f'Invalid pelvis vertex in {path}')
                values.append(x)
    if seen != {'Pelvis_L', 'Pelvis_R'} or not values:
        raise ValueError(f'Missing pelvis geometry in {path}')
    return (max(values)-min(values))*100


def main():
    records = []
    for sex, subject, length in [('male', 'N197925', 170.0), ('female', 'N186416', 170.2)]:
        for label, suffix in [('humeral_head_centres', 'shoulders'), ('femoral_head_centres', 'hips')]:
            centres = read_centres(ROOT/f'skeleton-{sex}.{suffix}')
            l, r = centres['L'], centres['R']
            records.append(dict(subject=subject, sex=sex, recorded_cadaver_length_cm=f'{length:.1f}',
                measure=label, transverse_cm=f'{abs(l[0]-r[0])*100:.4f}',
                point_to_point_cm=f'{math.dist(l, r)*100:.4f}'))
        records.append(dict(subject=subject, sex=sex, recorded_cadaver_length_cm=f'{length:.1f}',
            measure='pelvis_bone_mesh_transverse_extent',
            transverse_cm=f'{pelvis_x(ROOT/f"skeleton-{sex}.obj"):.4f}', point_to_point_cm='NA'))
    with OUT.open('w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=records[0].keys(), delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows(records)
    print(f'Wrote {len(records)} measurements to {OUT}')

if __name__ == '__main__':
    main()
