"""Extract Rajagopal-model knee transforms using OpenSim 4.6.

Reference model only: it is not calibrated to the OpenSkeleton subjects.
"""
import hashlib
import tempfile
import urllib.request
from pathlib import Path
import opensim

COMMIT = 'de55d59962450f0d8a64005db42cb72c01ffe849'
EXPECTED = '3d646de18f3014343dc4d1f7c879e3d50d3f17f55ceceb92f93f32b26b76e869'
URL = f'https://api.github.com/repos/stanfordnmbl/coupled-exo-sim/contents/Rajagopal2015.osim?ref={COMMIT}'
OUT = Path(__file__).resolve().parents[3] / 'tests/rajagopal_knee_cases.txt'
opensim.Logger.setLevelString('error')


def fmt(values):
    return ' '.join(f'{v:.17g}' for v in values)


def transform(t):
    return [t.R().get(i,j) for i in range(3) for j in range(3)] + [t.p().get(i) for i in range(3)]


def derivative(f, x, order):
    components = opensim.StdVectorInt()
    for _ in range(order): components.append(0)
    return f.calcDerivative(components, opensim.Vector(1, x))


def val(f, x): return f.calcValue(opensim.Vector(1, x))


def emit_axis(rows, axis, bounds):
    f = axis.getFunction()
    n = f.getConcreteClassName()
    direction = axis.getAxis()
    xyz = [direction.get(i) for i in range(3)]
    if n == 'Constant':
        rows.append('axis ' + fmt(xyz) + ' 0 ' + fmt([val(f, 0)]))
        return
    if n == 'SimmSpline':
        spline = opensim.SimmSpline.safeDownCast(f)
        xs = [spline.getX(i) for i in range(spline.getSize())]
    elif n == 'LinearFunction':
        xs = list(bounds)
    else: raise ValueError('unsupported function: '+n)
    rows.append('axis ' + fmt(xyz) + f' {len(xs)} 0')
    for i,x in enumerate(xs):
        y, slope = val(f,x), derivative(f,x,1)
        quad = derivative(f,x,2)/2
        cubic = 0
        if i+1 < len(xs):
            h = xs[i+1]-x
            cubic = (val(f,xs[i+1])-y-slope*h-quad*h*h)/(h*h*h)
        rows.append('knot ' + fmt([x,y,slope,quad,cubic]))


def relative(parent, child):
    p, c = parent.R(), child.R()
    r = [sum(p.get(k,i)*c.get(k,j) for k in range(3)) for i in range(3) for j in range(3)]
    d = [child.p().get(k)-parent.p().get(k) for k in range(3)]
    t = [sum(p.get(k,i)*d[k] for k in range(3)) for i in range(3)]
    return r+t


request = urllib.request.Request(URL, headers={'Accept':'application/vnd.github.raw+json','User-Agent':'gtc-skeleton-tests'})
content = urllib.request.urlopen(request).read()
if hashlib.sha256(content).hexdigest() != EXPECTED: raise ValueError('unexpected reference model hash')
with tempfile.TemporaryDirectory() as d:
    path = Path(d) / 'reference.osim'; path.write_bytes(content)
    model = opensim.Model(str(path)); state = model.initSystem()
    rows = ['# Rajagopal 2015 knee model OpenSim 4.6 fixtures; not subject-fitted.']
    for side in ('r','l'):
        j = opensim.CustomJoint.safeDownCast(model.getJointSet().get('walker_knee_'+side))
        q = model.getCoordinateSet().get('knee_angle_'+side)
        lo, hi = q.getRangeMin(), q.getRangeMax()
        rows.append('joint ' + side + ' ' + fmt([lo,hi]))
        rows.append('parent ' + fmt(transform(j.getParentFrame().findTransformInBaseFrame())))
        rows.append('child ' + fmt(transform(j.getChildFrame().findTransformInBaseFrame())))
        spatial = j.getSpatialTransform()
        for name in ('rotation1','rotation2','rotation3','translation1','translation2','translation3'):
            emit_axis(rows, getattr(spatial,'get_'+name)(), (lo,hi))
        for fraction in (0,.073,.17,.26,.4,.5,.62,.75,.87,.98,1):
            angle = lo+(hi-lo)*fraction
            q.setValue(state,angle,False); model.realizePosition(state)
            p = model.getBodySet().get('femur_'+side).getTransformInGround(state)
            c = model.getBodySet().get('tibia_'+side).getTransformInGround(state)
            rows.append('sample ' + fmt([angle]+relative(p,c)))
        rows.append('end')
OUT.write_text('\n'.join(rows)+'\n')
print('wrote bilateral OpenSim reference-knee fixture')
