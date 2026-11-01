"""Name a segmented bone piece by where it sits in a human body.

The atlas cannot name a limb in a cadaver scan, because a cadaver is not in the
atlas's pose -- the Visible Human Female was scanned with both arms folded and
both legs together, and nearest-atlas-axis assignment then hands a shin to the
patella and a humerus to the pelvis. What a scan does measure reliably is
height and side, so the appendicular skeleton is named by height band and side,
and the axial skeleton (skull, spine, ribs, sternum, pelvis, scapulae,
clavicles) is left to the atlas, whose parts do not move between poses.

Every band is in millimetres up from the soles and comes from `landmarks.py`,
which measures this subject rather than assuming an adult average.
"""
import numpy as np

def side_of(x_mm, midline):
    """Which side of the rig a piece belongs to.

    The shipped rig puts the left limb at positive x (`skeleton-male.obj` has
    `clavicle_l` at x +0.011..+0.154), and the scan's x axis maps straight onto
    the game's, so left is above the midline.
    """
    return 'l' if x_mm > midline else 'r'


def landmarks(components, spacing):
    """Find the ankle, knee and hip heights in this subject.

    A cadaver scan fuses the thigh to the shin and the shin to the foot, so the
    joints are not gaps to be found; they are where the two fused masses overlap.
    Each leg is therefore read as its two masses -- the long one above and the
    long one below -- and the knee is placed halfway through the overlap, which
    is the middle of the joint bridge. This has to be derived rather than
    assumed, because the two cadavers are different heights and neither of them
    is the population average.
    """
    top = max((row['hi'][0] + 1) for row in components.values()) * spacing[0]
    masses = []
    for row in components.values():
        lo, hi = np.array(row['lo']), np.array(row['hi'])
        extent = (hi - lo + 1) * spacing
        centre = np.array(row['centroid']) * spacing
        # A folded or hanging arm is also one long mass, so only the lower half
        # of the body can hold a leg.
        if row['voxels'] > 150000 and extent[0] > 250 and centre[0] < 0.5 * top:
            masses.append({'low': float(lo[0] * spacing[0]),
                           'high': float((hi[0] + 1) * spacing[0]),
                           'x': float(centre[2]), 'voxels': row['voxels']})
    if not masses:
        return {'knee': 0.0, 'ankle': 0.0, 'hip': 0.0}

    # The midline is the middle of the body's own lateral span, so a folded arm
    # on one side cannot drag it.
    middle = float((min(row['lo'][2] for row in components.values())
                    + max(row['hi'][2] for row in components.values()) + 1) * spacing[2] / 2.0)
    hips, knees = [], []
    for side in (True, False):
        leg = [m for m in masses if (m['x'] > middle) == side]
        if len(leg) < 2:
            continue
        leg.sort(key=lambda m: -m['voxels'])
        upper, lower = leg[0], leg[1]
        if upper['high'] < lower['high']:
            upper, lower = lower, upper
        hips.append(upper['high'])
        knees.append((upper['low'] + lower['high']) / 2.0)

    # The ankle is the joint just above the tarsals, measured from the foot
    # rather than from where the shin component happens to end.
    foot_top = 0.0
    for row in components.values():
        lo, hi = np.array(row['lo']), np.array(row['hi'])
        centre = np.array(row['centroid']) * spacing
        extent = (hi - lo + 1) * spacing
        if centre[0] < 0.15 * max(hips) and max(extent) < 160.0:
            foot_top = max(foot_top, float((hi[0] + 1) * spacing[0]))

    return {'knee': float(np.median(knees)) if knees else 0.0,
            'ankle': foot_top + 5.0,
            'hip': float(np.median(hips)) if hips else 0.0,
            'midline': middle}


def limb_part(centre_mm, extent_mm, voxels, marks, forearm_tips):
    """Return the rig part for a piece, or None if it belongs to the trunk.

    `centre_mm` and `extent_mm` are in the scan frame: z up, x lateral, y depth.
    """
    z, y, x = float(centre_mm[0]), float(centre_mm[1]), float(centre_mm[2])
    side = side_of(x, marks['midline'])
    lateral = abs(x - marks['midline'])

    # The hand: whatever sits just beyond the end of a forearm. Her hands rest
    # against her body, so this has to be driven by the arm rather than by
    # absolute position.
    # The hand. Her hands rest against her iliac crests, so the carpus and the
    # phalanges are separate components a hand's length past the end of the
    # forearm, and they have to be caught by distance from that end rather than
    # by any absolute position. The lateral test keeps ribs and spine out.
    if lateral > 70.0:
        for tip, arm_side in forearm_tips:
            if np.linalg.norm(np.array([z, y, x]) - tip) >= 170.0:
                continue
            # Near the wrist. A carpal, metacarpal or phalanx is short and small;
            # the distal end of the radius or ulna is longer and heavier, and it
            # belongs to the forearm even though it sits at the same place.
            if max(extent_mm) < 72.0 and voxels < 8000:
                return f'hand_{arm_side}'
            if max(extent_mm) < 200.0 and voxels < 25000:
                return f'forearm_{arm_side}'
            continue

    if z < marks['ankle']:
        return f'foot_{side}'
    if z < marks['knee'] - 15:
        small = max(extent_mm) < 70 and min(extent_mm) < 35
        if small and z > marks['knee'] - 110:
            return f'patella_{side}'
        return f'lower_leg_{side}'
    if z < marks['knee'] + 40:
        small = max(extent_mm) < 70 and min(extent_mm) < 35
        return f'patella_{side}' if small else f'lower_leg_{side}'
    if z < marks['hip'] and lateral > 30:
        return f'femur_{side}'
    return None
