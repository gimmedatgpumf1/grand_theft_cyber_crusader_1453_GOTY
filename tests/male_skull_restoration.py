"""Verify source-derived landmarks on faces of the shipped male skull.

Landmarks are vertices of Z-Anatomy-Sample SkeletalSystem100.fbx, git blob
7c62e45211bf3992bb7239170343e06e2b073865, transformed by the importer's
centimetre-to-metre conversion and one common floor translation. The right
maxilla/nasal meshes mirror the source left side, as the rest of the rig does.
These points were selected where the previous skull lacked nearby vertices;
this is an asset regression test, not a certification of anatomical health.
"""
import json
import math
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent / 'tools/skeleton-source'
LANDMARKS = {
    'Maxilla.l': [
        (0.044601, 1.544428, 0.059935), (0.043072, 1.560574, 0.058324),
        (0.042989, 1.546711, 0.055953), (0.041055, 1.563156, 0.055591),
        (0.042022, 1.556060, 0.054991), (0.038753, 1.548322, 0.065417),
        (0.037508, 1.544779, 0.062405), (0.039120, 1.547682, 0.056256),
    ],
    'Nasal bone.l': [
        (0.008269, 1.565630, 0.094013), (0.006055, 1.568533, 0.097990),
        (0.005115, 1.573698, 0.097881), (0.007070, 1.574018, 0.091433),
        (0.004443, 1.580784, 0.089789), (0.005088, 1.567892, 0.090466),
        (0.006188, 1.577241, 0.088854), (0.004121, 1.585949, 0.082827),
    ],
    'Vomer': [
        (0.004753, 1.566224, 0.030298), (0.001143, 1.554933, 0.080704),
        (0.001280, 1.551565, 0.074578), (0.000591, 1.551390, 0.068453),
        (0.000862, 1.559024, 0.069419), (0.001134, 1.551710, 0.063939),
        (0.000411, 1.560129, 0.065551), (0.001430, 1.561059, 0.060715),
    ],
}


def main():
    metadata = json.loads((BASE / 'skeleton-male.parts.json').read_text())
    names = {bone['name']: bone for bone in metadata['bones']}
    expected = {'Maxilla.l', 'Maxilla.r', 'Nasal bone.l', 'Nasal bone.r', 'Vomer'}
    assert expected <= names.keys(), f'missing source bones: {expected - names.keys()}'
    assert all(names[name]['part'] == 'skull' for name in expected)
    assert metadata['sourceObjects'] == len(names) == 266
    # These are cartilage meshes, not the nasal bones or hyoid bone.
    for name in ('Nasal septal cartilage', 'Lateral process of nasal septal cartilage.l',
                 'Lateral process of nasal septal cartilage.r', 'Major alar cartilage.l',
                 'Major alar cartilage.r', 'Thyroid cartilage', 'Cricoid cartilage',
                 'Arytenoid cartilage.l', 'Arytenoid cartilage.r',
                 'Corniculate cartilage.l', 'Corniculate cartilage.r'):
        assert name not in names, name
    assert names['Hyoid bone']['part'] == 'spine_cervical'

    vertices, referenced, cervical, part = [], set(), set(), None
    with (BASE / 'skeleton-male.obj').open() as obj:
        for line in obj:
            if line.startswith('o '):
                part = line[2:].strip()
            elif line.startswith('v '):
                vertices.append(tuple(map(float, line.split()[1:4])))
            elif part in ('skull', 'spine_cervical') and line.startswith('f '):
                indices = [int(token.split('/')[0]) - 1 for token in line.split()[1:]]
                assert len(indices) == 3 and all(0 <= index < len(vertices) for index in indices)
                (referenced if part == 'skull' else cervical).update(indices)

    # The source nasal-cartilage tip was at z=0.11577. Check actual referenced
    # skull vertices as well as the manifest so it cannot silently return.
    assert max(vertices[index][2] for index in referenced) < 0.105
    # At thyroid height, the old laryngeal cartilage extended to z=0.03195.
    assert max(vertices[index][2] for index in cervical
               if 1.46 < vertices[index][1] < 1.48) < 0.01

    # Only vertices actually used by skull triangles count. Compare using a
    # spatial grid so neither NumPy nor a copy of the 40 MB FBX is needed.
    cell = 0.001
    grid = {}
    for index in referenced:
        point = vertices[index]
        key = tuple(math.floor(value / cell) for value in point)
        grid.setdefault(key, []).append(point)
    worst = 0.0
    for name, points in LANDMARKS.items():
        for mirror in ((False, True) if name != 'Vomer' else (False,)):
            for x, y, z in points:
                point = (-x if mirror else x, y, z)
                key = tuple(math.floor(value / cell) for value in point)
                distance = min((sum((a - b) ** 2 for a, b in zip(point, vertex))
                                for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1)
                                for vertex in grid.get((key[0] + dx, key[1] + dy, key[2] + dz), [])),
                               default=float('inf'))
                assert distance < 0.001 ** 2, f'{name} mirrored={mirror}: missing landmark {point}'
                worst = max(worst, math.sqrt(distance))
    print(f'male skull restoration: PASS (40 referenced source landmarks, worst {worst * 1000:.3f} mm)')


if __name__ == '__main__':
    main()
