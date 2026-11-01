"""Check CT-derived hip-fit artifacts against the original named bone vertices.

These checks verify the data, source scale and fit stability, not clinical
accuracy, motion limits, or completeness of the joint rig.
"""
import json
import math
from pathlib import Path
import zipfile
from openskeleton_source_pose import load_points

BASE = Path(__file__).resolve().parent.parent / 'tools/skeleton-source'
for sex, archive_name in (('male', 'male-N197925'), ('female', 'skelly-out')):
    audit = json.loads((BASE / f'skeleton-{sex}-hip-fit.json').read_text())
    meta = json.loads((BASE / f'skeleton-{sex}.parts.json').read_text())
    assert audit['clinical_validation'] is False
    hips = {}
    for line in (BASE / f'skeleton-{sex}.hips').read_text().splitlines():
        if not line or line.startswith('#'): continue
        side, *values = line.split()
        assert side in ('L', 'R') and side not in hips and len(values) == 4
        hips[side] = list(map(float, values))
    assert set(hips) == {'L', 'R'}
    balls = {}
    for line in (BASE / f'skeleton-{sex}.lines').read_text().splitlines():
        fields = line.split()
        if len(fields) == 6 and fields[:2] in (['S', 'Hip_L'], ['S', 'Hip_R']):
            balls[fields[1][-1]] = list(map(float, fields[2:5]))
    assert set(balls) == {'L', 'R'}
    with zipfile.ZipFile(BASE / f'openskeleton/data/{archive_name}.npz') as archive:
        for side in ('L', 'R'):
            row = audit['hips'][side]
            center, radius = row['source_center_mm'], row['radius_mm']
            assert 15 < radius < 35 and row['inlier_vertices'] >= 200
            assert row['fit_rms_mm'] < 1 and row['registration_rms_mm'] < 8
            shift = meta['global_translation_m']
            expected = [center[0]/1000+shift[0], center[2]/1000+shift[1], -center[1]/1000+shift[2]]
            assert math.dist(expected, hips[side][:3]) < 1e-8
            assert math.dist(balls[side], hips[side][:3]) < 1e-6
            assert abs(hips[side][3] - radius/1000) < 1e-9
            vertices = load_points(archive, 'Femur_'+side)
            proximal = max(p[2] for p in vertices)
            assert proximal-75 < center[2] < proximal
            spherical = [p for p in vertices if abs(math.dist(p, center)-radius) < 1.5 and abs(p[2]-center[2]) < radius+2]
            assert len(spherical) >= 200
    print(f'{sex} CT-derived hip fits: PASS (source geometry, placement, V balls; not clinical validation)')
