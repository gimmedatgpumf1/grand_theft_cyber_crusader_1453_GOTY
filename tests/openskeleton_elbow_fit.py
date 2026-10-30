"""Audit saved elbow estimates and source placement, not clinical accuracy."""
import json
import math
import zipfile
from pathlib import Path
from openskeleton_source_pose import load_points

BASE = Path(__file__).resolve().parent.parent / 'tools/skeleton-source'
for sex, archive_name in (('male','male-N197925'),('female','skelly-out')):
    doc=json.loads((BASE/f'skeleton-{sex}-elbow-fit.json').read_text())
    meta=json.loads((BASE/f'skeleton-{sex}.parts.json').read_text())
    assert doc['clinical_validation'] is False
    fits={}
    for line in (BASE/f'skeleton-{sex}.elbows').read_text().splitlines():
        if not line or line.startswith('#'): continue
        side,*fields=line.split()
        assert side in ('L','R') and side not in fits and len(fields)==7
        fits[side]=list(map(float,fields))
    assert set(fits)=={'L','R'}
    with zipfile.ZipFile(BASE/f'openskeleton/data/{archive_name}.npz') as archive:
        for side in ('L','R'):
            row=doc['elbows'][side]; p=row['source_pivot_mm']; axis=row['source_axis']
            shift=meta['global_translation_m']
            assert math.dist(fits[side][:3],[p[0]/1000+shift[0],p[2]/1000+shift[1],-p[1]/1000+shift[2]])<1e-8
            assert math.dist(fits[side][3:6],[axis[0],axis[2],-axis[1]])<1e-8
            assert abs(math.sqrt(sum(a*a for a in axis))-1)<1e-8
            assert abs(fits[side][6]-row['source_angle_deg'])<1e-8
            assert 0 < row['source_angle_deg'] < 150
            assert 5 < row['capitellum_radius_mm'] < 30 and 5 < row['trochlear_radius_mm'] < 25
            assert row['axis_to_capitellum_mm'] < 5
            for name in ('Humerus_','Ulna_'):
                assert min(math.dist(v,p) for v in load_points(archive,name+side)) < 40
    print(f'{sex} elbow fits: PASS (source geometry; not clinical validation)')
