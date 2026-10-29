@echo off
setlocal
set "DATA=%~dp0"
if not exist "%DATA%NM_combined.mat" (
  echo Put NM_combined.mat next to this file and run it again.
  goto end
)
if not exist "%DATA%Faces.mat" (
  echo Put Faces.mat next to this file and run it again.
  goto end
)
where py >nul 2>nul
if errorlevel 1 (
  echo Python 3 is required. Install it from python.org and run this again.
  goto end
)
py -3 -c "import numpy, scipy, h5py" >nul 2>nul
if errorlevel 1 (
  echo Installing Python packages needed to extract the skeleton...
  py -3 -m pip install numpy scipy h5py
  if errorlevel 1 goto failed
)
py -3 -c "import sys; p=sys.argv[1]; sys.argv=[p]+sys.argv[2:]; exec(compile(open(p,encoding='utf-8').read().split('::PYTHON_SOURCE::\n',1)[1],p,'exec'))" "%~f0" --dir "%DATA%." --subject N197925 --out "%DATA%male-N197925.npz"
if errorlevel 1 goto failed
echo.
echo Upload male-N197925.npz and male-N197925.json from "%DATA%."
goto end
:failed
echo Extraction failed. Please send the error shown above.
:end
pause
exit /b
::PYTHON_SOURCE::
"""Pull one subject out of the OpenSkeleton .mat files into a single small file.

The Zenodo archive is 3.6 GB because it holds 102 subjects. A skeleton is about
2 MB of vertices, so one subject is a small file and the rest is payload that
never needs to move.

    python3 02-export-subject.py --dir /path/to/ZENODO --subject N186416 \
        --out /tmp/openskeleton/N186416.npz

Reads `NM_combined.mat` and `Faces.mat` with HDF5 indexing where possible, so a
2.5 GB file is never held in memory; each bone is streamed out on its own.

Layout, from the authors' EXAMPLE_GetData.m:

    verts = NM_combined.(case).(bone).Registered     N x 3
    faces = Faces.(['F', bone])                      M x 3, shared by every subject

Output is one .npz plus a JSON manifest: one float32 N x 3 array and one int32
M x 3 array per bone, named by the bone. Nothing is resampled, scaled or
reoriented here -- that is the importer's job, where it can be checked.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np


def open_source(path):
    """Return (root, kind) for a .mat, preferring streaming HDF5."""
    try:
        import h5py
    except ImportError:
        sys.exit('h5py is needed to read a 2.5 GB MATLAB file in pieces:\n'
                 '    pip install h5py numpy')
    try:
        return h5py.File(path, 'r'), 'h5'
    except OSError:
        from scipy.io import loadmat
        print(f'  {path.name} is not HDF5 (MATLAB v7.3); loading with scipy, '
              'this needs several GB of RAM')
        return loadmat(str(path), squeeze_me=True, struct_as_record=False), 'v5'


def child(node, name, kind):
    """Field access that works for an HDF5 group, a dict, and a MATLAB struct."""
    if kind == 'h5' or isinstance(node, dict):
        return node[name]
    return getattr(node, name)


def names_in(root, kind):
    """Child names of an HDF5 group, a dict, or a scipy mat_struct alike."""
    if kind == 'h5' or isinstance(root, dict):
        keys = list(root.keys())
    else:
        keys = list(getattr(root, '_fieldnames', []))
    return [k for k in keys if not k.startswith('__') and not k.startswith('#')]


def index_named(root, kind, depth=4):
    """Every named node reachable from the root, keyed by its own name.

    A .mat file does not start at the struct. `load('NM_combined.mat')` creates a
    variable called `NM_combined` whose fields are the subjects, so the subject
    sits one level below the file root -- and `Faces.mat` wraps its datasets the
    same way in a variable called `Faces`. Rather than assume how many wrappers
    there are, walk down and index what is actually there.
    """
    found = {}

    def walk(node, level):
        if level > depth:
            return
        for name in names_in(node, kind):
            try:
                value = child(node, name, kind)
            except (KeyError, AttributeError, ValueError):
                continue
            found.setdefault(name, value)
            if hasattr(value, 'keys') or hasattr(value, '_fieldnames'):
                walk(value, level + 1)

    walk(root, 0)
    return found


def describe(root, kind, limit=12):
    """What the file actually holds, for the error message when a lookup fails."""
    lines = []
    for name in names_in(root, kind)[:limit]:
        node = child(root, name, kind)
        inner = names_in(node, kind)
        head = ', '.join(inner[:4])
        lines.append(f'    {name}: {len(inner)} entries' + (f' ({head} ...)' if head else ''))
    return '\n'.join(lines)


def vertex_dataset(group, kind):
    """Use the documented Registered vertices; never guess a different array."""
    if kind == 'h5':
        if 'Registered' not in group:
            return None
        return np.asarray(group['Registered'], dtype=np.float32)
    value = getattr(group, 'Registered', None)
    return None if value is None else np.asarray(value, dtype=np.float32)


def as_triples(array):
    """MATLAB stores columns first, so an N x 3 mat may arrive transposed."""
    array = np.asarray(array)
    if array.ndim != 2:
        return None
    if array.shape[1] != 3 and array.shape[0] == 3:
        array = array.T
    return array if array.shape[1] == 3 else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dir', required=True, help='directory holding the Zenodo .mat files')
    parser.add_argument('--subject', default='N186416',
                        help='subject key, as in Metadata.mat (default: the female closest to 170 cm)')
    parser.add_argument('--out', help='output .npz path')
    parser.add_argument('--probe', action='store_true',
                        help='just describe the structure of the file and stop')
    args = parser.parse_args()

    source = Path(args.dir)
    combined = source / 'NM_combined.mat'
    faces_path = source / 'Faces.mat'
    for path in (combined, faces_path):
        if not path.is_file():
            sys.exit(f'missing {path}\n'
                     'Both NM_combined.mat and Faces.mat are needed: the first holds '
                     'vertices, the second the triangulation.')

    print(f'opening {combined.name} ({combined.stat().st_size / 1e9:.2f} GB)')
    root, kind = open_source(combined)
    if args.probe:
        print(describe(root, kind))
        return
    subjects = index_named(root, kind)
    if args.subject not in subjects:
        heads = [n for n in subjects if n.startswith('N') and n[1:].isdigit()]
        sys.exit(f'no subject {args.subject} in {combined.name}\n'
                 f'  {len(heads)} subject-like names: {heads[:8]} ...\n'
                 f'  structure of the file:\n{describe(root, kind)}')

    subject = subjects[args.subject]
    bones = names_in(subject, kind)
    print(f'{args.subject}: {len(bones)} structures')

    faces_root, faces_kind = open_source(faces_path)
    faces_names = index_named(faces_root, faces_kind)

    store, manifest, skipped = {}, {}, []
    for index, bone in enumerate(bones, 1):
        node = child(subject, bone, kind)
        verts = as_triples(vertex_dataset(node, kind))
        face_key = f'F{bone}'
        if face_key not in faces_names:
            skipped.append((bone, 'no triangulation'))
            continue
        faces = as_triples(np.asarray(faces_names[face_key], dtype=np.int32))
        if verts is None or faces is None or len(verts) == 0 or len(faces) == 0:
            skipped.append((bone, 'empty'))
            continue
        if faces.max() > len(verts):
            skipped.append((bone, f'faces index past vertices ({faces.max()} > {len(verts)})'))
            continue
        store[f'v_{bone}'] = verts
        store[f'f_{bone}'] = faces
        manifest[bone] = {'vertices': int(len(verts)), 'triangles': int(len(faces))}
        if index % 25 == 0:
            print(f'  {index}/{len(bones)} bones', flush=True)

    if faces_kind == 'h5':
        faces_root.close()
    if kind == 'h5':
        root.close()
    if not store:
        sys.exit('no geometry extracted -- every structure was skipped')

    # `--out` is whatever the caller typed. numpy silently appends .npz to a name
    # that lacks it, which once wrote the mesh beside an empty directory that had
    # been created at the same path -- so the file is resolved here, explicitly.
    out = Path(args.out)
    if out.is_dir():
        out = out / f'{args.subject}.npz'
    elif out.suffix.lower() != '.npz':
        out = Path(f'{out}.npz')
    out.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(out, **store)
    allv = np.concatenate([store[k] for k in store if k.startswith('v_')])
    span = allv.max(0) - allv.min(0)
    manifest_doc = {
        'subject': args.subject,
        'structures': manifest,
        'skipped': skipped,
        'extent': span.tolist(),
        'units': 'raw, as stored by the dataset',
        'note': 'Vertices are registered template shapes, one per bone. Nothing is scaled.',
    }
    out.with_suffix('.json').write_text(json.dumps(manifest_doc, indent=1))
    manifest_path = out.with_suffix('.json')
    print(f'\nwrote {out} ({out.stat().st_size / 1e6:.1f} MB) and '
          f'{manifest_path.name} ({manifest_path.stat().st_size / 1e3:.0f} kB)')
    print(f'  {len(manifest)} bones, longest axis of the whole body {span.max():.1f} raw units')
    if skipped:
        print(f'  {len(skipped)} skipped: {skipped[:4]}{" ..." if len(skipped) > 4 else ""}')
    print('Upload the .npz and the .json; the importer does the rest.')


if __name__ == '__main__':
    main()
