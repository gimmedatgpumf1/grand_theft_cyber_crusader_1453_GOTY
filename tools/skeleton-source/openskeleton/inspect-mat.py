"""Read the OpenSkeleton .mat files and report what is actually inside them.

OpenSkeleton ships MATLAB structs, not meshes, so nothing can be assumed about
their layout. This prints the structure before anything imports it: field names,
array shapes, the subject table and every skeletal structure name. Run it once
and the importer can be written against facts instead of guesses.

    python3 tools/skeleton-source/openskeleton/inspect-mat.py /tmp/openskeleton

The directory should hold the files listed in tools/skeleton-source/openskeleton/README.md.
Only the metadata file is needed for a subject list; the mesh files are read with
HDF5 indexing, so a 2.5 GB file is never loaded into memory in one piece.
"""
import sys
from pathlib import Path

import numpy as np

try:
    import h5py
except ImportError:  # pragma: no cover - the message is the point
    sys.exit('h5py is required for MATLAB v7.3 files: pip install h5py')


def tree(node, prefix='', depth=0, limit=3):
    """Describe an HDF5 group without reading its datasets."""
    if depth > limit:
        return
    for name, item in node.items():
        if isinstance(item, h5py.Group):
            print(f'{prefix}{name}/')
            tree(item, prefix + '  ', depth + 1, limit)
        else:
            shape = item.shape
            kind = item.dtype.kind
            print(f'{prefix}{name}  {shape}  {item.dtype}')
            if kind in 'US' and shape and np.prod(shape) < 64:
                try:
                    print(f'{prefix}  value = {item[()]!r}')
                except Exception:
                    pass


def open_mat(path):
    """Return an object exposing the root of either a v7.3 or an older .mat."""
    try:
        return h5py.File(path, 'r')
    except OSError:
        from scipy.io import loadmat
        print(f'  (not HDF5, falling back to scipy for {path.name})')
        return loadmat(str(path), squeeze_me=False, struct_as_record=False)


def describe_metadata(path):
    print(f'\n=== {path.name} ===')
    root = open_mat(path)
    if isinstance(root, h5py.File):
        tree(root, '  ', 0, 3)
        root.close()
    else:
        for key, value in root.items():
            if key.startswith('__'):
                continue
            print(f'  {key}: {type(value).__name__} {getattr(value, "shape", "")}')


def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else '/tmp/openskeleton')
    if not directory.is_dir():
        sys.exit(f'no such directory: {directory}\n'
                 'See tools/skeleton-source/openskeleton/README.md for the file list.')
    mats = sorted(directory.glob('*.mat'))
    if not mats:
        sys.exit(f'no .mat files in {directory}')
    print(f'{len(mats)} file(s) in {directory}')
    for path in sorted(mats, key=lambda p: p.stat().st_size):
        describe_metadata(path)
    print('\nNext: pick a female subject whose stature is closest to the canonical '
          '1.70 m, using the field names printed above -- they are not guessed '
          'here on purpose, see README.md.')


if __name__ == '__main__':
    main()
