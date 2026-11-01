"""Check femoral anatomical-axis estimates against each subject's source bones.

No assertion here establishes individual knee kinematics or ROM.
"""
import json
import math
from pathlib import Path
import zipfile
from openskeleton_source_pose import load_points

BASE = Path(__file__).resolve().parent.parent / 'tools/skeleton-source'

for sex, name in (('male', 'male-N197925'), ('female', 'skelly-out')):
    audit = json.loads((BASE / f'skeleton-{sex}-knee-fit.json').read_text())
    shift = json.loads((BASE / f'skeleton-{sex}.parts.json').read_text())['global_translation_m']
    assert audit['clinical_validation'] is False
    fitted = {}
    for line in (BASE / f'skeleton-{sex}.knees').read_text().splitlines():
        if not line or line.startswith('#'): continue
        side, *values = line.split()
        assert side in ('L', 'R') and side not in fitted and len(values) == 7
        fitted[side] = list(map(float, values))
    assert set(fitted) == {'L', 'R'}
    with zipfile.ZipFile(BASE / f'openskeleton/data/{name}.npz') as source:
        for side in ('L', 'R'):
            row = audit['knees'][side]
            femur = load_points(source, 'Femur_'+side)
            medial = row['medial_epicondyle_mm']
            lateral = row['lateral_epicondyle_mm']
            assert min(math.dist(medial, p) for p in femur) < 1e-8
            assert min(math.dist(lateral, p) for p in femur) < 1e-8
            width = math.dist(medial, lateral)
            assert 45 < width < 110 and abs(row['width_mm']-width) < 1e-8
            center = [(m+l)/2 for m,l in zip(medial,lateral)]
            axis = [(m-l)/width for m,l in zip(medial,lateral)]
            assert math.dist(center, row['axis_origin_mm']) < 1e-9
            assert math.dist(axis, row['axis_medial_direction']) < 1e-9
            expected = [center[0]/1000+shift[0], center[2]/1000+shift[1], -center[1]/1000+shift[2]]
            assert math.dist(expected, row['axis_origin_m']) < 1e-8
            assert math.dist([axis[0], axis[2], -axis[1]], row['axis_medial_direction_game']) < 1e-9
            assert math.dist(fitted[side][:3], row['axis_origin_m']) < 1e-8
            assert math.dist(fitted[side][3:6], row['axis_medial_direction_game']) < 1e-8
            assert abs(fitted[side][6] - width/1000) < 1e-8
            assert min(p[2] for p in femur) <= center[2] <= min(p[2] for p in femur)+60
            assert all(count >= 3 for count in row['candidate_counts'])
            assert row['registration_rms_mm'] < 8
    print(f'{sex} CT-derived epicondylar axes: PASS (source and frame; not a kinematic or ROM validation)')
