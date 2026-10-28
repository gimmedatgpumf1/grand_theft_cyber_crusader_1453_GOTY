"""Visible Human Female CT -> one mesh per bone, grouped into the game's rig parts.

Every vertex in the output is a point on her own bone surface: the CT is
thresholded, the bones are separated as connected components, each component is
named against the atlas, and marching cubes turns her voxels into a surface.

Nothing is scaled. The output is her stature.
"""
import json
import numpy as np
from scipy import ndimage as ndi
from scipy.optimize import linear_sum_assignment
from skimage import measure

SPACING = np.array([2.0, 1.445312, 1.445312])          # mm per voxel, (z, y, x)
BONE_HU = 1600
MIN_VOXELS = 30
OUT = sys.argv[2] if len(sys.argv) > 2 else '/tmp/vhf/dense.obj'

import sys
VOLUME = sys.argv[1] if len(sys.argv) > 1 else '/tmp/vhf/vol2.npy'
vol = np.load(VOLUME)
bone = ndi.binary_fill_holes(ndi.binary_opening(vol > BONE_HU, np.ones((1, 2, 2))))
components, count = ndi.label(bone)
sizes = np.bincount(components.ravel())
sizes[0] = 0
keep = np.zeros(sizes.size, dtype=bool)
keep[sizes >= MIN_VOXELS] = True
keep[0] = False
bone = keep[components]
components, count = ndi.label(bone)
sizes = np.bincount(components.ravel())
sizes[0] = 0
print('components', count, 'voxels', int(bone.sum()), flush=True)

# ---------------------------------------------------------------- atlas
parts = {}
current = None
for line in open('/home/user/grand_theft_cyber_crusader_1453_GOTY/'
                 'tools/skeleton-source/skeleton-male.obj'):
    if line.startswith('o '):
        current = line[2:].strip()
        parts.setdefault(current, [])
    elif line.startswith('v ') and current:
        parts[current].append([float(x) for x in line.split()[1:4]])
parts = {k: np.array(v) * 1000.0 for k, v in parts.items()}
ATLAS = {k: np.stack([v[:, 1] + 8.0, -v[:, 2] + 240.0, v[:, 0] + 236.0], 1) for k, v in parts.items()}
names = list(ATLAS)
centres = {k: v.mean(0) for k, v in ATLAS.items()}
lengths = {k: float(np.linalg.norm(v.max(0) - v.min(0))) for k, v in ATLAS.items()}

# ---------------------------------------------------------------- assign
ids = [i for i in range(1, count + 1) if sizes[i] >= MIN_VOXELS]
comp_centre = {}
comp_length = {}
comp_volume = {}
for i in ids:
    idx = np.argwhere(components == i)
    comp_centre[i] = idx.mean(0) * SPACING
    comp_length[i] = float(np.linalg.norm((idx.max(0) - idx.min(0) + 1) * SPACING))
    comp_volume[i] = int(len(idx))

cost = np.zeros((len(ids), len(names)))
for a, i in enumerate(ids):
    for b, name in enumerate(names):
        d = np.linalg.norm(comp_centre[i] - centres[name]) / 40.0
        r = abs(np.log(max(comp_length[i], 1.0) / max(lengths[name], 1.0))) * 2.0
        cost[a, b] = d + r
rows, cols = linear_sum_assignment(cost)
assignment = {ids[r]: names[c] for r, c in zip(rows, cols)}

# A part may own several components (a hip bone is two components, teeth are
# many); every component still gets exactly one name, and a part that ends up
# with no component is reported rather than invented.
owned = {}
for i, name in assignment.items():
    owned.setdefault(name, []).append(i)
missing = [n for n in names if n not in owned]
print('parts with a component', len(owned), 'of', len(names), flush=True)
if missing:
    print('parts with no component:', missing, flush=True)

# ---------------------------------------------------------------- surfaces
groups = {}
details = []
for name, comps in owned.items():
    points = []
    faces = []
    offset = 0
    for i in comps:
        sub = components == i
        # One voxel of padding, or marching cubes cannot close the surface.
        padded = np.pad(sub, 1)
        vertices, triangles, _, _ = measure.marching_cubes(padded.astype(np.float32), level=0.5,
                                                           spacing=SPACING)
        vertices = vertices - SPACING          # undo the padding
        # marching_cubes returns (z, y, x); the game wants (x lateral, y up, z
        # anterior) at millimetres -> metres with the soles on the floor.
        xyz = np.stack([vertices[:, 2], vertices[:, 0], -vertices[:, 1]], axis=1) / 1000.0
        points.append(xyz)
        faces.append(triangles + offset)
        offset += len(xyz)
        details.append({'component': int(i), 'part': name, 'voxels': comp_volume[i],
                        'triangles': int(len(triangles))})
    groups[name] = (np.concatenate(points), np.concatenate(faces))

# ---------------------------------------------------------------- write
allz = np.concatenate([g[0] for g in groups.values()])
low, high = allz.min(0), allz.max(0)
print('raw bbox', np.round(low, 3), np.round(high, 3), flush=True)

lines = ['# Visible Human Female skeleton, segmented from her own CT.',
         '# Source: Visible Human Project (NLM), CT in COMODO-research/Comodo_VisibleHuman.',
         '# Z-Anatomy used only for bone names. See tools/skeleton-source/ATTRIBUTION.md.']
running = 1
for name in sorted(groups):
    xyz, faces = groups[name]
    lines.append(f'o {name}')
    for p in xyz:
        lines.append(f'v {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}')
    for f in faces:
        lines.append(f'f {running + f[0]} {running + f[1]} {running + f[2]}')
    running += len(xyz)
open(OUT, 'w').write('\n'.join(lines) + '\n')

json.dump({'source': 'Visible Human Female CT', 'bone_hu': BONE_HU,
           'components': count, 'parts': len(groups), 'details': details,
           'bbox': {'min': low.tolist(), 'max': high.tolist()}},
          open('/tmp/vhf/dense.json', 'w'), indent=1)
print('vertices', running - 1, 'groups', len(groups), 'written', OUT, flush=True)
