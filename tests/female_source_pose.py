"""Independently check the shipped female against the named source meshes.

This uses only the standard library. It checks every bone's name, rig grouping,
OBJ faces and simplified vertices against the same named source bone, after one
common coordinate conversion and translation. This is NOT an anatomical
assessment or a claim that the source registration is physically correct.
"""
import ast
import json
import math
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / 'tools/skeleton-source'


def load_points(archive, name):
    data = archive.read('v_' + name + '.npy')
    if data[:8] != b'\x93NUMPY\x01\x00':
        raise AssertionError(f'{name}: unsupported NPY header')
    length = struct.unpack_from('<H', data, 8)[0]
    header = ast.literal_eval(data[10:10 + length].decode('latin1').strip())
    assert header['descr'] == '<f4' and header['shape'][1] == 3
    payload = data[10 + length:]
    if header['fortran_order']:
        values = [value[0] for value in struct.iter_unpack('<f', payload)]
        count = header['shape'][0]
        points = list(zip(values[:count], values[count:2 * count], values[2 * count:]))
    else:
        points = list(struct.iter_unpack('<fff', payload))
    assert len(points) == header['shape'][0]
    return points


def expected_part(name):
    if name in ('Head', 'Jaw', 'Sternum', 'Sacrum', 'Pelvis_L', 'Pelvis_R'):
        return {'Head': 'skull', 'Jaw': 'mandible', 'Sternum': 'sternum',
                'Sacrum': 'pelvis', 'Pelvis_L': 'pelvis', 'Pelvis_R': 'pelvis'}[name]
    if name[0] in 'CTL' and name[1:].isdigit():
        return {'C': 'spine_cervical', 'T': 'spine_thoracic', 'L': 'spine_lumbar'}[name[0]]
    root, side = name.rsplit('_', 1)
    suffix = side.lower()
    if root.startswith('Rib'):
        return 'ribs_' + suffix
    if root.startswith('Hand'):
        return 'hand_' + suffix
    if root.startswith('Foot') or root in ('Cal', 'Tal', 'Nav', 'Cub', 'MedCun', 'InterCun', 'LatCun'):
        return 'foot_' + suffix
    base = {'Scapula': 'scapula', 'Clavicula': 'clavicle', 'Humerus': 'humerus',
            'Radius': 'forearm', 'Ulna': 'forearm', 'Femur': 'femur', 'Patella': 'patella',
            'Tibia': 'lower_leg', 'Fibula': 'lower_leg'}
    return base[root] + '_' + suffix


def main():
    meta = json.loads((BASE / 'skeleton-female.parts.json').read_text())
    manifest = json.loads((BASE / 'openskeleton/data/skelly-out.json').read_text())
    bone_info = {entry['name']: entry for entry in meta['source_bones']}
    assert len(bone_info) == 182 == meta['bones']
    assert set(bone_info) == set(manifest['structures']) and not manifest['skipped']
    for name, entry in bone_info.items():
        assert entry['part'] == expected_part(name), (name, entry['part'])

    # Parse exactly the groups and vertex/index ranges that the game receives.
    actual = {}
    part = name = None
    total_vertices = 0
    with (BASE / 'skeleton-female.obj').open() as obj:
        for line in obj:
            if line.startswith('o '):
                part = line[2:].strip()
            elif line.startswith('g '):
                name = line[2:].strip()
                assert name in bone_info and name not in actual and part == expected_part(name), name
                assert total_vertices + 1 == bone_info[name]['first_vertex'], name
                actual[name] = {'points': [], 'faces': 0, 'first': total_vertices + 1}
            elif line.startswith('v '):
                assert name is not None
                actual[name]['points'].append(tuple(map(float, line.split()[1:])))
                total_vertices += 1
            elif line.startswith('f '):
                assert name is not None
                indices = [int(value.split('/')[0]) for value in line.split()[1:]]
                assert len(indices) == 3 and all(actual[name]['first'] <= i <= total_vertices for i in indices), name
                actual[name]['faces'] += 1
    assert set(actual) == set(bone_info)
    for name, info in bone_info.items():
        assert len(actual[name]['points']) == info['vertices'] and actual[name]['faces'] == info['triangles'], name

    with zipfile.ZipFile(BASE / 'openskeleton/data/skelly-out.npz') as archive:
        source_names = {path[2:-4] for path in archive.namelist() if path.startswith('v_') and path.endswith('.npy')}
        assert source_names == set(bone_info)
        source = {name: load_points(archive, name) for name in source_names}
        # Recompute the common placement from the unmodified registered source.
        lowest = min(point[2] for points in source.values() for point in points) / 1000
        pairs = [(name, name[:-2] + '_R') for name in source_names
                 if name.endswith('_L') and name[:-2] + '_R' in source_names]
        centre = sum(0.5 * (sum(p[0] for p in source[a]) / len(source[a]) +
                            sum(p[0] for p in source[b]) / len(source[b])) for a, b in pairs) / len(pairs) / 1000
        pelvis = [p for name in ('Pelvis_L', 'Pelvis_R', 'Sacrum') for p in source[name]]
        forward = sum(-p[1] for p in pelvis) / len(pelvis) / 1000
        shift = (-centre, -lowest, -forward)
        assert max(abs(a - b) for a, b in zip(shift, meta['global_translation_m'])) < 1e-8

        # Simplification can move vertices slightly; compare every emitted point
        # against the original *same* bone in a 5 mm spatial neighbourhood.
        cell, limit2, worst = 0.005, 0.005 ** 2, 0.0
        for name in sorted(source_names):
            buckets = {}
            for x, y, z in source[name]:
                p = (x / 1000 + shift[0], z / 1000 + shift[1], -y / 1000 + shift[2])
                key = tuple(math.floor(v / cell) for v in p)
                buckets.setdefault(key, []).append(p)
            for point in actual[name]['points']:
                key = tuple(math.floor(v / cell) for v in point)
                best = float('inf')
                for x in range(key[0] - 1, key[0] + 2):
                    for y in range(key[1] - 1, key[1] + 2):
                        for z in range(key[2] - 1, key[2] + 2):
                            for p in buckets.get((x, y, z), ()):
                                d = sum((a - b) ** 2 for a, b in zip(point, p))
                                best = min(best, d)
                if best > limit2:
                    raise AssertionError(f'{name}: simplified vertex farther than 5 mm from source')
                worst = max(worst, best)
    print(f'female source pose: PASS (182 named bones; worst vertex/source distance {math.sqrt(worst)*1000:.3f} mm)')
    print('female anatomy: source registration is not independently certified')


if __name__ == '__main__':
    main()
