#!/usr/bin/env python3
"""Pack MHCLO-fitted community hair for the offline Three.js renderer.

Usage: python3 tools/convert-hair.py male
       python3 tools/convert-hair.py female

Only Python's standard library is required. Geometry is fitted at runtime to the
same morph weights as the body, including MakeHuman's helper-hair vertices.
"""
import base64, json, struct, sys
from collections import defaultdict
from pathlib import Path

SEX = sys.argv[1] if len(sys.argv) > 1 else 'male'
if SEX not in ('male', 'female'):
    raise SystemExit('Expected male or female')
REPO = Path(__file__).resolve().parents[1]
ROOT = REPO / 'tools' / 'human-source'
HAIR = ROOT / 'hair'
SPECS = json.loads((REPO / 'tools' / 'human-morphs.json').read_text())
OUT = REPO / 'web' / 'js' / 'vendor' / f'human-hair-{SEX}-data.js'
STYLE_KEYS = {
    1: 'short01', 2: 'short02', 3: 'bob01', 4: 'grump',
    5: 'waves', 6: 'long-bangs', 7: 'short04', 8: 'short01',
    9: 'ponytail01', 10: 'braid01', 11: 'afro01', 12: 'long-waves',
    13: 'wavy-bob', 14: 'curly-bob', 15: 'fifties-updo',
}
KEEP = {'root', 'spine03', 'spine02', 'spine01', 'neck01', 'head', 'jaw'}
for side in ('L', 'R'):
    KEEP.update({f'{name}.{side}' for name in (
        'clavicle', 'shoulder01', 'upperarm01', 'upperarm02', 'lowerarm01',
        'lowerarm02', 'wrist', 'pelvis', 'upperleg01', 'upperleg02',
        'lowerleg01', 'lowerleg02', 'foot')})

def read_target(path):
    result = {}
    for line in path.open():
        parts = line.split()
        if len(parts) == 4 and parts[0].isdigit():
            result[int(parts[0])] = tuple(map(float, parts[1:4]))
    return result

def read_obj(path):
    vertices, uvs, faces = [], [], []
    for line in path.open():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'v':
            vertices.append(tuple(map(float, parts[1:4])))
        elif parts[0] == 'vt':
            uvs.append(tuple(map(float, parts[1:3])))
        elif parts[0] == 'f':
            face = []
            for item in parts[1:]:
                values = item.split('/')
                vertex = int(values[0]) - 1
                uv = int(values[1]) - 1 if len(values) > 1 and values[1] else 0
                face.append((vertex, uv))
            for index in range(1, len(face) - 1):
                faces.append((face[0], face[index], face[index + 1]))
    return vertices, uvs, faces

def read_mhclo(path):
    mappings, scales = [], {}
    obj_name = None
    inside = False
    for line in path.open():
        parts = line.split()
        if not parts:
            inside = False
            continue
        if parts[0].startswith('#'):
            continue
        if parts[0] == 'obj_file':
            obj_name = parts[1]
        if parts[0] in ('x_scale', 'y_scale', 'z_scale'):
            scales[parts[0]] = (int(parts[1]), int(parts[2]), float(parts[3]))
        if parts[0] == 'verts':
            inside = True
            continue
        if not inside or parts[0] in ('material', 'vertexboneweights_file'):
            continue
        if not parts[0].isdigit():
            inside = False
            continue
        if len(parts) == 1:
            vertex = int(parts[0])
            mappings.append(((vertex, vertex, vertex), (1.0, 0.0, 0.0), (0.0, 0.0, 0.0)))
        elif len(parts) >= 9:
            values = list(map(float, parts[:9]))
            mappings.append((tuple(int(value) for value in values[:3]),
                             tuple(values[3:6]), tuple(values[6:9])))
        else:
            raise ValueError(f'Bad MHCLO mapping in {path}: {line!r}')
    if not obj_name:
        raise ValueError(f'Missing obj_file in {path}')
    return obj_name, mappings, scales

vertices = []
for line in (ROOT / 'base.obj').open():
    parts = line.split()
    if parts and parts[0] == 'v':
        vertices.append(tuple(map(float, parts[1:4])))
base_macro = read_target(ROOT / f'caucasian-{SEX}-young.target')
for index, delta in base_macro.items():
    vertices[index] = tuple(vertices[index][axis] + delta[axis] for axis in range(3))

skeleton = json.load((ROOT / 'default.mhskel').open())
bone_defs = skeleton['bones']
weight_data = json.load((ROOT / 'default_weights.mhw').open())['weights']

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
bone_index = {name: index for index, name in enumerate(bone_names)}

assets = {}
required = set()
for key in sorted(set(STYLE_KEYS.values())):
    folder = HAIR / key
    clo_path = next(folder.glob('*.mhclo'))
    obj_name, mappings, scales = read_mhclo(clo_path)
    obj_vertices, uvs, faces = read_obj(folder / obj_name)
    if len(obj_vertices) != len(mappings):
        raise ValueError(f'{key}: {len(obj_vertices)} OBJ vertices but {len(mappings)} MHCLO rows')
    assets[key] = {'mappings': mappings, 'uvs': uvs, 'faces': faces, 'scales': scales}
    for refs, weights, offsets in mappings:
        required.update(refs)
    for scale in scales.values():
        required.update(scale[:2])
required = sorted(required)
auxiliary = {original: index for index, original in enumerate(required)}
print(SEX, 'hair styles', len(assets), 'auxiliary vertices', len(required))

body_vertices = set()
group = ''
for line in (ROOT / 'base.obj').open():
    parts = line.split()
    if not parts:
        continue
    if parts[0] == 'g':
        group = parts[1]
    elif parts[0] == 'f' and group == 'body':
        body_vertices.update(int(item.split('/')[0]) - 1 for item in parts[1:])
minimum_y = min(vertices[index][1] for index in body_vertices)
maximum_y = max(vertices[index][1] for index in body_vertices)
ground = -minimum_y
scale = 1.8 / (maximum_y - minimum_y)
neck_ids = skeleton['joints'][bone_defs['neck01']['head']]
regions_head = sum(vertices[index][1] for index in neck_ids) / len(neck_ids) - 0.07
cache = {}
def morph_delta(paths):
    combined = defaultdict(lambda: [0.0, 0.0, 0.0])
    for relative in paths:
        if relative.startswith('@'):
            group_name = relative[1:]
            macro = 'caucasian' if group_name == 'old' else group_name
            path = ROOT / 'targets' / 'macrodetails' / f"{macro}-{SEX}-{'old' if group_name == 'old' else 'young'}.target"
        else:
            path = ROOT / 'targets' / relative
        key = str(path)
        if key not in cache:
            cache[key] = read_target(path)
        target = cache[key]
        if relative.startswith('@'):
            differences = {index: tuple(target.get(index, (0, 0, 0))[axis] - base_macro.get(index, (0, 0, 0))[axis]
                                        for axis in range(3))
                           for index in set(target) | set(base_macro)}
            reference = [sum(differences.get(index, (0, 0, 0))[axis] for index in neck_ids) / len(neck_ids)
                         for axis in range(3)]
            for index, delta in differences.items():
                if vertices[index][1] >= regions_head - 0.20:
                    for axis in range(3):
                        combined[index][axis] += delta[axis] - reference[axis]
        else:
            for index, delta in target.items():
                for axis in range(3):
                    combined[index][axis] += delta[axis]
    return combined

sections, payload = [], bytearray()
def add_array(name, values, code):
    while len(payload) % 4:
        payload.append(0)
    offset = len(payload)
    payload.extend(struct.pack('<' + code * len(values), *values))
    sections.append({'name': name, 'offset': offset, 'count': len(values), 'type': code})

positions, skin_indices, skin_weights = [], [], []
for original in required:
    x, y, z = vertices[original]
    positions.extend((x * scale, (y + ground) * scale, z * scale))
    influences = sorted(collapsed.get(original, {'root': 1.0}).items(),
                        key=lambda item: item[1], reverse=True)[:4]
    total = sum(weight for _, weight in influences) or 1.0
    values = [max(0, round(weight / total * 255)) for _, weight in influences]
    values[0] += 255 - sum(values)
    while len(influences) < 4:
        influences.append(('root', 0))
        values.append(0)
    skin_indices.extend(bone_index[name] for name, _ in influences)
    skin_weights.extend(values)
add_array('basePosition', positions, 'f')
add_array('baseSkinIndex', skin_indices, 'B')
add_array('baseSkinWeight', skin_weights, 'B')

style_meta = {}
processed = set()
for number, key in STYLE_KEYS.items():
    asset = assets[key]
    prefix = f'hair_{key}'
    if key not in processed:
        processed.add(key)
        refs, bary, offsets, texcoords, indices = [], [], [], [], []
        unique, remap = {}, {}
        for face in asset['faces']:
            for corner in face:
                if corner not in unique:
                    unique[corner] = len(unique)
                    vertex, uv = corner
                    originals, weights, offset = asset['mappings'][vertex]
                    refs.extend(auxiliary[original] for original in originals)
                    bary.extend(weights)
                    offsets.extend(offset)
                    texcoords.extend(asset['uvs'][uv])
                remap[corner] = unique[corner]
        for face in asset['faces']:
            indices.extend(remap[corner] for corner in face)
        add_array(f'{prefix}_refs', refs, 'H')
        add_array(f'{prefix}_bary', bary, 'f')
        add_array(f'{prefix}_offsets', offsets, 'f')
        add_array(f'{prefix}_uv', texcoords, 'f')
        add_array(f'{prefix}_index', indices, 'H')
    scale_refs, scale_defaults = [], []
    for axis in ('x_scale', 'y_scale', 'z_scale'):
        if axis in asset['scales']:
            a, b, default = asset['scales'][axis]
            scale_refs.extend((auxiliary[a], auxiliary[b]))
            scale_defaults.append(default)
    style_meta[str(number)] = {
        'key': key, 'prefix': prefix, 'texture': f'assets/human/hair-{key}.png',
        'scaleRefs': scale_refs, 'scaleDefaults': scale_defaults,
        'receded': number == 8,
    }

morph_meta = {}
for spec in SPECS:
    entry = {}
    for direction in ('negative', 'positive'):
        if not spec[direction]:
            continue
        raw = morph_delta(spec[direction])
        indices, deltas = [], []
        for original in required:
            delta = raw.get(original)
            if not delta:
                continue
            quantized = [max(-32768, min(32767, round(value * scale * 100000))) for value in delta]
            if not any(quantized):
                continue
            indices.append(auxiliary[original])
            deltas.extend(quantized)
        index_name = f"morph_{spec['key']}_{direction}_index"
        delta_name = f"morph_{spec['key']}_{direction}_delta"
        add_array(index_name, indices, 'H')
        add_array(delta_name, deltas, 'h')
        entry[direction] = {'index': index_name, 'delta': delta_name}
    morph_meta[spec['key']] = entry

meta = {'scale': scale, 'ground': ground, 'morphQuant': 100000,
        'boneNames': bone_names, 'styles': style_meta, 'morphs': morph_meta, 'sections': sections}
meta_json = json.dumps(meta, separators=(',', ':')).encode()
padding = b'\0' * ((4 - (12 + len(meta_json)) % 4) % 4)
blob = struct.pack('<8sI', b'GTCHAIR1', len(meta_json)) + meta_json + padding + payload
encoded = base64.b64encode(blob).decode()
lines = [encoded[index:index + 160] for index in range(0, len(encoded), 160)]
content = '// Generated from community MakeHuman hair. See web/assets/human/LICENSE.md.\nconst PACK = [\n'
content += ''.join(f"  '{line}',\n" for line in lines)
content += '''].join('');
let cache = null;
export function hairData() {
  if (cache) return cache;
  const binary = atob(PACK);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; ++i) bytes[i] = binary.charCodeAt(i);
  const view = new DataView(bytes.buffer);
  const length = view.getUint32(8, true);
  const meta = JSON.parse(new TextDecoder().decode(bytes.subarray(12, 12 + length)));
  const base = (12 + length + 3) & ~3;
  const types = { f: Float32Array, B: Uint8Array, H: Uint16Array, h: Int16Array };
  const arrays = {};
  for (const section of meta.sections) {
    arrays[section.name] = new types[section.type](bytes.buffer, base + section.offset, section.count);
  }
  cache = { ...meta, ...arrays };
  return cache;
}
'''
OUT.write_text(content)
print('packed', len(blob), 'js', len(content), 'output', OUT)
