"""Turn one OpenSkeleton subject into the rig's OBJ contract.

Input is the `.npz` produced by `02-export-subject.py`: 182 individually named
bones of a real woman, each with its own triangulation, already in one common
frame but in the pose she was scanned in -- lying down, arms folded across the
chest, feet pointing away from the table.

Output is `skeleton-female.obj`: metres, soles on y = 0, facing +z, one `o` group
per rig part, 1-based faces. Bone shapes are never scaled. Only two things move,
and both are rigid:

* the arms are brought down to an A-pose, because a rig whose rest pose has the
  forearms folded over the sternum cannot be animated;
* the feet are flattened, because a foot pointing sixty degrees down cannot put
  its sole on the floor.

    python3 03-import.py --npz /tmp/oskel/skelly-out.npz \
        --obj tools/skeleton-source/skeleton-female.obj \
        --parts tools/skeleton-source/skeleton-female.parts.json
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from scipy.spatial import cKDTree

# Bone name -> rig part. The dataset names every bone; the rig has 27 groups.
# `teeth` is absent on purpose: this subject's dentition is part of her cranium
# and jaw geometry, so there is nothing separate to name.
NAME_MAP = {}
for _side, _suffix in (('L', 'l'), ('R', 'r')):
    for _i in range(1, 13):
        NAME_MAP[f'Rib{_i}_{_side}'] = f'ribs_{_suffix}'
    NAME_MAP[f'Scapula_{_side}'] = f'scapula_{_suffix}'
    NAME_MAP[f'Clavicula_{_side}'] = f'clavicle_{_suffix}'
    NAME_MAP[f'Humerus_{_side}'] = f'humerus_{_suffix}'
    NAME_MAP[f'Radius_{_side}'] = f'forearm_{_suffix}'
    NAME_MAP[f'Ulna_{_side}'] = f'forearm_{_suffix}'
    NAME_MAP[f'Femur_{_side}'] = f'femur_{_suffix}'
    NAME_MAP[f'Patella_{_side}'] = f'patella_{_suffix}'
    NAME_MAP[f'Tibia_{_side}'] = f'lower_leg_{_suffix}'
    NAME_MAP[f'Fibula_{_side}'] = f'lower_leg_{_suffix}'
    NAME_MAP[f'Pelvis_{_side}'] = 'pelvis'
    for _bone in ('Cal', 'Tal', 'Nav', 'Cub', 'MedCun', 'InterCun', 'LatCun'):
        NAME_MAP[f'{_bone}_{_side}'] = f'foot_{_suffix}'
    for _i in range(1, 6):
        for _seg in ('FootMeta', 'FootProxP', 'FootMidP', 'FootDistP'):
            NAME_MAP[f'{_seg}{_i}_{_side}'] = f'foot_{_suffix}'
        for _seg in ('HandMeta', 'HandProxP', 'HandMidP', 'HandDistP'):
            NAME_MAP[f'{_seg}{_i}_{_side}'] = f'hand_{_suffix}'
    for _carpal in ('Scaphoid', 'Luna', 'Triquetrum', 'Pisiform', 'Trapezium',
                    'Trapezoideum', 'Capitatum', 'Hamata'):
        NAME_MAP[f'Hand{_carpal}_{_side}'] = f'hand_{_suffix}'
    NAME_MAP[f'FootSesaLat_{_side}'] = f'foot_{_suffix}'
    NAME_MAP[f'FootSesaMed_{_side}'] = f'foot_{_suffix}'

NAME_MAP['Head'] = 'skull'
NAME_MAP['Jaw'] = 'mandible'
NAME_MAP['Sacrum'] = 'pelvis'
NAME_MAP['Sternum'] = 'sternum'
for _i in range(1, 8):
    NAME_MAP[f'C{_i}'] = 'spine_cervical'
for _i in range(1, 13):
    NAME_MAP[f'T{_i}'] = 'spine_thoracic'
for _i in range(1, 6):
    NAME_MAP[f'L{_i}'] = 'spine_lumbar'

PART_ORDER = ['skull', 'mandible', 'teeth', 'spine_cervical', 'spine_thoracic',
              'spine_lumbar', 'ribs_l', 'ribs_r', 'sternum', 'scapula_l', 'scapula_r',
              'clavicle_l', 'clavicle_r', 'pelvis', 'humerus_l', 'humerus_r',
              'forearm_l', 'forearm_r', 'hand_l', 'hand_r', 'femur_l', 'femur_r',
              'patella_l', 'patella_r', 'lower_leg_l', 'lower_leg_r', 'foot_l', 'foot_r']

# Rest pose, in the dataset's own axes: +z is up, -y is front (her face direction).
A_POSE_SHOULDER = np.radians(14.0)   # upper arm away from the body
A_POSE_FOREARM = np.radians(8.0)     # forearm angle toward the midline
TOE_OUT = np.radians(7.0)
# Where the shoulder joint sits on a standing body, as a fraction of stature.
# Taken from the male, whose pose was checked and accepted: his glenohumeral
# joint lands at 0.8070 of his height.
SHOULDER_FRACTION = 0.8070


def unit(v):
    n = np.linalg.norm(v)
    return v / n if n > 1e-12 else v


def principal_axis(points):
    """Longest axis of a point cloud, and the cloud's extent along it."""
    centre = points.mean(0)
    axis = np.linalg.svd(points - centre, full_matrices=False)[2][0]
    projection = (points - centre) @ axis
    return centre, axis, projection


def ends(points):
    """The two tips of a bone along its own long axis."""
    centre, axis, projection = principal_axis(points)
    return centre + axis * projection.min(), centre + axis * projection.max(), axis


def near_cap(points, target, thickness=25.0):
    """Centroid of the bone's surface nearest `target` -- i.e. its joint face."""
    distances = np.linalg.norm(points - target, axis=1)
    closest = distances.min()
    cap = points[distances < closest + thickness]
    return cap.mean(0) if len(cap) else points.mean(0)


def rotation_between(source, target):
    """Smallest rotation carrying `source` onto `target`."""
    a, b = unit(source), unit(target)
    axis = np.cross(a, b)
    s, c = np.linalg.norm(axis), float(np.dot(a, b))
    if s < 1e-9:
        if c > 0:
            return np.eye(3)
        # Opposite vectors: a half turn about any perpendicular axis.
        axis = np.cross(a, [1.0, 0.0, 0.0])
        if np.linalg.norm(axis) < 1e-6:
            axis = np.cross(a, [0.0, 1.0, 0.0])
        axis = unit(axis)
        return 2.0 * np.outer(axis, axis) - np.eye(3)
    axis = axis / s
    angle = np.arctan2(s, c)
    k = np.array([[0, -axis[2], axis[1]], [axis[2], 0, -axis[0]], [-axis[1], axis[0], 0]])
    return np.eye(3) + np.sin(angle) * k + (1 - np.cos(angle)) * (k @ k)


def place(bones, names, pivot_now, pivot_then, direction_now, direction_then):
    """Rigidly move a set of bones: rotate about `pivot_now`, then translate."""
    turn = rotation_between(direction_now, direction_then)
    shift = pivot_then - turn @ pivot_now
    for name in names:
        bones[name] = (turn @ bones[name].T).T + shift


def seat_shoulder(bones, side, shoulder_z):
    """Drop a shrugged shoulder girdle to a neutral one, about the SC joint.

    She was scanned with her arms folded on her abdomen, and the shoulder that
    makes that pose comfortable is a shrug: her clavicles were sitting 43.7
    degrees above horizontal where a resting clavicle lies about ten, and
    everything hanging off them came up with them. Her acromion ended 60 mm
    higher than the same point on the male, which is what leaves her looking
    neckless beside him while her ribs sit at exactly his height.

    The girdle turns about the sternal end of the clavicle, so the clavicle stays
    on the manubrium and the scapula and the whole arm turn with it. The angle is
    solved for the height the shoulder joint should reach, not guessed, and the
    arm is aimed again afterwards -- so the A-pose is unchanged and only the
    height it hangs from moves.
    """
    scapula = bones[f'Scapula_{side}']
    humerus = bones[f'Humerus_{side}']
    distance = np.linalg.norm(humerus - scapula.mean(0), axis=1)
    joint = humerus[distance < distance.min() + 22.0].mean(0)

    clavicle = bones[f'Clavicula_{side}']
    sternum = bones['Sternum']
    hinge = clavicle[int(np.argmin(np.linalg.norm(clavicle - sternum.mean(0), axis=1)))]

    # The turn is about the front-back axis, so the shoulder stays in the
    # frontal plane. Rather than invert the trigonometry and argue about the
    # sign of a rotation, sweep the angle and keep the one that lands the joint
    # on the wanted height -- a body-length of shoulder is 100 mm of travel, so
    # a tenth of a degree is well inside a millimetre.
    offset = joint - hinge
    if np.hypot(offset[0], offset[2]) < 1.0:
        return
    best = None
    for degrees in np.arange(-90.0, 90.01, 0.1):
        turn = np.radians(degrees)
        cosine, sine = np.cos(turn), np.sin(turn)
        spin = np.array([[cosine, 0.0, sine], [0.0, 1.0, 0.0], [-sine, 0.0, cosine]])
        landed = float(hinge[2] + (spin @ offset)[2])
        if best is None or abs(landed - shoulder_z) < best[0]:
            best = (abs(landed - shoulder_z), spin)
    if best[0] > 1.0:
        return
    spin = best[1]
    names = [n for n in bones if n in (
        f'Clavicula_{side}', f'Scapula_{side}', f'Humerus_{side}',
        f'Radius_{side}', f'Ulna_{side}')] + [
        n for n in bones if NAME_MAP.get(n) == f'hand_{side.lower()}']
    for name in names:
        bones[name] = (spin @ (bones[name] - hinge).T).T + hinge


def repose_arm(bones, side):
    """Bring one arm from the folded scan pose down to an A-pose."""
    sign = 1.0 if side == 'L' else -1.0
    humerus = bones[f'Humerus_{side}']
    scapula = bones[f'Scapula_{side}']
    forearm_names = [f'Radius_{side}', f'Ulna_{side}']
    hand_names = [n for n in bones if NAME_MAP.get(n) == f'hand_{side.lower()}']

    shoulder_end, elbow_end, _ = ends(humerus)
    # Her humerus is not symmetric front-to-back, so decide which tip is the
    # shoulder by which one is nearer her scapula.
    if np.linalg.norm(elbow_end - scapula.mean(0)) < np.linalg.norm(shoulder_end - scapula.mean(0)):
        shoulder_end, elbow_end = elbow_end, shoulder_end
    shoulder = near_cap(humerus, shoulder_end)
    elbow_now = near_cap(np.concatenate([bones[n] for n in forearm_names]), elbow_end)
    wrist_end = near_cap(np.concatenate([bones[n] for n in forearm_names]),
                         elbow_now + (elbow_now - shoulder))
    fingers = np.concatenate([bones[n] for n in hand_names])
    wrist_now = near_cap(fingers, wrist_end)
    # The hand has to be turned as well as moved: in the scan she is holding her
    # hands across her body, so carrying them down without rotating leaves two
    # flat blades sticking out sideways.
    reach = np.linalg.norm(fingers - wrist_now, axis=1)
    hand_tip = fingers[np.argsort(reach)[-200:]].mean(0)
    hand_now_dir = unit(hand_tip - wrist_now)

    upper = np.linalg.norm(elbow_end - shoulder)
    fore = np.linalg.norm(wrist_now - elbow_now)

    upper_dir = np.array([sign * np.sin(A_POSE_SHOULDER), -0.06, -np.cos(A_POSE_SHOULDER)])
    fore_dir = np.array([-sign * np.sin(A_POSE_FOREARM), -0.10, -np.cos(A_POSE_FOREARM)])
    hand_dir = np.array([0.0, -0.10, -1.0])
    upper_dir, fore_dir, hand_dir = unit(upper_dir), unit(fore_dir), unit(hand_dir)

    elbow_new = shoulder + upper_dir * upper
    wrist_new = elbow_new + fore_dir * fore

    place(bones, [f'Humerus_{side}'], shoulder, shoulder,
          elbow_end - shoulder, elbow_new - shoulder)
    place(bones, forearm_names, elbow_now, elbow_new,
          wrist_now - elbow_now, wrist_new - elbow_new)
    place(bones, hand_names, wrist_now, wrist_new, hand_now_dir, hand_dir)
    return shoulder, elbow_new, wrist_new


def flat_foot(bones, side):
    """Stand one foot flat on the floor, and lay its toes on the floor.

    A skeleton stands on eleven points: the back of the calcaneus, the five
    metatarsal heads and the five toe tips, and on a level floor they are all at
    the same height. This foot was scanned hanging with the ankle pointed, so the
    ball was 77 mm below the heel, and once that is taken out the foot is also
    rolled -- her big toe came out 44 mm above her little toe. Rather than fit a
    frame by hand, the foot is turned about its ankle until those eleven points
    are as level as they can be; that number is what the eye checks when it
    decides whether a body is standing on the ground. Each toe is then turned
    about its own joint until its tip reaches the same floor, so the foot is
    standing on its toes rather than hovering above them.

    Turning about the ankle leaves the talus where the shin expects it, so no
    joint is opened by this.
    """
    sign = 1.0 if side == 'L' else -1.0
    names = [n for n in bones if NAME_MAP.get(n) == f'foot_{side.lower()}']
    ankle = bones[f'Tal_{side}'].mean(0)

    # Yaw first: point the foot the way she faces, with a little toe-out. Yaw
    # about the vertical axis cannot change how level the sole is.
    heel_centre = bones[f'Cal_{side}'].mean(0)
    ball_centre = np.mean([bones[f'FootMeta{i}_{side}'].mean(0) for i in range(1, 6)], axis=0)
    heading = ball_centre - heel_centre
    heading[2] = 0.0
    want = np.array([sign * np.sin(TOE_OUT), -np.cos(TOE_OUT), 0.0])
    yaw = np.arctan2(np.cross(unit(heading), want)[2], np.dot(unit(heading), want))
    c, sn = np.cos(yaw), np.sin(yaw)
    turn = np.array([[c, -sn, 0.0], [sn, c, 0.0], [0.0, 0.0, 1.0]])
    for name in names:
        bones[name] = (turn @ (bones[name] - ankle).T).T + ankle

    # Whole bones, thinned out, rather than a patch chosen by height: which
    # vertices are the lowest depends on the rotation being searched for, so a
    # patch picked before the search is a patch picked for the wrong pose.
    def thin(points, step):
        return points[::step] - ankle

    groups = [thin(bones[f'Cal_{side}'], 4)]
    groups += [thin(bones[f'FootMeta{i}_{side}'], 4) for i in range(1, 6)]
    groups += [thin(bones[f'FootDistP{i}_{side}'], 3) for i in range(1, 6)
               if f'FootDistP{i}_{side}' in bones]
    probes = np.concatenate(groups)

    def spread(pitch, roll):
        cp, sp, cr, sr = np.cos(pitch), np.sin(pitch), np.cos(roll), np.sin(roll)
        rx = np.array([[1.0, 0, 0], [0, cp, -sp], [0, sp, cp]])
        ry = np.array([[cr, 0, sr], [0, 1.0, 0], [-sr, 0, cr]])
        placed = probes @ (ry @ rx).T
        heights = []
        start = 0
        for group in groups:
            block = placed[start:start + len(group)]
            heights.append(block[:, 2].min())
            start += len(group)
        return float(np.std(heights)), ry @ rx

    best = None
    for pitch in np.radians(np.arange(-90.0, 91.0, 3.0)):
        for roll in np.radians(np.arange(-85.0, 86.0, 3.0)):
            score, turn = spread(pitch, roll)
            if best is None or score < best[0]:
                best = (score, turn, pitch, roll)
    for pitch in np.radians(np.arange(np.degrees(best[2]) - 3.0, np.degrees(best[2]) + 3.01, 0.25)):
        for roll in np.radians(np.arange(np.degrees(best[3]) - 3.0, np.degrees(best[3]) + 3.01, 0.25)):
            score, turn = spread(pitch, roll)
            if score < best[0]:
                best = (score, turn, pitch, roll)
    turn = best[1]
    for name in names:
        bones[name] = (turn @ (bones[name] - ankle).T).T + ankle

    # Toes down onto the floor: turn each one about its own joint until its tip
    # reaches the same floor the heel and the ball are standing on.
    floor = min(bones[n][:, 2].min() for n in names)
    for i in range(1, 6):
        tip, meta, prox = f'FootDistP{i}_{side}', f'FootMeta{i}_{side}', f'FootProxP{i}_{side}'
        if tip not in bones or prox not in bones:
            continue
        gap, nearest = cKDTree(bones[meta]).query(bones[prox])
        joint = 0.5 * (bones[prox][int(np.argmin(gap))]
                       + bones[meta][int(nearest[np.argmin(gap)])])
        lowest = bones[tip][int(np.argmin(bones[tip][:, 2]))]
        reach = float(np.linalg.norm(lowest - joint))
        high = float(lowest[2] - floor)
        if reach < 1.0 or high <= 0.5:
            continue
        chain = [prox]
        if f'FootMidP{i}_{side}' in bones:
            chain.append(f'FootMidP{i}_{side}')
        chain.append(tip)
        # Turn the toe's own axis down by the angle that brings its lowest
        # vertex to the floor. Using the direction to that vertex instead would
        # swing the toe downwards by its sole thickness on top of the drop.
        axis_now = unit(far_tip(bones[tip], joint) - joint)
        drop = np.arcsin(min(1.0, high / reach))
        flat = axis_now.copy()
        flat[2] = 0.0
        flat = unit(flat) if np.linalg.norm(flat) > 1e-6 else np.array([0.0, -1.0, 0.0])
        elevation = float(np.arcsin(max(-1.0, min(1.0, axis_now[2]))))
        wanted = unit(flat * np.cos(elevation - drop)
                      + np.array([0.0, 0.0, 1.0]) * np.sin(elevation - drop))
        place(bones, chain, joint, joint, axis_now, wanted)
    return want


def measure(bones, part, side):
    """Longest extent of one rig part along its own principal axis, in millimetres."""
    names = [n for n in bones if NAME_MAP.get(n) == f'{part}_{side.lower()}']
    _, _, projection = principal_axis(np.concatenate([bones[n] for n in names]))
    return float(projection.max() - projection.min())


def digit_chains(prefix, side):
    """The bone chains of one hand or foot: [metacarpal, proximal, (middle), distal]."""
    chains = []
    for i in range(1, 6):
        chain = [f'{prefix}Meta{i}_{side}', f'{prefix}ProxP{i}_{side}']
        if i >= 2:  # the thumb and the big toe have no middle phalanx
            chain.append(f'{prefix}MidP{i}_{side}')
        chain.append(f'{prefix}DistP{i}_{side}')
        chains.append(chain)
    return chains


def far_tip(points, joint):
    """The point of a bone furthest from `joint` along its own long axis."""
    centre, axis, projection = principal_axis(points)
    low, high = centre + axis * projection.min(), centre + axis * projection.max()
    return low if np.linalg.norm(low - joint) > np.linalg.norm(high - joint) else high


def straighten_digits(bones, prefix, side, along=None):
    """Uncurl one hand or foot, joint by joint, without moving any joint apart.

    She was scanned with her fingers folded across her abdomen and her toes
    curled, so every phalanx sits at an angle to the bone behind it. The bones
    themselves are the right size -- her metacarpal and metatarsal lengths match
    the reference tables to a millimetre -- but a curled chain measures short,
    which is why the hand came out at 144 mm against a 155 mm reference.

    Each joint is turned about its own contact point, so the surfaces stay
    touching and only the angle between two bones changes: the chain ends up
    collinear with its metacarpal or metatarsal. Nothing is scaled and the joint
    centres do not move, which is also the pose a rig wants.
    """
    for chain in digit_chains(prefix, side):
        for index in range(1, len(chain)):
            bone, parent = chain[index], chain[index - 1]
            if bone not in bones or parent not in bones:
                continue
            # The joint is where the two surfaces actually meet, which is the
            # closest pair of vertices -- a few millimetres, not the centroid of
            # a cap chosen by proximity, which the registration skews.
            gap, nearest = cKDTree(bones[parent]).query(bones[bone])
            here = bones[bone][int(np.argmin(gap))]
            there = bones[parent][int(nearest[np.argmin(gap)])]
            if np.linalg.norm(here - there) > 30.0:
                continue  # not a joint: these two bones are not in contact
            joint = 0.5 * (here + there)
            # The direction the chain is travelling: from the parent's far end
            # towards the joint. The child has to continue that line.
            parent_dir = (unit(along) if along is not None
                          else unit(joint - far_tip(bones[parent], joint)))
            child_dir = unit(far_tip(bones[bone], joint) - joint)
            angle = float(np.degrees(np.arccos(np.clip(np.dot(child_dir, parent_dir), -1, 1))))
            if angle < 1.0:
                continue
            place(bones, chain[index:], joint, joint, child_dir, parent_dir)


def mirror_right(bones, faces):
    """Rebuild her right side from her own left, mirrored.

    Her scan assembly places the two sides at different distances from the
    midline -- a femur sits 66 mm off its twin -- because every bone was
    registered on its own and dropped back into a template skeleton. A rig needs
    a symmetric rest pose, and mirroring her own left-hand bones uses her own
    anatomy rather than anyone else's. The faces are rewound so the mirrored
    surfaces still face outwards.
    """
    for name in list(bones):
        if not name.endswith('_L'):
            continue
        twin = f'{name[:-2]}_R'
        if twin not in bones:
            continue
        mirrored = bones[name].copy()
        mirrored[:, 0] = -mirrored[:, 0]
        bones[twin] = mirrored
        faces[twin] = faces[name][:, [0, 2, 1]]


def decimate(points, faces, target):
    import fast_simplification
    if len(faces) <= target:
        return points, faces
    reduction = 1.0 - target / len(faces)
    out_points, out_faces = fast_simplification.simplify(
        points.astype(np.float32), faces.astype(np.int32),
        target_reduction=float(np.clip(reduction, 0.0, 0.98)), agg=7)
    return out_points.astype(np.float64), out_faces.astype(np.int64)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--npz', required=True)
    parser.add_argument('--obj', required=True)
    parser.add_argument('--parts', required=True)
    parser.add_argument('--triangles', type=int, default=180000)
    parser.add_argument('--floor', type=int, default=44, help='triangles kept per bone')
    parser.add_argument('--no-repose', action='store_true',
                        help='keep the scan pose, for comparing against the source')
    args = parser.parse_args()

    store = np.load(args.npz)
    names = sorted(k[2:] for k in store if k.startswith('v_'))
    unwanted = [n for n in names if n not in NAME_MAP]
    if unwanted:
        sys.exit(f'unmapped bones: {unwanted}')

    bones = {n: store[f'v_{n}'].astype(np.float64) for n in names}
    # The dataset stores 1-based faces; the simplifier works 0-based. Normalise
    # once here and everything downstream can subtract nothing and add nothing
    # but the running vertex offset.
    faces = {n: store[f'f_{n}'].astype(np.int64) - 1 for n in names}
    for n in names:
        if faces[n].min() < 0 or faces[n].max() >= len(bones[n]):
            sys.exit(f'{n}: faces index outside its own vertices')
    if not args.no_repose:
        # Feet first: they set the floor, and the floor is what the rest of the
        # pose is measured against. The girdle is seated before the arms are
        # aimed, because the arm hangs from wherever the shoulder ends up.
        soles = {side: flat_foot(bones, side) for side in ('L', 'R')}
        floor = min(bones[n][:, 2].min() for n in bones
                    if NAME_MAP.get(n) in ('foot_l', 'foot_r'))
        stature = float(bones['Head'][:, 2].max() - floor)
        for side in ('L', 'R'):
            seat_shoulder(bones, side, floor + SHOULDER_FRACTION * stature)
        for side in ('L', 'R'):
            shoulder, elbow, wrist = repose_arm(bones, side)
            print(f'  arm {side}: shoulder {np.round(shoulder)} elbow {np.round(elbow)} '
                  f'wrist {np.round(wrist)}', flush=True)
            straighten_digits(bones, 'Hand', side)
            straighten_digits(bones, 'Foot', side, along=soles[side])
        mirror_right(bones, faces)
        for side in ('L', 'R'):
            print(f'  digits {side}: hand '
                  f'{measure(bones, "hand", side):.0f} mm, foot '
                  f'{measure(bones, "foot", side):.0f} mm', flush=True)

    # Game axes: +x stays her left, +z (up) becomes +y, and her front (-y) becomes +z.
    def game(v):
        v = v / 1000.0
        return np.stack([v[:, 0], v[:, 2], -v[:, 1]], 1)

    placed = {n: game(bones[n]) for n in names}
    everything = np.concatenate([placed[n] for n in names])
    lowest = everything[:, 1].min()
    # The plane the body is symmetric about. The mean of the pelvis is not it:
    # the sacrum is a single midline bone, so averaging it in with the two hip
    # bones drags the plane a few millimetres to one side and the body ships
    # off-centre. Taking the midpoint of each left/right pair cancels that.
    pairs = [(n, f'{n[:-2]}_R') for n in names
             if n.endswith('_L') and f'{n[:-2]}_R' in placed]
    midline = float(np.mean([0.5 * (placed[a][:, 0].mean() + placed[b][:, 0].mean())
                             for a, b in pairs]))
    # Depth is centred on the pelvis too. The scan sits wherever the scanner
    # was, so without this her feet land 180 mm behind the origin and she
    # stands a body's depth behind the male in the same room.
    hips = np.concatenate([placed[n] for n in names if NAME_MAP[n] == 'pelvis'])
    forward = float(hips[:, 2].mean())
    shift = np.array([-midline, -lowest, -forward])
    for n in names:
        placed[n] = placed[n] + shift

    groups = {}
    for n in names:
        groups.setdefault(NAME_MAP[n], []).append(n)
    total_source = sum(len(faces[n]) for n in names)
    report, lines, running = [], ['# Human skeleton segmented from CT, one object per bone group.',
                                  '# Source: OpenSkeleton (NMDID), CC BY 4.0, subject N186416.',
                                  '# Units: metres, soles on y = 0, facing +z.',
                                  '# See tools/skeleton-source/openskeleton/README.md.'], 0
    verts_total = faces_total = 0
    for part in PART_ORDER:
        if part not in groups:
            continue
        members = groups[part]
        own = sum(len(faces[n]) for n in members)
        budget = max(args.floor * len(members),
                     int(round(args.triangles * own / total_source)))
        points_out, faces_out, offset = [], [], 0
        for n in members:
            share = max(args.floor, int(round(budget * len(faces[n]) / max(1, own))))
            p, f = decimate(placed[n], faces[n], share)
            points_out.append(p)
            faces_out.append(f + offset)
            offset += len(p)
        points = np.concatenate(points_out)
        triangles = np.concatenate(faces_out)
        if triangles.min() < 0 or triangles.max() >= len(points):
            sys.exit(f'{part}: faces index outside its own vertices after decimation')
        lines.append(f'o {part}')
        lines.extend(f'v {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}' for p in points)
        # +1 because OBJ faces are 1-based while `triangles` is 0-based.
        lines.extend(f'f {running + int(t[0]) + 1} {running + int(t[1]) + 1} '
                     f'{running + int(t[2]) + 1}' for t in triangles)
        running += len(points)
        verts_total += len(points)
        faces_total += len(triangles)
        report.append({'name': part, 'vertices': int(len(points)),
                       'triangles': int(len(triangles)), 'bones': len(members),
                       'bbox': {'low': points.min(0).tolist(),
                                'high': points.max(0).tolist()}})

    Path(args.obj).parent.mkdir(parents=True, exist_ok=True)
    Path(args.obj).write_text('\n'.join(lines) + '\n')
    stature = float(everything[:, 1].max() - lowest)
    Path(args.parts).write_text(json.dumps(
        {'stature': stature, 'parts': report, 'bones': len(names),
         'subject': 'N186416', 'source': 'OpenSkeleton (NMDID) CC BY 4.0'}, indent=1))
    print(f'{len(report)} parts, {len(names)} bones, {verts_total} vertices, '
          f'{faces_total} triangles, stature {stature:.5f} m')


if __name__ == '__main__':
    main()
