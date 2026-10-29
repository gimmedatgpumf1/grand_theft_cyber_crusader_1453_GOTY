"""Small real-MAT regression for the OpenSkeleton extraction script.

The full source archive is not needed; use synthetic data with the same documented
struct names and verify Registered is chosen, not an unrelated array.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent
script = root / 'tools/skeleton-source/openskeleton/02-export-subject.py'
cmd = script.with_name('extract-male.cmd')
marker = '::PYTHON_SOURCE::\n'
assert cmd.read_text().split(marker, 1)[1] == script.read_text(), 'embedded CMD extractor is stale'

try:
    import numpy as np
    import scipy.io
    import h5py
except ImportError:
    print('OpenSkeleton extraction: SKIP (numpy, scipy or h5py not installed)')
    raise SystemExit(0)

verts = np.array([[1., 2., 3.], [4., 5., 6.], [7., 8., 9.], [10., 11., 12.]])
faces = np.array([[1, 2, 3], [2, 3, 4]], dtype=np.int32)
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    scipy.io.savemat(d / 'Faces.mat', {'Faces': {'FHead': faces}}, do_compression=True)
    scipy.io.savemat(d / 'NM_combined.mat', {
        'NM_combined': {'N197925': {'Head': {'Other': verts + 1000, 'Registered': verts}}}
    }, do_compression=True)
    for kind in ('v5', 'h5'):
        if kind == 'h5':
            (d / 'NM_combined.mat').unlink()
            with h5py.File(d / 'NM_combined.mat', 'w') as h5:
                bone = h5.create_group('NM_combined/N197925/Head')
                bone['Other'] = verts + 1000
                bone['Registered'] = verts.T
        for mode in ('script', 'cmd'):
            output = d / f'{kind}-{mode}.npz'
            if mode == 'script':
                command = [sys.executable, str(script)]
            else:
                # Same Python entry point the CMD uses; exercises the embedded
                # program without pretending to run the Windows batch shell.
                entry = ("import sys; p=sys.argv[1]; sys.argv=[p]+sys.argv[2:]; "
                         "exec(compile(open(p,encoding='utf-8').read().split('::PYTHON_SOURCE::\\n',1)[1],p,'exec'))")
                command = [sys.executable, '-c', entry, str(cmd)]
            subprocess.run([*command, '--dir', str(d), '--subject', 'N197925',
                            '--out', str(output)], check=True, stdout=subprocess.DEVNULL)
            with np.load(output) as data:
                assert set(data.files) == {'v_Head', 'f_Head'}
                assert np.array_equal(data['v_Head'], verts)
                assert np.array_equal(data['f_Head'], faces)
            doc = json.loads(output.with_suffix('.json').read_text())
            assert doc['subject'] == 'N197925' and not doc['skipped']
print('OpenSkeleton extraction: PASS (standalone script and CMD Python entry, synthetic v5 and HDF5)')
