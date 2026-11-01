"""Movement classes for every relationship in 04-line-graph's anatomical table.

The data distinguishes descriptive biological motion from the simplified V-view
geometry; the grouped shapes do not independently move their member bones.
See JOINT_MOBILITY.md for definitions, reference ranges and source citations.
"""
import re


def mobility(a, b, kind):
    if kind == 'cartilage': return 'costal_cartilage'
    a, b = sorted((a.removesuffix('_L').removesuffix('_R'), b.removesuffix('_L').removesuffix('_R')))
    pair = {a, b}
    if pair == {'Head', 'Jaw'}: return 'temporomandibular'
    if pair == {'Head', 'C1'}: return 'atlanto_occipital'
    if pair == {'C1', 'C2'}: return 'atlanto_axial'
    spine = lambda x: re.fullmatch(r'[CTL]\d+', x) is not None
    if (spine(a) and spine(b)) or pair == {'L5', 'Sacrum'}: return 'intervertebral'
    if 'Sacrum' in pair and 'Pelvis' in pair: return 'sacroiliac'
    if a == b == 'Pelvis': return 'pubic_symphysis'
    if pair == {'Pelvis', 'Femur'}: return 'hip_ball_socket'
    if pair == {'Femur', 'Patella'}: return 'patellofemoral_glide'
    if pair == {'Femur', 'Tibia'}: return 'knee_modified_hinge'
    if pair == {'Fibula', 'Tibia'}: return 'tibiofibular'
    if 'Tal' in pair and (pair & {'Tibia', 'Fibula'}): return 'ankle_hinge'
    if 'Sternum' in pair and 'Clavicula' in pair: return 'sternoclavicular'
    if pair == {'Clavicula', 'Scapula'}: return 'acromioclavicular'
    if pair == {'Humerus', 'Scapula'}: return 'shoulder_ball_socket'
    if 'Humerus' in pair and (pair & {'Radius', 'Ulna'}): return 'elbow_hinge'
    if pair == {'Radius', 'Ulna'}: return 'radioulnar_pivot'
    if 'Radius' in pair and (pair & {'HandScaphoid', 'HandLuna'}): return 'wrist_condyloid'
    if any(re.fullmatch(r'T\d+', x) for x in pair) and any(x.startswith('Rib') for x in pair): return 'costovertebral'
    if any(x.startswith('FootSesa') for x in pair): return 'sesamoid_glide'
    if pair == {'HandTrapezium', 'HandMeta1'}: return 'thumb_cmc_saddle'
    if any(re.fullmatch(r'HandMeta[1-5]', x) for x in pair) and any(re.fullmatch(r'HandProxP[1-5]', x) for x in pair):
        return 'thumb_mcp' if 'HandMeta1' in pair else 'finger_mcp_condyloid'
    if any(re.fullmatch(r'FootMeta[1-5]', x) for x in pair) and any(re.fullmatch(r'FootProxP[1-5]', x) for x in pair):
        return 'great_toe_mtp' if 'FootMeta1' in pair else 'lesser_toe_mtp'
    for prefix in ('Hand', 'Foot'):
        if all(re.fullmatch(prefix+r'(?:Prox|Mid|Dist)P[1-5]', x) for x in pair):
            if prefix == 'Hand':
                return 'thumb_ip_hinge' if any(x.endswith('P1') for x in pair) else ('finger_pip_hinge' if any('ProxP' in x for x in pair) else 'finger_dip_hinge')
            return 'toe_ip_hinge'
    if all(x.startswith('Hand') for x in pair): return 'carpal_glide'
    foot = {'Tal', 'Cal', 'Nav', 'Cub', 'MedCun', 'InterCun', 'LatCun'}
    if all(x in foot or x.startswith('FootMeta') for x in pair): return 'tarsal_glide'
    raise ValueError(f'unclassified anatomical relationship: {a} / {b} ({kind})')
