"""Audit saved geometric humeral head fits, not clinical landmarks."""
import json
import math
import zipfile
from pathlib import Path
from openskeleton_source_pose import load_points

BASE = Path(__file__).resolve().parent.parent / 'tools/skeleton-source'
for sex, archive_name in (('male', 'male-N197925'), ('female', 'skelly-out')):
    audit = json.loads((BASE / f'skeleton-{sex}-shoulder-fit.json').read_text())
    meta = json.loads((BASE / f'skeleton-{sex}.parts.json').read_text())
    assert audit['clinical_validation'] is False
    centers = {}
    for line in (BASE / f'skeleton-{sex}.shoulders').read_text().splitlines():
        if not line or line.startswith('#'): continue
        side, *values = line.split()
        assert side in ('L', 'R') and side not in centers and len(values) == 4
        centers[side] = list(map(float, values))
    assert set(centers) == {'L', 'R'}
    with zipfile.ZipFile(BASE / f'openskeleton/data/{archive_name}.npz') as archive:
        for side in ('L', 'R'):
            row = audit['shoulders'][side]
            center, radius = row['source_center_mm'], row['radius_mm']
            assert 15 < radius < 35 and row['surface_vertices'] >= 200
            assert row['rms_mm'] < 1.5 and row['cutoff_15mm_center_difference_mm'] < 2
            shift = meta['global_translation_m']
            expected = [center[0]/1000+shift[0], center[2]/1000+shift[1], -center[1]/1000+shift[2]]
            assert math.dist(expected, centers[side][:3]) < 1e-8
            assert abs(centers[side][3] - radius/1000) < 1e-9
            vertices = load_points(archive, 'Humerus_'+side)
            assert sum(abs(math.dist(p, center)-radius) < 1.5 for p in vertices) >= 200
    print(f'{sex} humeral head fit: PASS (geometry only, not clinical validation)')
