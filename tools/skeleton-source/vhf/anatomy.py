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

SIDE_BY_X = 243.0        # midline column of the Visible Human Female, in mm


def side_of(x_mm):
    return 'l' if x_mm < SIDE_BY_X else 'r'


def landmarks(components, spacing):
    """Find the knee, hip, shoulder and ankle heights in this subject.

    The fused limb masses make the joints visible as the ends of long
    components: the thigh mass ends where the shin mass starts, and that seam is
    the knee.
    """
    masses = []
    for row in components.values():
        lo, hi = np.array(row['lo']), np.array(row['hi'])
        extent = (hi - lo + 1) * spacing
        centre = np.array(row['centroid']) * spacing
        if row['voxels'] > 150000 and extent[0] > 250:
            masses.append((centre, extent, row['voxels']))
    tops = sorted(float(c[0] + e[0] / 2) for c, e, _ in masses)
    bottoms = sorted(float(c[0] - e[0] / 2) for c, e, _ in masses)
    knee = float(np.median([t for t in tops if 350 < t < 650]))
    ankle = float(np.median([b for b in bottoms if 100 < b < 300]))
    hip = float(np.median([t for t in tops if 850 < t < 1000]))
    return {'knee': knee, 'ankle': ankle, 'hip': hip}


def limb_part(centre_mm, extent_mm, voxels, marks, forearm_tips):
    """Return the rig part for a piece, or None if it belongs to the trunk.

    `centre_mm` and `extent_mm` are in the scan frame: z up, x lateral, y depth.
    """
    z, y, x = float(centre_mm[0]), float(centre_mm[1]), float(centre_mm[2])
    side = side_of(x)
    lateral = abs(x - SIDE_BY_X)

    # The hand: whatever sits just beyond the end of a forearm. Her hands rest
    # against her body, so this has to be driven by the arm rather than by
    # absolute position.
    # Hand bones are small and sit just past the wrist; a piece as big as a long
    # bone is the arm itself and must not be collected here.
    for tip, arm_side in forearm_tips:
        if (np.linalg.norm(np.array([z, y, x]) - tip) < 60.0
                and max(extent_mm) < 120.0):
            return f'hand_{arm_side}'

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
