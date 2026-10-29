"""Measure each thin surface against its real projected source mesh faces.

This optional regeneration test requires NumPy and Shapely >= 2.1. It checks
coverage and added area; the small extra area includes anatomical bridges.
It does not validate the source registration or anatomical correctness.
"""
from pathlib import Path
import runpy
import numpy as np
import shapely

root = Path(__file__).resolve().parent.parent
source = root / 'tools/skeleton-source'
generator = runpy.run_path(str(source / 'openskeleton/04-line-graph.py'))
for sex in ('male', 'female'):
    bones, faces = generator['read_bones'](source / f'skeleton-{sex}.obj')
    meshes = {row.split()[1]: row.split() for row in (source / f'skeleton-{sex}.lines').read_text().splitlines()
              if row.startswith('M ')}
    for name, members in generator['unified_groups']().items():
        vertices = np.concatenate([bones[member] for member in members])
        centre = vertices.mean(axis=0)
        _, _, basis = np.linalg.svd(vertices-centre, full_matrices=False)
        triangles = []
        for member in members:
            xy = (bones[member]-centre) @ basis[:2].T
            polys = shapely.polygons(xy[faces[member]])
            triangles.extend(polys[shapely.area(polys) > 1e-14])
        actual = shapely.union_all(triangles, grid_size=0.00001)
        row = meshes[name]
        assert float(row[2]) <= 0.003
        n, count, _ = map(int, row[3:6])
        points = np.array(row[6:6+n*3], dtype=float).reshape(-1, 3)
        surface_faces = np.array(row[6+n*3:6+n*3+count*3], dtype=int).reshape(-1, 3)
        xy = (points-centre) @ basis[:2].T
        displayed = shapely.union_all(shapely.polygons(xy[surface_faces]), grid_size=0.00001)
        overlap = actual.intersection(displayed).area
        coverage = overlap / actual.area
        added = (displayed.area-overlap) / displayed.area
        hull = displayed.area / actual.convex_hull.area
        assert coverage >= 0.99 and added <= 0.015 and hull < 0.9, (sex, name, coverage, added, hull)
        print(f'{sex} {name}: {coverage*100:.2f}% projected source covered; {added*100:.2f}% extra; {hull*100:.1f}% of convex hull')
