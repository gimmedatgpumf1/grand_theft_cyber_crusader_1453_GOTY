#!/usr/bin/env python3
"""Check real T-pose against the pre-change pose, not an invented rib target."""
import csv
import importlib.util
import tempfile
from pathlib import Path
import numpy as np

root=Path(__file__).resolve().parents[1]
p=root/'tools/skeleton-source/openskeleton/41-audit-rib-girdle.py'
spec=importlib.util.spec_from_file_location('audit',p); audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
before=audit.baseline()
with tempfile.TemporaryDirectory() as tmp:
    exe=Path(tmp)/'pose'; audit.tpose.compile_exporter(exe); after=audit.tpose.matrices(exe)
rows=audit.calculate({'before':before,'after':after})
stored=list(csv.DictReader((audit.OUT/'RIB_PROJECTION_AUDIT.tsv').open(),delimiter='\t'))
assert len(rows)==len(stored)==96
for row,saved in zip(rows,stored):
    for key,value in row.items():
        if isinstance(value,(float,np.floating)):
            assert abs(float(saved[key])-value)<1e-6
        else: assert saved[key]==str(value)
for sex,depth in [('male',20.808),('female',16.444)]:
    mesh=audit.tpose.whole.mesh(sex)
    def angle(pose,side):
        sign=1 if side=='L' else -1
        p=audit.tpose.transform(mesh['Clavicula_'+side],pose[sex,'Clavicula_'+side])
        ordered=p[np.argsort(sign*p[:,0])];n=len(ordered)//20
        chord=ordered[-n:].mean(0)-ordered[:n].mean(0)
        return np.degrees(np.arctan2(chord[1],sign*chord[0]))
    old=abs(angle(before,'L')-angle(before,'R'))
    new=abs(angle(after,'L')-angle(after,'R'))
    assert new<0.5 and new<old
    # The rigidly attached scapula follows the same SC motion; rib cage and
    # vertebrae have not been distorted or moved in response to unknown breath.
    for side in 'LR':
        name='Clavicula_'+side; scap='Scapula_'+side
        assert np.linalg.norm(after[sex,name]-before[sex,name])>1e-4
        assert np.allclose(after[sex,name],after[sex,scap],atol=1e-7)
        # Vertex-to-vertex clearance is only a regression proxy; it does not
        # rule out triangle intersections or establish a clinical joint gap.
        clav=audit.tpose.transform(mesh[name],after[sex,name])
        rib=audit.tpose.transform(mesh['Rib1_'+side],after[sex,'Rib1_'+side])
        assert audit.cKDTree(clav).query(rib)[0].min()>0.005
        for n in range(1,13):
            for name in (f'Rib{n}_{side}',f'T{n}'):
                assert np.allclose(before[sex,name],after[sex,name],atol=1e-10)
    assert np.allclose(before[sex,'Sternum'],after[sex,'Sternum'],atol=1e-10)
    actual=next(float(r['cage_depth_cm']) for r in stored if r['sex']==sex and r['pose']=='after')
    assert abs(actual-depth)<1e-6
print('Bilateral girdle symmetry, unchanged rib pose, forward projection and saved TSV PASS')
