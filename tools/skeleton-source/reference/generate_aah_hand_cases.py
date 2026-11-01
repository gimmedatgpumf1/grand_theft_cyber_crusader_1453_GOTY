"""Regenerate the AAH-model reference cases with OpenSim 4.6.

The fixture captures 32 *reference-model* finger joints, not subject-fitted
OpenSkeleton joints. Network is only used here, never by the game or tests.
"""
import hashlib
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path
import opensim

COMMIT = '99ac3d066685b4beb77fc381682a700d379d51cc'
MODELS = (
    ('RightArmAndHand_NoMuscles.osim', '0eaa91f0e624d80904f82885a5c91de3a7516fe6eef811fa11cfc3a73b87451b'),
    ('LeftArmAndHand_NoMuscles.osim', '1b46a7bbd1a3f1237cac60f64b0bf5bd50dd9f97c17e717d5e0f1edd03bde741'),
)
OUT = Path(__file__).resolve().parents[3] / 'tests/aah_hand_joint_cases.txt'
CHAIN_OUT = OUT.with_name('aah_finger_chain_cases.txt')
opensim.Logger.setLevelString('error')


def frame(x):
    return [x.R().get(i, j) for i in range(3) for j in range(3)] + [x.p().get(i) for i in range(3)]


def local(parent, child):
    a, b = frame(parent), frame(child)
    rotation = [sum(a[k*3+i]*b[k*3+j] for k in range(3)) for i in range(3) for j in range(3)]
    delta = [b[9+i]-a[9+i] for i in range(3)]
    translation = [sum(a[k*3+i]*delta[k] for k in range(3)) for i in range(3)]
    return rotation + translation


def row(kind, values):
    return kind + ' ' + ' '.join(f'{x:.17g}' for x in values)


chain_rows = ['# Complete AAH reference finger chains, verified in OpenSim 4.6; not fitted to OpenSkeleton.']
rows = ['# OpenSim 4.6 AAH bilateral reference-hand model, MIT license; see tools/skeleton-source/reference/AAH_LICENSE.txt',
        '# 32 joints, 5 reference samples each; not fitted to OpenSkeleton.']
with tempfile.TemporaryDirectory() as directory:
    for model_name, expected_hash in MODELS:
        url = f'https://api.github.com/repos/nishbo/ms_arm_and_hand/contents/AAH%20Model/{model_name}?ref={COMMIT}'
        request = urllib.request.Request(url, headers={'Accept': 'application/vnd.github.raw+json', 'User-Agent': 'gtc-skeleton-tests'})
        content = urllib.request.urlopen(request).read()
        if hashlib.sha256(content).hexdigest() != expected_hash:
            raise ValueError('unexpected source model hash: ' + model_name)
        path = Path(directory) / model_name
        path.write_bytes(content)
        model = opensim.Model(str(path))
        state = model.initSystem()
        # Both models list the thumb CMC phantom chain and all finger joints last.
        elements = list(ET.fromstring(content).iter('CustomJoint'))[-16:]
        if len(elements) != 16:
            raise ValueError('unexpected hand joint count')
        for element in elements:
            joint_name = element.attrib['name']
            joint = model.getJointSet().get(joint_name)
            parent, child = joint.getParentFrame(), joint.getChildFrame()
            coords = []
            for axis in element.findall('SpatialTransform/TransformAxis'):
                name = axis.findtext('coordinates')
                if not name:
                    continue
                if axis.attrib['name'].startswith('translation') or axis.findtext('LinearFunction/coefficients').strip() != '1 0':
                    raise ValueError('unsupported axis in ' + joint_name)
                direction = [float(x) for x in axis.findtext('axis').split()]
                coord = model.updCoordinateSet().get(name)
                if coord.isConstrained(state):
                    raise ValueError('dependent coordinate in ' + joint_name)
                coords.append((coord, direction, coord.getRangeMin(), coord.getRangeMax()))
            if len(coords) not in (1, 2):
                raise ValueError('unexpected DOF count in ' + joint_name)
            rows.append(f'joint {joint_name} {len(coords)}')
            rows.append(row('parent', frame(parent.findTransformInBaseFrame())))
            rows.append(row('child', frame(child.findTransformInBaseFrame())))
            for _, direction, lower, upper in coords:
                rows.append(row('axis', direction + [lower, upper]))
            poses = [[max(low, min(0, high)) for _, _, low, high in coords],
                     [low for _, _, low, high in coords], [high for _, _, low, high in coords],
                     [low + (high-low)*0.25 for _, _, low, high in coords],
                     [low + (high-low)*(0.7 if i == 0 else 0.3) for i, (_, _, low, high) in enumerate(coords)]]
            for angles in poses:
                for (coord, _, _, _), angle in zip(coords, angles):
                    coord.setValue(state, angle, False)
                model.realizePosition(state)
                transform = local(parent.findBaseFrame().getTransformInGround(state),
                                  child.findBaseFrame().getTransformInGround(state))
                rows.append(row('sample', angles + transform))
            rows.append('end')
        prefix = 'RA' if model_name.startswith('Right') else 'LA'
        by_name = {element.attrib['name']: element for element in elements}
        for digit in range(1, 6):
            if digit == 1:
                names = [f'{prefix}3T_{prefix}4M1_PHANT', f'{prefix}4M1_PHANT_{prefix}4M1',
                         f'{prefix}4M1_{prefix}5P1', f'{prefix}5P1_{prefix}6D1']
            else:
                names = [f'{prefix}4M{digit}_{prefix}5P{digit}', f'{prefix}5P{digit}_{prefix}6M{digit}',
                         f'{prefix}6M{digit}_{prefix}7D{digit}']
            chain = [model.getJointSet().get(n) for n in names]
            coords = []
            for joint_name in names:
                element = by_name[joint_name]
                for axis in element.findall('SpatialTransform/TransformAxis'):
                    coord_name = axis.findtext('coordinates')
                    if coord_name:
                        coord = model.updCoordinateSet().get(coord_name)
                        coords.append((coord, coord.getRangeMin(), coord.getRangeMax()))
            if len(coords) != (5 if digit == 1 else 4):
                raise ValueError('unexpected finger chain degrees of freedom')
            chain_rows.append(f'chain {len(names)} ' + ' '.join(names))
            for fraction in (0.1, 0.25, 0.5, 0.75, 0.9):
                angles = [low + (high-low)*(fraction if i % 2 == 0 else 1-fraction)
                          for i, (_, low, high) in enumerate(coords)]
                for (coord, _, _), angle in zip(coords, angles):
                    coord.setValue(state, angle, False)
                model.realizePosition(state)
                first = chain[0].getParentFrame().findBaseFrame().getTransformInGround(state)
                last = chain[-1].getChildFrame().findBaseFrame().getTransformInGround(state)
                chain_rows.append(row('pose', angles + local(first, last)))
            chain_rows.append('end')
OUT.write_text('\n'.join(rows) + '\n')
CHAIN_OUT.write_text('\n'.join(chain_rows) + '\n')
print(f'wrote {OUT}: 32 joints and 160 OpenSim transforms; plus 50 full-chain poses')
