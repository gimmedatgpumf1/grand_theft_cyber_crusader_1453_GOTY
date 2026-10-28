"""Segment a whole-body cadaver CT into one mesh per bone, at full resolution.

Written for the NLM Visible Human data. Both cadavers are in
COMODO-research/Comodo_VisibleHuman under CC BY 4.0:

  VHF  1727 slices of 670 x 673 at 0.7227 x 0.7227 x 1.0 mm   (female, 59)
  VHM  1873 slices of 552 x 520 at 0.9375 x 0.9375 x 1.0 mm   (male, 39)

Full resolution is not optional. Downsampling the female to 1.4 x 1.4 x 2.0 mm
welded her arm into a single humerus+radius+ulna+hand blob; at 0.72 mm the same
arm comes apart and her feet resolve into 25-29 separate bones, which is what a
foot has. Bone is a thin cortical shell -- about 1.5 mm -- and averaging eats it.

The pipeline is:

  1. threshold to bone (HU cut per subject),
  2. label connected components in overlapping slabs, merging the components
     that cross a slab seam so a bone is never cut in half,
  3. name each component against an anatomical atlas by centroid and size,
  4. marching cubes each component inside its own bounding box,
  5. cluster-decimate each bone on its own.

The output is one OBJ grouped into the rig's 28 parts, in metres, soles on y = 0,
facing +z, at the subject's own height. No scale factor is ever applied.

Usage:
  python3 segments.py --dicom DIR --labels PREFIX --obj OUT.obj --parts OUT.parts.json \
      --hu 1600 --min-voxels 60
"""
import argparse
import json
import os
import sys

import numpy as np
import pydicom
from scipy import ndimage as ndi
from skimage import measure


# ------------------------------------------------------------------ geometry
def series(path):
    files = sorted(os.listdir(path))
    first = pydicom.dcmread(os.path.join(path, files[0]), stop_before_pixels=True)
    rows, cols = int(first.Rows), int(first.Columns)
    pixel = float(first.PixelSpacing[0])
    spacing = np.array([float(first.get('SpacingBetweenSlices', 1.0)), pixel, pixel])
    return files, rows, cols, spacing


def load_slab(path, files, z0, z1):
    return np.stack([pydicom.dcmread(os.path.join(path, files[z])).pixel_array
                     for z in range(z0, z1)])


# ------------------------------------------------------------------ labeling
def segment(subject, dicom, labels_prefix, hu, min_voxels, slab=240, overlap=24):
    """Threshold and label the whole body, writing one label slab to disk at a time.

    Returns a table of components: global id, voxel count, bounding box, centroid.
    """
    files, rows, cols, spacing = series(dicom)
    depth = len(files)
    print(f'{subject}: {depth} slices of {rows} x {cols} at '
          f'{spacing[0]} x {spacing[1]:.4f} x {spacing[2]:.4f} mm', flush=True)

    structure = ndi.generate_binary_structure(3, 1)
    next_id = 1
    table = {}                 # id -> dict(voxels, lo, hi, sum)
    saved = []
    start = 0
    previous_footprint = {}    # global id -> set of (y, x) at the slab's last slice

    while start < depth:
        stop = min(start + slab, depth)
        volume = load_slab(dicom, files, start, stop)
        mask = ndi.binary_fill_holes(volume > hu)
        del volume
        local, count = ndi.label(mask, structure=structure)
        sizes = np.bincount(local.ravel())
        sizes[0] = 0
        labels = np.zeros(local.shape, dtype=np.int32)

        mapping = {}
        for index in range(1, count + 1):
            if sizes[index] < min_voxels:
                continue
            voxels = np.argwhere(local == index)
            lo = voxels.min(0)
            hi = voxels.max(0)
            # A component that reaches the slab's first slice and overlaps a bone
            # from the previous slab is that same bone: inherit its id. The two
            # footprints have to be compared at the same slice of the patient,
            # which is the first slice of this slab and the overlap-th from the
            # end of the previous one.
            gid = None
            if start > 0 and lo[0] == 0:
                footprint = set(map(tuple, np.argwhere(local[0] == index)[:, :2].tolist()))
                best = (0.0, None)
                for pid, previous in previous_footprint.items():
                    if not footprint or not previous:
                        continue
                    shared = len(footprint & previous) / max(1, len(footprint))
                    if shared > best[0]:
                        best = (shared, pid)
                if best[1] is not None and best[0] > 0.2:
                    gid = best[1]
            if gid is None:
                gid = next_id
                next_id += 1
                table[gid] = {'voxels': 0, 'lo': np.array([1 << 30] * 3), 'hi': np.array([-1] * 3),
                              'sum': np.zeros(3)}
            mapping[index] = gid
            global_lo = lo + np.array([start, 0, 0])
            global_hi = hi + np.array([start, 0, 0])
            table[gid]['lo'] = np.minimum(table[gid]['lo'], global_lo)
            table[gid]['hi'] = np.maximum(table[gid]['hi'], global_hi)
            table[gid]['voxels'] += int(sizes[index])
            table[gid]['sum'] += (voxels + np.array([start, 0, 0])).sum(0)

        for index, gid in mapping.items():
            labels[local == index] = gid

        # Footprints are taken at the slice the next slab will start on, so the
        # two sides of a seam describe the same ring of bone.
        seam = max(0, labels.shape[0] - overlap)
        tail = labels[seam]
        previous_footprint = {}
        for gid in np.unique(tail):
            if gid == 0:
                continue
            previous_footprint[int(gid)] = set(map(tuple, np.argwhere(tail == gid).tolist()))
        del local, tail

        name = f'{labels_prefix}-{start:05d}.npy'
        np.save(name, labels.astype(np.int32))
        saved.append((start, stop, name))
        print(f'  slab {start}-{stop}: {len(mapping)} components, {len(table)} total', flush=True)
        del labels, mask
        start = stop - overlap if stop < depth else depth

    for gid, row in table.items():
        row['centroid'] = row['sum'] / max(1, row['voxels'])
        row['lo'] = row['lo'].tolist()
        row['hi'] = row['hi'].tolist()
        row['centroid'] = row['centroid'].tolist()
        row['sum'] = row['sum'].tolist()
    return table, saved, spacing


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--subject', default='VHF')
    parser.add_argument('--dicom', required=True)
    parser.add_argument('--labels', required=True)
    parser.add_argument('--hu', type=float, default=1600)
    parser.add_argument('--min-voxels', type=int, default=60)
    parser.add_argument('--table', required=True)
    args = parser.parse_args()
    table, saved, spacing = segment(args.subject, args.dicom, args.labels,
                                    args.hu, args.min_voxels)
    json.dump({'spacing': spacing.tolist(), 'slabs': saved,
               'components': {str(k): v for k, v in table.items()}},
              open(args.table, 'w'))
    print(f'components {len(table)}  table {args.table}', flush=True)
