"""Extract the AAH shoulder/wrist couplers and OpenSim 4.6 verification data.

This is a reference model, not a calibration of the OpenSkeleton subjects.
No network or OpenSim dependency is introduced into the game or normal tests.
"""
import hashlib
import tempfile
import urllib.request
from pathlib import Path
import opensim

COMMIT = '99ac3d066685b4beb77fc381682a700d379d51cc'
MODELS = (
    ('RightArmAndHand_NoMuscles.osim', '0eaa91f0e624d80904f82885a5c91de3a7516fe6eef811fa11cfc3a73b87451b'),
    ('LeftArmAndHand_NoMuscles.osim', '1b46a7bbd1a3f1237cac60f64b0bf5bd50dd9f97c17e717d5e0f1edd03bde741'),
)
OUT = Path(__file__).resolve().parents[3] / 'tests/aah_coupling_cases.txt'
opensim.Logger.setLevelString('error')


def value(f, x):
    return f.calcValue(opensim.Vector(1, x))


def derivative(f, x, order):
    components = opensim.StdVectorInt()
    for _ in range(order):
        components.append(0)
    return f.calcDerivative(components, opensim.Vector(1, x))


def row(kind, values):
    return kind + ' ' + ' '.join(f'{x:.17g}' for x in values)


rows = ['# AAH model spline couplings and OpenSim 4.6 values; see tools/skeleton-source/reference/AAH_LICENSE.txt',
        '# Reference-model shoulder/wrist data, not fitted to OpenSkeleton.']
with tempfile.TemporaryDirectory() as directory:
    for name, expected in MODELS:
        url = f'https://api.github.com/repos/nishbo/ms_arm_and_hand/contents/AAH%20Model/{name}?ref={COMMIT}'
        request = urllib.request.Request(url, headers={'Accept': 'application/vnd.github.raw+json', 'User-Agent': 'gtc-skeleton-tests'})
        content = urllib.request.urlopen(request).read()
        if hashlib.sha256(content).hexdigest() != expected:
            raise ValueError('unexpected model hash')
        path = Path(directory) / name
        path.write_bytes(content)
        model = opensim.Model(str(path))
        model.initSystem()
        constraints = model.getConstraintSet()
        if constraints.getSize() != 13:
            raise ValueError('unexpected coupler count')
        for i in range(constraints.getSize()):
            c = opensim.CoordinateCouplerConstraint.safeDownCast(constraints.get(i))
            if c is None or c.getIndependentCoordinateNames().getSize() != 1:
                raise ValueError('unsupported constraint')
            f = opensim.SimmSpline.safeDownCast(c.getFunction())
            if f is None or f.getSize() < 2:
                raise ValueError('unsupported coupling function')
            xs = [f.getX(j) for j in range(f.getSize())]
            rows.append(f'coupling {c.getName()} {c.getIndependentCoordinateNames().get(0)} {c.getDependentCoordinateName()} {len(xs)}')
            for j, x in enumerate(xs):
                y, b = value(f, x), derivative(f, x, 1)
                quadratic = derivative(f, x, 2) / 2
                cubic = 0
                if j + 1 < len(xs):
                    h = xs[j+1]-x
                    cubic = (value(f, xs[j+1])-y-b*h-quadratic*h*h)/(h*h*h)
                rows.append(row('knot', [x, y, b, quadratic, cubic]))
            for x in [xs[0]-(xs[-1]-xs[0])*0.2, xs[0], *[xs[0]+(xs[-1]-xs[0])*t for t in (.13, .25, .5, .7, .9)], xs[-1], xs[-1]+(xs[-1]-xs[0])*0.2]:
                rows.append(row('sample', [x, value(f, x)]))
            rows.append('end')
OUT.write_text('\n'.join(rows)+'\n')
print('wrote 26 reference shoulder/wrist couplings')
