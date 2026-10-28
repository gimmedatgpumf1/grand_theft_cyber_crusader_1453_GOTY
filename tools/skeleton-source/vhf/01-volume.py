"""Visible Human Female CT -> 2 x 2 x 2 mm bone volume.

The DICOM series is 670 x 673 x 1727 at 0.7227 x 0.7227 x 1.0 mm. Two-by-two-by-
two averaging gives 1.446 x 1.446 x 2.0 mm, which still resolves the joint gaps
between phalanges while fitting in memory. Averaging, not maximum: cortical bone
is a thin shell and a max filter would inflate it by a voxel on every side.
"""
import os
import sys

import numpy as np
import pydicom

# Usage: python 01-volume.py /path/to/VHF_DICOM /path/to/vol2.npy
SRC = sys.argv[1] if len(sys.argv) > 1 else '/tmp/comodo/VHF/DICOM'
DEST = sys.argv[2] if len(sys.argv) > 2 else '/tmp/vhf/vol2.npy'
files = sorted(os.listdir(SRC))
first = pydicom.dcmread(os.path.join(SRC, files[0]), stop_before_pixels=True)
rows, cols = int(first.Rows), int(first.Columns)
pixel = float(first.PixelSpacing[0])

z_block, y_block, x_block = 2, 2, 2
nz = len(files) // z_block
ny, nx = rows // y_block, cols // x_block
vol = np.zeros((nz, ny, nx), dtype=np.float32)
pending = None
for index in range(nz * z_block):
    slice_ = pydicom.dcmread(os.path.join(SRC, files[index])).pixel_array.astype(np.float32)
    slice_ = slice_[:ny * y_block, :nx * x_block]
    block = slice_.reshape(ny, y_block, nx, x_block).mean(axis=(1, 3))
    if index % 2 == 0:
        pending = block
    else:
        vol[index // 2] = (pending + block) * 0.5
    if index % 300 == 0:
        print('read', index, flush=True)

np.save(DEST, vol)
print('grid', vol.shape, 'spacing mm', 1.0 * z_block, pixel * y_block, pixel * x_block, flush=True)
print('written', DEST, flush=True)
