"""Export an OpenSkeleton source in its original registered pose.

The only spatial changes are a common axis/unit conversion and a single global
translation. No individual bone is posed, mirrored, shifted or scaled. The OBJ's
`o` names are the existing rig groups; its `g` names retain the 182 source bone
labels. The source triangulations are simplified independently to the existing
180,000-triangle budget. Requires numpy and fast-simplification.
"""
import json
from pathlib import Path

import numpy as np
import fast_simplification

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'data/skelly-out.npz'
OBJ = HERE.parent / 'skeleton-female.obj'
PARTS = HERE.parent / 'skeleton-female.parts.json'
TRIANGLES = 180000
MIN_PER_BONE = 44

# The source's names are retained verbatim, in addition to these rig groups.
NAME_MAP = {}
for side, suffix in (('L', 'l'), ('R', 'r')):
    for i in range(1, 13):
        NAME_MAP[f'Rib{i}_{side}'] = f'ribs_{suffix}'
    for source, rig in (('Scapula', 'scapula'), ('Clavicula', 'clavicle'),
                        ('Humerus', 'humerus'), ('Radius', 'forearm'), ('Ulna', 'forearm'),
                        ('Femur', 'femur'), ('Patella', 'patella'), ('Tibia', 'lower_leg'),
                        ('Fibula', 'lower_leg'), ('Pelvis', 'pelvis')):
        NAME_MAP[f'{source}_{side}'] = 'pelvis' if rig == 'pelvis' else f'{rig}_{suffix}'
    for bone in ('Cal', 'Tal', 'Nav', 'Cub', 'MedCun', 'InterCun', 'LatCun',
                 'FootSesaLat', 'FootSesaMed'):
        NAME_MAP[f'{bone}_{side}'] = f'foot_{suffix}'
    for i in range(1, 6):
        for segment in ('FootMeta', 'FootProxP', 'FootMidP', 'FootDistP'):
            NAME_MAP[f'{segment}{i}_{side}'] = f'foot_{suffix}'
        for segment in ('HandMeta', 'HandProxP', 'HandMidP', 'HandDistP'):
            NAME_MAP[f'{segment}{i}_{side}'] = f'hand_{suffix}'
    for bone in ('Scaphoid', 'Luna', 'Triquetrum', 'Pisiform', 'Trapezium',
                 'Trapezoideum', 'Capitatum', 'Hamata'):
        NAME_MAP[f'Hand{bone}_{side}'] = f'hand_{suffix}'
NAME_MAP.update({'Head': 'skull', 'Jaw': 'mandible', 'Sacrum': 'pelvis', 'Sternum': 'sternum'})
for prefix, count, part in (('C', 7, 'spine_cervical'), ('T', 12, 'spine_thoracic'),
                            ('L', 5, 'spine_lumbar')):
    for i in range(1, count + 1):
        NAME_MAP[f'{prefix}{i}'] = part

PART_ORDER = ['skull', 'mandible', 'spine_cervical', 'spine_thoracic', 'spine_lumbar',
              'ribs_l', 'ribs_r', 'sternum', 'scapula_l', 'scapula_r', 'clavicle_l',
              'clavicle_r', 'pelvis', 'humerus_l', 'humerus_r', 'forearm_l', 'forearm_r',
              'hand_l', 'hand_r', 'femur_l', 'femur_r', 'patella_l', 'patella_r',
              'lower_leg_l', 'lower_leg_r', 'foot_l', 'foot_r']


def simplify(points, faces, target):
    if len(faces) <= target:
        return points, faces
    points, faces = fast_simplification.simplify(
        points.astype(np.float32), faces.astype(np.int32),
        target_reduction=float(np.clip(1.0 - target / len(faces), 0.0, 0.98)), agg=7)
    return points.astype(np.float64), faces.astype(np.int64)


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--male', action='store_true', help='import N197925 instead of N186416')
    args = parser.parse_args()
    source = HERE / 'data/male-N197925.npz' if args.male else SOURCE
    obj = HERE.parent / 'skeleton-male.obj' if args.male else OBJ
    parts = HERE.parent / 'skeleton-male.parts.json' if args.male else PARTS
    subject = 'N197925' if args.male else 'N186416'
    with np.load(source) as store:
        names = sorted(k[2:] for k in store if k.startswith('v_'))
        if not names or set(names) - NAME_MAP.keys():
            raise ValueError(f'unmapped source bones: {sorted(set(names) - NAME_MAP.keys())}')
        if {k[2:] for k in store if k.startswith('f_')} != set(names):
            raise ValueError('source vertex and face labels do not match')
        vertices = {name: store[f'v_{name}'].astype(np.float64) for name in names}
        faces = {name: store[f'f_{name}'].astype(np.int64) - 1 for name in names}

    # Uniform transform for the entire skeleton: mm to metres, z-up to y-up.
    placed = {name: np.stack((p[:, 0], p[:, 2], -p[:, 1]), axis=1) / 1000.0
              for name, p in vertices.items()}
    for name in names:
        if faces[name].min() < 0 or faces[name].max() >= len(placed[name]):
            raise ValueError(f'{name}: invalid source face index')
    lowest = min(p[:, 1].min() for p in placed.values())
    pairs = [(name, name[:-2] + '_R') for name in names
             if name.endswith('_L') and name[:-2] + '_R' in placed]
    midline = np.mean([0.5 * (placed[a][:, 0].mean() + placed[b][:, 0].mean()) for a, b in pairs])
    pelvis = np.concatenate([placed[name] for name in names if NAME_MAP[name] == 'pelvis'])
    shift = np.array([-midline, -lowest, -pelvis[:, 2].mean()])
    for name in names:
        placed[name] += shift

    groups = {part: [name for name in names if NAME_MAP[name] == part] for part in PART_ORDER}
    if sum(map(len, groups.values())) != len(names):
        raise ValueError('a source bone is missing from the ordered rig groups')
    total_faces = sum(len(faces[name]) for name in names)
    lines = [f'# OpenSkeleton registered source pose, subject {subject}, CC BY 4.0.',
             '# One common axis/unit conversion and translation; no individual bone movement.',
             '# o = rig part, g = original source bone label.']
    report, bones = [], []
    running = 0
    for part, members in groups.items():
        if not members:
            continue
        lines.append(f'o {part}')
        part_points, part_faces = [], 0
        own = sum(len(faces[name]) for name in members)
        budget = max(MIN_PER_BONE * len(members), round(TRIANGLES * own / total_faces))
        for name in members:
            share = max(MIN_PER_BONE, round(budget * len(faces[name]) / own))
            points, triangles = simplify(placed[name], faces[name], share)
            if triangles.min() < 0 or triangles.max() >= len(points):
                raise ValueError(f'{name}: invalid face index after simplification')
            lines.append(f'g {name}')
            lines.extend(f'v {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}' for p in points)
            lines.extend(f'f {running + int(t[0]) + 1} {running + int(t[1]) + 1} '
                         f'{running + int(t[2]) + 1}' for t in triangles)
            bones.append({'name': name, 'part': part, 'first_vertex': running + 1,
                          'vertices': len(points), 'triangles': len(triangles)})
            running += len(points)
            part_points.append(points)
            part_faces += len(triangles)
        points = np.concatenate(part_points)
        report.append({'name': part, 'vertices': len(points), 'triangles': part_faces,
                       'bones': len(members), 'bbox': {'low': points.min(0).tolist(),
                                                       'high': points.max(0).tolist()}})
    obj.write_text('\n'.join(lines) + '\n')
    span = max(p[:, 1].max() for p in placed.values()) - min(p[:, 1].min() for p in placed.values())
    parts.write_text(json.dumps({'stature': float(span), 'parts': report, 'bones': len(names),
                                 'source_bones': bones, 'global_translation_m': shift.tolist(),
                                 'pose': 'source registered pose', 'subject': subject,
                                 'source': 'OpenSkeleton (NMDID) CC BY 4.0'}, indent=1) + '\n')
    print(f'{len(report)} rig parts, {len(names)} source bones, {running} vertices, '
          f'{sum(part["triangles"] for part in report)} triangles, {span:.5f} m')


if __name__ == '__main__':
    main()
