#!/usr/bin/env python3
"""Build a lazy-loaded runtime rig from the vendored CC0 MakeHuman assets.

Usage: python3 tools/convert-human.py male
       python3 tools/convert-human.py female
"""
import base64, json, math, struct, sys
from collections import defaultdict
from pathlib import Path

SEX = sys.argv[1] if len(sys.argv) > 1 else 'male'
if SEX not in ('male', 'female'):
    raise SystemExit('Expected male or female')
REPO = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parent / 'human-source'
MORPH_SPECS = json.loads((Path(__file__).resolve().parent / 'human-morphs.json').read_text())
OUT = REPO / 'web' / 'js' / 'vendor' / f'human-{SEX}-data.js'
SCALE = 0.108
GROUND = 8.1676
KEEP = {
    'root', 'spine03', 'spine02', 'spine01', 'neck01', 'head', 'jaw',
}
for side in ('L', 'R'):
    KEEP.update({f'{n}.{side}' for n in (
        'clavicle', 'shoulder01', 'upperarm01', 'upperarm02',
        'lowerarm01', 'lowerarm02', 'wrist', 'pelvis', 'upperleg01',
        'upperleg02', 'lowerleg01', 'lowerleg02', 'foot')})

def read_target(path):
    result = {}
    for line in path.open():
        parts = line.split()
        if len(parts) == 4 and parts[0].isdigit():
            result[int(parts[0])] = tuple(float(value) for value in parts[1:4])
    return result

vertices, uvs, faces = [], [], []
group = ''
for line in (ROOT / 'base.obj').open():
    parts = line.split()
    if not parts:
        continue
    if parts[0] == 'v':
        vertices.append(tuple(map(float, parts[1:4])))
    elif parts[0] == 'vt':
        uvs.append(tuple(map(float, parts[1:3])))
    elif parts[0] == 'g':
        group = parts[1]
    elif parts[0] == 'f' and group == 'body':
        faces.append([tuple(int(n) - 1 for n in item.split('/')[:2]) for item in parts[1:]])

base_macro = read_target(ROOT / f'caucasian-{SEX}-young.target')
for index, delta in base_macro.items():
    vertices[index] = tuple(vertices[index][axis] + delta[axis] for axis in range(3))
body_vertices = {vertex for face in faces for vertex, _ in face}
minimum_y = min(vertices[index][1] for index in body_vertices)
maximum_y = max(vertices[index][1] for index in body_vertices)
GROUND = -minimum_y
SCALE = 1.8 / (maximum_y - minimum_y)

skeleton = json.load((ROOT / 'default.mhskel').open())
weight_data = json.load((ROOT / 'default_weights.mhw').open())['weights']
bone_defs = skeleton['bones']
EYE_SOURCE = {side: skeleton['joints'][bone_defs[f'eye.{side}']['head']] for side in ('L', 'R')}
NECK_SOURCE = skeleton['joints'][bone_defs['neck01']['head']]

def joint(name):
    ids = skeleton['joints'][name]
    return tuple(sum(vertices[i][axis] for i in ids) / len(ids) for axis in range(3))

def bone_point(name, part='head'):
    return joint(bone_defs[name][part])

HEAD_SHIFT = bone_point('eye.L')[1] - 7.28415
BODY_SHIFT = bone_point('upperleg01.L')[1] - 0.4862
REGIONS = {
    'head': bone_point('neck01')[1] - 0.07,
    'collar': bone_point('neck01')[1] - 0.85,
    'pants': bone_point('root', 'tail')[1],
    'boots': bone_point('foot.L')[1] + 0.3978,
    'handX': abs(bone_point('wrist.L')[0]) - 0.03,
    'handY': bone_point('wrist.L')[1] + 0.75,
}

def kept_ancestor(name):
    while name is not None and name not in KEEP:
        name = bone_defs[name]['parent']
    return name or 'root'

collapsed = defaultdict(lambda: defaultdict(float))
for bone, entries in weight_data.items():
    target = kept_ancestor(bone)
    for vertex, weight in entries:
        collapsed[vertex][target] += weight

bone_names = []
def add_bone(name):
    if name in bone_names:
        return
    parent = kept_ancestor(bone_defs[name]['parent']) if bone_defs[name]['parent'] else None
    if parent and parent != name:
        add_bone(parent)
    bone_names.append(name)
for name in sorted(KEEP):
    add_bone(name)
bone_index = {name: i for i, name in enumerate(bone_names)}

bones = []
for name in bone_names:
    definition = bone_defs[name]
    parent = kept_ancestor(definition['parent']) if definition['parent'] else None
    head = joint(definition['head'])
    tail = joint(definition['tail'])
    bones.append({
        'name': name,
        'parent': bone_index[parent] if parent and parent != name else -1,
        'head': [round(v, 6) for v in head],
        'tail': [round(v, 6) for v in tail],
    })

unique = {}
remap = {}
for face in faces:
    for vertex, uv in face:
        key = (vertex, uv)
        if key not in unique:
            unique[key] = len(unique)
        remap[key] = unique[key]

positions, texcoords, skin_indices, skin_weights = [], [], [], []
raw_for_vertex = []
original_to_unique = defaultdict(list)
unweighted = 0
for (original, uv), new_index in sorted(unique.items(), key=lambda item: item[1]):
    x, y, z = vertices[original]
    raw_for_vertex.append((x, y, z))
    original_to_unique[original].append(new_index)
    positions.extend((x * SCALE, (y + GROUND) * SCALE, z * SCALE))
    texcoords.extend(uvs[uv])
    weights = collapsed.get(original)
    if not weights:
        weights = {'root': 1.0}
        unweighted += 1
    influences = sorted(weights.items(), key=lambda item: item[1], reverse=True)[:4]
    total = sum(weight for _, weight in influences) or 1.0
    vals = [max(0, round(weight / total * 255)) for _, weight in influences]
    vals[0] += 255 - sum(vals)
    while len(influences) < 4:
        influences.append(('root', 0))
        vals.append(0)
    skin_indices.extend(bone_index[name] for name, _ in influences)
    skin_weights.extend(vals)

def centroid(face):
    ids = [v for v, _ in face]
    return tuple(sum(vertices[i][axis] for i in ids) / len(ids) for axis in range(3))

def face_weights(face):
    result = defaultdict(float)
    for vertex, _ in face:
        for name, weight in collapsed.get(vertex, {'root': 1}).items():
            result[name] += weight / len(face)
    return result

def category(face):
    x, y, z = centroid(face)
    weights = face_weights(face)
    neck = weights.get('neck01', 0) + weights.get('head', 0) + weights.get('jaw', 0)
    if y > REGIONS['head'] or (y > REGIONS['collar'] and neck > 0.42):
        return 'head'
    hand = weights.get('wrist.L', 0) + weights.get('wrist.R', 0)
    if hand > 0.53 or (abs(x) > REGIONS['handX'] and y < REGIONS['handY']):
        return 'skin'
    if y < REGIONS['boots']:
        return 'boots'
    leg = sum(weights.get(f'{part}.{side}', 0) for side in ('L', 'R')
              for part in ('upperleg01', 'upperleg02', 'lowerleg01', 'lowerleg02', 'pelvis'))
    if y < REGIONS['pants'] or leg > 0.43:
        return 'pants'
    lower = sum(weights.get(f'{part}.{side}', 0) for side in ('L', 'R')
                for part in ('lowerarm01', 'lowerarm02'))
    if lower > 0.42:
        return 'sleeve'
    return 'shirt'

head_indices, body_lists = [], {name: [] for name in ('skin', 'shirt', 'sleeve', 'pants', 'boots', 'torso')}
hair_lists = [[] for _ in range(8)]
coat_indices = []
triangle_raw = []
for face in faces:
    kind = category(face)
    triangles = [(face[0], face[i], face[i + 1]) for i in range(1, len(face) - 1)]
    for triangle in triangles:
        indices = [remap[item] for item in triangle]
        if kind == 'head':
            head_indices.extend(indices)
            x, y, z = centroid(triangle)
            receded = 7.72 + HEAD_SHIFT + 0.12 * min(1, abs(x) / 0.8) + 0.06 * max(0, z - 1.0)
            swept = 7.57 + HEAD_SHIFT + 0.15 * max(0, x) + 0.06 * max(0, z - 1.0)
            masks = (
                y > 7.62 + HEAD_SHIFT + 0.10 * abs(x) or (z < 0.38 and y > 6.98 + HEAD_SHIFT) or (abs(x) > 0.77 and z < 0.60 and y > 7.22 + HEAD_SHIFT),
                y > receded or (z < 0.42 and y > 6.82 + HEAD_SHIFT) or (abs(x) > 0.76 and z < 0.55 and y > 7.02 + HEAD_SHIFT),
                y > 7.48 + HEAD_SHIFT + 0.10 * abs(x) or (z < 0.64 and y > 6.10 + HEAD_SHIFT) or (abs(x) > 0.70 and z < 0.93 and y > 6.35 + HEAD_SHIFT),
                y > swept or (z < 0.35 and y > 6.88 + HEAD_SHIFT) or (x < -0.55 and z < 0.85 and y > 7.04 + HEAD_SHIFT),
                y > 7.50 + HEAD_SHIFT + 0.10 * abs(x) or (z < 0.62 and y > 6.45 + HEAD_SHIFT) or (abs(x) > 0.68 and z < 0.90 and y > 6.55 + HEAD_SHIFT),
                y > 7.48 + HEAD_SHIFT + 0.10 * abs(x) or (z < 0.70 and y > 5.75 + HEAD_SHIFT) or (abs(x) > 0.72 and z < 0.95 and y > 5.95 + HEAD_SHIFT),
                y > 7.78 + HEAD_SHIFT + 0.04 * abs(x) or (z < 0.32 and y > 7.15 + HEAD_SHIFT) or (abs(x) > 0.78 and z < 0.42 and y > 7.35 + HEAD_SHIFT),
                y > 7.96 + HEAD_SHIFT + 0.05 * abs(x) or (z < 0.30 and y > 6.95 + HEAD_SHIFT) or (abs(x) > 0.80 and z < 0.48 and y > 7.25 + HEAD_SHIFT),
            )
            for style, enabled in enumerate(masks):
                if enabled:
                    hair_lists[style].extend(indices)
        else:
            if kind == 'shirt':
                x, y, z = centroid(triangle)
                if abs(x) < 1.45 and y > 2.4 + BODY_SHIFT:
                    kind = 'torso'
            body_lists[kind].extend(indices)
            if kind in ('shirt', 'sleeve', 'torso'):
                x, y, z = centroid(triangle)
                open_front = kind != 'sleeve' and abs(x) < 0.50 and z > 0.75 and 2.1 + BODY_SHIFT < y < 5.55 + BODY_SHIFT
                if not open_front:
                    coat_indices.extend(indices)

body_indices = []
body_groups = []
for material_index, kind in enumerate(('skin', 'shirt', 'sleeve', 'pants', 'boots', 'torso')):
    start = len(body_indices)
    body_indices.extend(body_lists[kind])
    body_groups.append({'start': start, 'count': len(body_indices) - start, 'materialIndex': material_index})

head_vertices = [i for i, (x, y, z) in enumerate(raw_for_vertex) if y > 5.75 + HEAD_SHIFT]
def nearest(target):
    return min(head_vertices, key=lambda i: sum((raw_for_vertex[i][a] - target[a]) ** 2 for a in range(3)))

def landmark(target):
    index = nearest(target)
    return [round(texcoords[index * 2], 6), round(texcoords[index * 2 + 1], 6)]

def bone_center(name, tail=False):
    definition = bone_defs[name]
    return joint(definition['tail' if tail else 'head'])

eye_raw = {side: bone_center(f'eye.{side}') for side in ('L', 'R')}
landmarks = {
    'eyeL': landmark(bone_center('eye.L', True)),
    'eyeR': landmark(bone_center('eye.R', True)),
    'browInnerL': landmark((0.16, eye_raw['L'][1] + 0.46, eye_raw['L'][2] + 0.22)),
    'browOuterL': landmark((0.55, eye_raw['L'][1] + 0.40, eye_raw['L'][2] + 0.05)),
    'browInnerR': landmark((-0.16, eye_raw['R'][1] + 0.46, eye_raw['R'][2] + 0.22)),
    'browOuterR': landmark((-0.55, eye_raw['R'][1] + 0.40, eye_raw['R'][2] + 0.05)),
    'mouth': landmark(bone_center('oris05')),
    'mouthL': landmark((0.34, 6.70 + HEAD_SHIFT, 1.49)),
    'mouthR': landmark((-0.34, 6.70 + HEAD_SHIFT, 1.49)),
    'chin': landmark(bone_center('jaw', True)),
    'jawL': landmark((0.53, 6.30 + HEAD_SHIFT, 0.98)),
    'jawR': landmark((-0.53, 6.30 + HEAD_SHIFT, 0.98)),
    'cheekL': landmark((0.51, 6.96 + HEAD_SHIFT, 1.24)),
    'cheekR': landmark((-0.51, 6.96 + HEAD_SHIFT, 1.24)),
    'forehead': landmark((0, 7.69 + HEAD_SHIFT, 1.22)),
}
nose_candidates = [i for i in head_vertices if abs(raw_for_vertex[i][0]) < 0.18 and 6.78 + HEAD_SHIFT < raw_for_vertex[i][1] < 7.18 + HEAD_SHIFT]
nose_index = max(nose_candidates, key=lambda i: raw_for_vertex[i][2])
landmarks['nose'] = [round(texcoords[nose_index * 2], 6), round(texcoords[nose_index * 2 + 1], 6)]
landmarks['noseL'] = landmark((0.13, 6.85 + HEAD_SHIFT, 1.47))
landmarks['noseR'] = landmark((-0.13, 6.85 + HEAD_SHIFT, 1.47))

def transformed(point):
    return [round(point[0] * SCALE, 6), round((point[1] + GROUND) * SCALE, 6), round(point[2] * SCALE, 6)]

def eye_anchor(side):
    center = bone_center(f'eye.{side}')
    index = nearest(center)
    base = transformed(raw_for_vertex[index])
    exact = transformed(center)
    return {'index': index, 'offset': [round(exact[axis] - base[axis], 6) for axis in range(3)]}

target_cache = {}
def morph_delta(paths):
    combined = defaultdict(lambda: [0.0, 0.0, 0.0])
    for relative in paths:
        if relative.startswith('@'):
            group = relative[1:]
            macro_group = 'caucasian' if group == 'old' else group
            filename = f'{macro_group}-{SEX}-' + ('old.target' if group == 'old' else 'young.target')
            path = ROOT / 'targets' / 'macrodetails' / filename
        else:
            path = ROOT / 'targets' / relative
        key = str(path)
        if key not in target_cache:
            target_cache[key] = read_target(path)
        target = target_cache[key]
        if relative.startswith('@'):
            difference = {index: tuple(target.get(index, (0, 0, 0))[axis] - base_macro.get(index, (0, 0, 0))[axis]
                                       for axis in range(3)) for index in set(target) | set(base_macro)}
            reference = [sum(difference.get(index, (0, 0, 0))[axis] for index in NECK_SOURCE) / len(NECK_SOURCE)
                         for axis in range(3)]
            for index, delta in difference.items():
                if vertices[index][1] < REGIONS['head'] - 0.20:
                    continue
                for axis in range(3):
                    combined[index][axis] += delta[axis] - reference[axis]
        else:
            for index, delta in target.items():
                for axis in range(3):
                    combined[index][axis] += delta[axis]
    return combined

meta = {
    'scale': SCALE, 'ground': GROUND, 'bones': bones, 'bodyGroups': body_groups,
    'landmarks': landmarks, 'regions': REGIONS, 'headShift': HEAD_SHIFT, 'bodyShift': BODY_SHIFT,
    'eyes': {side: transformed(bone_center(f'eye.{side}')) for side in ('L', 'R')},
    'eyeAnchors': {side: eye_anchor(side) for side in ('L', 'R')},
    'morphOrder': [spec['key'] for spec in MORPH_SPECS], 'morphQuant': 100000,
    'shoes': {side: transformed((bone_center(f'foot.{side}')[0], bone_center(f'foot.{side}')[1] - 0.272,
                                 bone_center(f'foot.{side}')[2] + 0.789)) for side in ('L', 'R')},
    'unweighted': unweighted,
}
sections = []
payload = bytearray()
def add_array(name, values, code):
    while len(payload) % 4:
        payload.append(0)
    offset = len(payload)
    payload.extend(struct.pack('<' + code * len(values), *values))
    sections.append({'name': name, 'offset': offset, 'count': len(values), 'type': code})

add_array('position', positions, 'f')
add_array('uv', texcoords, 'f')
add_array('skinIndex', skin_indices, 'B')
add_array('skinWeight', skin_weights, 'B')
for name, values in [('headIndex', head_indices), ('bodyIndex', body_indices), ('coatIndex', coat_indices)]:
    add_array(name, values, 'H')
for i, values in enumerate(hair_lists, 1):
    add_array(f'hair{i}Index', values, 'H')
morph_meta = {}
for spec in MORPH_SPECS:
    entry = {}
    for direction in ('negative', 'positive'):
        if not spec[direction]:
            continue
        mapped = {}
        raw_delta = morph_delta(spec[direction])
        for original, delta in raw_delta.items():
            quantized = [max(-32768, min(32767, round(value * SCALE * 100000))) for value in delta]
            if not any(quantized):
                continue
            for new_index in original_to_unique.get(original, []):
                mapped[new_index] = quantized
        indices, deltas = [], []
        for index in sorted(mapped):
            indices.append(index)
            deltas.extend(mapped[index])
        index_name = f"morph_{spec['key']}_{direction}_index"
        delta_name = f"morph_{spec['key']}_{direction}_delta"
        add_array(index_name, indices, 'H')
        add_array(delta_name, deltas, 'h')
        eye_deltas = []
        for side in ('L', 'R'):
            sources = EYE_SOURCE[side]
            for axis in range(3):
                value = sum(raw_delta.get(index, (0, 0, 0))[axis] for index in sources) / len(sources)
                eye_deltas.append(max(-32768, min(32767, round(value * SCALE * 100000))))
        eye_name = f"morph_{spec['key']}_{direction}_eyes"
        add_array(eye_name, eye_deltas, 'h')
        entry[direction] = {'index': index_name, 'delta': delta_name, 'eyes': eye_name}
    morph_meta[spec['key']] = entry
meta['morphs'] = morph_meta
meta['sections'] = sections
meta_json = json.dumps(meta, separators=(',', ':')).encode()
padding = b'\0' * ((4 - (12 + len(meta_json)) % 4) % 4)
blob = struct.pack('<8sI', b'GTCUMAN1', len(meta_json)) + meta_json + padding + payload
encoded = base64.b64encode(blob).decode()
lines = [encoded[i:i + 160] for i in range(0, len(encoded), 160)]
content = '''// Generated from MakeHuman CC0 base mesh, default skeleton and symmetric weights.\n// Source and provenance: web/assets/human/LICENSE.md. Do not edit by hand.\nconst PACK = [\n'''
content += ''.join(f"  '{line}',\n" for line in lines)
content += '''].join('');\nlet cache = null;\nexport function humanData() {\n  if (cache) return cache;\n  const binary = atob(PACK);\n  const bytes = new Uint8Array(binary.length);\n  for (let i = 0; i < binary.length; ++i) bytes[i] = binary.charCodeAt(i);\n  const view = new DataView(bytes.buffer);\n  const length = view.getUint32(8, true);\n  const meta = JSON.parse(new TextDecoder().decode(bytes.subarray(12, 12 + length)));\n  const base = (12 + length + 3) & ~3;\n  const types = { f: Float32Array, B: Uint8Array, H: Uint16Array, h: Int16Array };\n  const arrays = {};\n  for (const section of meta.sections) {\n    arrays[section.name] = new types[section.type](bytes.buffer, base + section.offset, section.count);\n  }\n  cache = { ...meta, ...arrays };\n  return cache;\n}\n'''
OUT.write_text(content)
public_specs = [{key: spec[key] for key in ('key', 'label', 'tab', 'min', 'max')} for spec in MORPH_SPECS]
morph_module = '// Generated by tools/convert-human.py from tools/human-morphs.json.\n'
morph_module += 'export const MORPH_DEFS = ' + json.dumps(public_specs, indent=2) + ';\n'
(REPO / 'web' / 'js' / 'morphs.js').write_text(morph_module)
print('vertices', len(unique), 'triangles', len(head_indices + body_indices) // 3,
      'head', len(head_indices) // 3, 'bones', len(bones), 'unweighted', unweighted,
      'morphs', len(morph_meta), 'packed', len(blob), 'js', len(content))
print('groups', body_groups)
print('landmarks', json.dumps(landmarks, indent=2))
