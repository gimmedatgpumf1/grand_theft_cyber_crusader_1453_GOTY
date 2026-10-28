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
    return [k for k in (root.keys() if kind == 'h5' else root.keys())
            if not k.startswith('__') and not k.startswith('#')]


def vertex_dataset(group, kind):
    """Find the N x 3 vertex array of one bone, whatever it is called."""
    if kind == 'h5':
        if 'Registered' in group:
            return np.asarray(group['Registered'], dtype=np.float32)
        best = None
        for key in group:
            item = group[key]
            if hasattr(item, 'shape') and item.ndim == 2 and 3 in item.shape:
                if best is None or item.shape[0] > best[1]:
                    best = (key, item.shape[0])
        if best is None:
            return None
        return np.asarray(group[best[0]], dtype=np.float32)
    for field in getattr(group, '_fieldnames', []):
        value = getattr(group, field)
        if getattr(value, 'ndim', 0) == 2 and 3 in value.shape:
            return np.asarray(value, dtype=np.float32)
    return None


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
    parser.add_argument('--out', required=True, help='output .npz path')
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
    if args.subject not in names_in(root, kind):
        available = [n for n in names_in(root, kind) if n.startswith('N')]
        sys.exit(f'no subject {args.subject} in {combined.name}\n'
                 f'it holds {len(available)}: {available[:8]} ...')

    subject = child(root, args.subject, kind)
    bones = [k for k in (subject.keys() if kind == 'h5' else subject._fieldnames)
             if not k.startswith('#')]
    print(f'{args.subject}: {len(bones)} structures')

    faces_root, faces_kind = open_source(faces_path)
    faces_names = set(names_in(faces_root, faces_kind))

    store, manifest, skipped = {}, {}, []
    for index, bone in enumerate(bones, 1):
        node = child(subject, bone, kind)
        verts = as_triples(vertex_dataset(node, kind))
        face_key = f'F{bone}'
        if face_key not in faces_names:
            skipped.append((bone, 'no triangulation'))
            continue
        faces = as_triples(np.asarray(child(faces_root, face_key, faces_kind),
                                      dtype=np.int32))
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

    out = Path(args.out)
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
    print(f'\nwrote {out} ({out.stat().st_size / 1e6:.1f} MB) and {out.with_suffix(".json").name}')
    print(f'  {len(manifest)} bones, longest axis of the whole body {span.max():.1f} raw units')
    if skipped:
        print(f'  {len(skipped)} skipped: {skipped[:4]}{" ..." if len(skipped) > 4 else ""}')
    print('Upload the .npz and the .json; the importer does the rest.')


if __name__ == '__main__':
    main()
