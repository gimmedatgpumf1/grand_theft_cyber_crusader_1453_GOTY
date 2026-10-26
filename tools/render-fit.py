#!/usr/bin/env python3
"""Fit existing morph sliders against landmarks measured from actual renders.

Development only: references and detected landmarks never enter browser assets.
Usage: PYTHONPATH=.cache/python python3 tools/render-fit.py [preset ...]
"""
import atexit, json, math, os, shutil, subprocess, sys
from pathlib import Path

os.environ.setdefault('GLOG_minloglevel', '3')
import cv2
import mediapipe as mp
import numpy as np
from scipy.optimize import lsq_linear
from scipy.spatial import cKDTree

REPO = Path(__file__).resolve().parents[1]
WORK = REPO / 'tools/human-source/portrait-fitting'
ROOT = REPO / 'tools/human-source'
BUILD = REPO / 'build'
SPECS = json.loads((REPO / 'tools/human-morphs.json').read_text())
REFS = json.loads((WORK / 'reference-landmarks.json').read_text())
CANON = np.array([list(map(float, line.split()[1:4])) for line in open(WORK / 'canonical_face_model.obj') if line.startswith('v ')])
KEY = {1,4,10,13,14,33,133,159,145,362,263,386,374,61,291,152,234,454,70,300,63,293,105,334,66,296,107,336,55,285,65,295,52,282,53,283,46,276,
       48,115,220,45,64,98,327,344,278,440,275,50,280,101,330,205,425,17,0,37,39,40,267,269,270,321,405,314,84,181,91,146}
FEATURE = {1,4,10,13,14,33,133,159,145,362,263,386,374,61,291,17,0,61,291,152}
FIT = sorted(set(range(0, 468, 6)) | KEY)
COORDINATE = [
    'headWidth', 'headHeight', 'faceShape', 'faceFullness', 'forehead', 'cheekBones', 'cheekFullness',
    'cheekHeight', 'jawWidth', 'jawHeight', 'chinWidth', 'chinHeight', 'chinProjection', 'chinBones',
    'noseWidth', 'noseLength', 'noseVertical', 'noseProjection', 'noseBridge', 'noseTip', 'nostrils',
    'eyeSize', 'eyeHeight', 'eyeSpacing', 'eyeDepth', 'eyeTilt', 'browHeight', 'browAngle',
    'mouthWidth', 'mouthVertical', 'smile', 'lipHeight', 'upperLip', 'lowerLip',
]
EXCLUDE = {'neckWidth','shoulders','chest','torsoWidth','stomach','muscles','armWidth','legWidth','african','asian'}
SELECT = [i for i, spec in enumerate(SPECS) if spec['key'] not in EXCLUDE and not spec['key'].endswith('Sculpt')]


def read_target(path):
    result = {}
    for line in open(path):
        parts = line.split()
        if len(parts) == 4 and parts[0].isdigit():
            result[int(parts[0])] = np.array(list(map(float, parts[1:4])))
    return result


def closest_triangle(point, a, b, c):
    """Closest point and barycentric weights, from Real-Time Collision Detection."""
    ab, ac, ap = b - a, c - a, point - a
    d1, d2 = ab @ ap, ac @ ap
    if d1 <= 0 and d2 <= 0:
        return a, np.array([1., 0., 0.])
    bp = point - b
    d3, d4 = ab @ bp, ac @ bp
    if d3 >= 0 and d4 <= d3:
        return b, np.array([0., 1., 0.])
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3)
        return a + v * ab, np.array([1 - v, v, 0.])
    cp = point - c
    d5, d6 = ab @ cp, ac @ cp
    if d6 >= 0 and d5 <= d6:
        return c, np.array([0., 0., 1.])
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2 / (d2 - d6)
        return a + w * ac, np.array([1 - w, 0., w])
    va = d3 * d6 - d5 * d4
    if va <= 0 and d4 - d3 >= 0 and d5 - d6 >= 0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return b + w * (c - b), np.array([0., 1 - w, w])
    denominator = va + vb + vc
    v, w = vb / denominator, vc / denominator
    return a + ab * v + ac * w, np.array([1 - v - w, v, w])


class Mesh:
    def __init__(self, sex):
        self.sex = sex
        self.vertices = []
        body_ids = set()
        body_faces = []
        group = ''
        for line in open(ROOT / 'base.obj'):
            parts = line.split()
            if not parts:
                continue
            if parts[0] == 'v':
                self.vertices.append(np.array(list(map(float, parts[1:4]))))
            elif parts[0] == 'g':
                group = parts[1]
            elif parts[0] == 'f' and group == 'body':
                face = [int(item.split('/')[0]) - 1 for item in parts[1:]]
                body_ids.update(face)
                body_faces.extend([[face[0], face[index], face[index + 1]] for index in range(1, len(face) - 1)])
        self.vertices = np.array(self.vertices)
        self.base_macro = read_target(ROOT / f'caucasian-{sex}-young.target')
        for index, delta in self.base_macro.items():
            self.vertices[index] += delta
        self.skeleton = json.load(open(ROOT / 'default.mhskel'))
        self.bones = self.skeleton['bones']
        self.scale = 1.8 / (max(self.vertices[list(body_ids), 1]) - min(self.vertices[list(body_ids), 1]))
        self.ground = -min(self.vertices[list(body_ids), 1])
        neck = self.joint(self.bones['neck01']['head'])
        self.regions_head = neck[1] - .07
        self.neck_source = self.skeleton['joints'][self.bones['neck01']['head']]
        self.head_ids = np.array(sorted(index for index in body_ids if self.vertices[index, 1] > self.regions_head - .25))
        head_set = set(self.head_ids)
        self.head_faces = np.array([face for face in body_faces if all(index in head_set for index in face)], dtype=int)
        self.head_triangles = self.vertices[self.head_faces]
        self.head_tree = cKDTree(self.head_triangles.mean(axis=1))
        self.surface_map = None
        eye_l = self.joint(self.bones['eye.L']['head'])
        eye_r = self.joint(self.bones['eye.R']['head'])
        source_center = (CANON[33] + CANON[263]) / 2
        aligned = CANON * self.scale + (eye_l + eye_r) / 2 - self.scale * source_center
        distance, nearest = cKDTree(self.vertices[self.head_ids]).query(aligned)
        self.canonical_original = self.head_ids[nearest]
        self.map_distance = distance
        self.cache = {}

    def joint(self, name):
        return self.vertices[self.skeleton['joints'][name]].mean(axis=0)

    def raw_morph(self, paths):
        combined = {}
        for relative in paths:
            if relative.startswith('@'):
                group = relative[1:]
                macro = 'caucasian' if group == 'old' else group
                filename = f"{macro}-{self.sex}-" + ('old.target' if group == 'old' else 'young.target')
                path = ROOT / 'targets' / 'macrodetails' / filename
            else:
                path = ROOT / 'targets' / relative.replace('{sex}', self.sex)
            key = str(path)
            if key not in self.cache:
                self.cache[key] = read_target(path)
            target = self.cache[key]
            if relative.startswith('@'):
                difference = {i: target.get(i, np.zeros(3)) - self.base_macro.get(i, np.zeros(3))
                              for i in set(target) | set(self.base_macro)}
                reference = np.array([difference.get(i, np.zeros(3)) for i in self.neck_source]).mean(axis=0)
                for index, delta in difference.items():
                    if self.vertices[index, 1] >= self.regions_head - .20:
                        combined[index] = combined.get(index, np.zeros(3)) + delta - reference
            else:
                for index, delta in target.items():
                    combined[index] = combined.get(index, np.zeros(3)) + delta
        return combined

    def points(self, original, morphs):
        result = self.vertices[original].copy()
        for spec in SPECS:
            weight = morphs.get(spec['key'], 0) / 100
            if not weight:
                continue
            raw = self.raw_morph(spec['positive' if weight > 0 else 'negative'])
            result += np.array([raw.get(index, np.zeros(3)) for index in original]) * abs(weight)
        return result

    def nearest_surface(self, points, neighbors=16):
        closest_points, face_ids, barycentric, distances = [], [], [], []
        for point in points:
            candidates = np.atleast_1d(self.head_tree.query(point, k=neighbors)[1])
            best = (float('inf'), None, None, None)
            for face_id in candidates:
                candidate, weights = closest_triangle(point, *self.head_triangles[face_id])
                distance = float(np.linalg.norm(point - candidate))
                if distance < best[0]:
                    best = (distance, face_id, weights, candidate)
            distances.append(best[0])
            face_ids.append(best[1])
            barycentric.append(best[2])
            closest_points.append(best[3])
        return np.array(closest_points), np.array(face_ids), np.array(barycentric), np.array(distances)

    def build_surface_map(self):
        """Align the generic canonical face without using any reference photograph."""
        eye_l = self.joint(self.bones['eye.L']['head'])
        eye_r = self.joint(self.bones['eye.R']['head'])
        source_center = (CANON[33] + CANON[263]) / 2
        scales = np.full(3, self.scale)
        translation = (eye_l + eye_r) / 2 - scales * source_center
        ids = np.array(sorted(set(range(0, 468, 6)) | KEY))
        source = CANON[ids]
        for _ in range(8):
            aligned = CANON * scales + translation
            closest, _, _, distances = self.nearest_surface(aligned[ids])
            weights = np.minimum(1, .08 / np.maximum(distances, 1e-9))
            root = np.sqrt(weights)
            for axis in range(3):
                design = np.column_stack((source[:, axis], np.ones(len(source))))
                coefficients = np.linalg.lstsq(design * root[:, None], closest[:, axis] * root, rcond=None)[0]
                scales[axis], translation[axis] = coefficients
        aligned = CANON * scales + translation
        _, face_ids, barycentric, distances = self.nearest_surface(aligned, 24)
        faces = self.head_faces[face_ids]
        self.surface_map = (faces, barycentric, distances)
        return self.surface_map

    def surface_points(self, ids, morphs):
        if self.surface_map is None:
            self.build_surface_map()
        faces, barycentric, _ = self.surface_map
        selected = faces[ids]
        unique, inverse = np.unique(selected.reshape(-1), return_inverse=True)
        shaped = self.points(unique, morphs)
        triangles = shaped[inverse.reshape(selected.shape)]
        return np.sum(triangles * barycentric[ids, :, None], axis=1)

    def derivative(self, original, spec, weight):
        positive = np.array([self.raw_morph(spec['positive']).get(index, np.zeros(3)) for index in original]) if spec['positive'] else np.zeros((len(original), 3))
        negative = np.array([self.raw_morph(spec['negative']).get(index, np.zeros(3)) for index in original]) if spec['negative'] else np.zeros((len(original), 3))
        if abs(weight) < .01:
            if not spec['negative']:
                return positive
            if not spec['positive']:
                return -negative
            return (positive - negative) * .5
        return positive if weight > 0 else -negative


MESHES = {}


def mesh(sex):
    if sex not in MESHES:
        MESHES[sex] = Mesh(sex)
    return MESHES[sex]


def normalize(points):
    midpoint = (points[33] + points[263]) / 2
    eye = points[263] - points[33]
    scale = np.linalg.norm(eye)
    angle = -math.atan2(eye[1], eye[0])
    c, s = math.cos(angle), math.sin(angle)
    rotation = np.array([[c, -s], [s, c]])
    return (points - midpoint) @ rotation.T / scale, rotation, scale


def landmark_weights(ids):
    weights = []
    for index in ids:
        if index in FEATURE:
            weights.append(1.8)
        elif index in KEY:
            weights.append(1.0)
        elif index in {10, 152, 234, 454}:
            weights.append(.7)
        else:
            weights.append(.32)
    return np.array(weights)


def score(points, reference, ids, weights):
    normalized, _, _ = normalize(points)
    return float(np.sqrt(np.sum(weights * np.sum((normalized[ids] - reference[ids]) ** 2, axis=1)) / weights.sum()))


def export_initial():
    script = """
import fs from 'node:fs';
import { PRESETS, normalizeAppearance } from './web/js/appearance.js';
import { MORPH_DEFS } from './web/js/morphs.js';
const output = {};
for (const [key, preset] of Object.entries(PRESETS)) {
  if (!(key in %s)) continue;
  const value = normalizeAppearance(preset);
  output[key] = { male: value.male, morphs: Object.fromEntries(MORPH_DEFS.map((definition, index) => [definition.key, value.morphs[index]])) };
}
fs.writeFileSync('build/render-fit-initial.json', JSON.stringify(output));
""" % json.dumps({key: True for key in REFS})
    subprocess.run(['node', '--input-type=module', '-e', script], cwd=REPO, check=True)
    return json.loads((BUILD / 'render-fit-initial.json').read_text())


class Driver:
    def __init__(self):
        self.process = subprocess.Popen(['node', 'tools/render-fit-driver.cjs', '--serve'], cwd=REPO,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        ready = self.process.stdout.readline()
        if not ready or not json.loads(ready).get('ready'):
            raise RuntimeError('Render driver failed to start')

    def render(self, candidates, keys):
        request = {'candidates': {key: candidates[key] for key in keys}, 'keys': keys}
        self.process.stdin.write(json.dumps(request) + '\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError('Render driver stopped unexpectedly')
        response = json.loads(line)
        if not response.get('ok'):
            raise RuntimeError(response.get('error', 'Render failed'))

    def close(self):
        if not self.process:
            return
        if self.process.poll() is None:
            try:
                self.process.stdin.close()
                self.process.wait(timeout=15)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self.process.kill()
        self.process = None


def detect(face_mesh, key):
    image = cv2.imread(str(BUILD / f'render-fit-{key}.png'))
    result = face_mesh.process(cv2.cvtColor(image, cv2.COLOR_BGR2RGB))
    if not result.multi_face_landmarks:
        raise RuntimeError(f'No generated face detected for {key}')
    height, width = image.shape[:2]
    return np.array([[point.x * width, point.y * height] for point in result.multi_face_landmarks[0].landmark[:468]])


def propose(key, candidate, points, reference):
    male = candidate['male']
    model = mesh('male' if male else 'female')
    ids = np.array([index for index in FIT if model.map_distance[index] < .34])
    original = model.canonical_original[ids]
    morphs = candidate['morphs']
    current = np.array([morphs[SPECS[index]['key']] / 100 for index in SELECT])
    model_points = model.points(original, morphs)
    detected = points[ids]
    design = np.column_stack((model_points, np.ones(len(ids))))
    base_weights = landmark_weights(ids)
    robust = base_weights.copy()
    for _ in range(4):
        root = np.sqrt(robust)
        coefficients = np.linalg.lstsq(design * root[:, None], detected * root[:, None], rcond=None)[0]
        error = np.linalg.norm(design @ coefficients - detected, axis=1)
        robust = base_weights / np.maximum(1, error / 12)
    normalized, rotation, scale = normalize(points)
    difference = reference[ids] - normalized[ids]
    columns = []
    for position, spec_index in enumerate(SELECT):
        derivative = model.derivative(original, SPECS[spec_index], current[position])
        projected = derivative @ coefficients[:3]
        eye_midpoint = (projected[list(ids).index(33)] + projected[list(ids).index(263)]) / 2
        transformed = (projected - eye_midpoint) @ rotation.T / scale
        columns.append(transformed)
    jacobian = np.stack(columns, axis=2)
    weights = landmark_weights(ids)
    matrix = (jacobian * weights[:, None, None]).reshape(-1, len(SELECT))
    target = (difference * weights[:, None]).ravel()
    regular = np.array([.08 if SPECS[index]['key'] == 'age' else .04 for index in SELECT])
    damping = np.full(len(SELECT), .025)
    matrix = np.vstack((matrix, np.diag(regular), np.diag(damping)))
    target = np.concatenate((target, -regular * current, np.zeros(len(SELECT))))
    lower = np.maximum(-1 - current, -.25)
    upper = np.minimum(1 - current, .25)
    for position, spec_index in enumerate(SELECT):
        if SPECS[spec_index]['min'] == 0:
            lower[position] = max(0 - current[position], -.25)
    result = lsq_linear(matrix, target, bounds=(lower, upper), method='bvls', tol=1e-10, max_iter=200)
    print('proposal', key, 'jacobian', round(float(np.max(np.abs(matrix))), 5),
          'step', round(float(np.max(np.abs(result.x))), 5), 'cost', round(float(result.cost), 6),
          'optimality', round(float(result.optimality), 6), 'iterations', result.nit, result.message, flush=True)
    return result.x


def coordinate(driver, face_mesh, candidates, observations, scores, histories, references):
    """Bounded coordinate descent measured from real renders, never source photos."""
    passes = int(os.environ.get('RENDER_FIT_PASSES', '1'))
    step = int(os.environ.get('RENDER_FIT_STEP', '8'))
    requested = os.environ.get('RENDER_FIT_SLIDERS', '')
    names = requested.split(',') if requested else COORDINATE
    definitions = {spec['key']: spec for spec in SPECS}
    for iteration in range(passes):
        for key in candidates:
            candidate = candidates[key]
            model = mesh('male' if candidate['male'] else 'female')
            ids = np.array([index for index in FIT if model.map_distance[index] < .34])
            weights = landmark_weights(ids)
            for name in names:
                if name not in definitions or name not in candidate['morphs']:
                    continue
                definition = definitions[name]
                base = candidate['morphs'][name]
                best_score = scores[key]
                best_candidate = candidate
                best_points = observations[key]
                best_value = base
                for direction in (-1, 1):
                    value = int(np.clip(base + direction * step, definition['min'], definition['max']))
                    if value == base:
                        continue
                    trial = json.loads(json.dumps(candidate))
                    trial['morphs'][name] = value
                    driver.render({key: trial}, [key])
                    points = detect(face_mesh, key)
                    measured = score(points, references[key], ids, weights)
                    if measured < best_score - .00020:
                        best_score = measured
                        best_candidate = trial
                        best_points = points
                        best_value = value
                        shutil.copyfile(BUILD / f'render-fit-{key}.png', BUILD / f'render-fit-best-{key}.png')
                if best_value != base:
                    candidate = best_candidate
                    candidates[key] = candidate
                    observations[key] = best_points
                    scores[key] = best_score
                    histories[key].append(best_score)
                    print('coordinate accept', iteration, key, name, base, '->', best_value,
                          round(best_score, 5), flush=True)
                output = {candidate_key: {**candidate_value, 'score': scores[candidate_key], 'history': histories[candidate_key]}
                          for candidate_key, candidate_value in candidates.items()}
                (BUILD / 'render-fit-results.json').write_text(json.dumps(output, indent=2))
        step = max(2, step // 2)


def main():
    selected = [key for key in (sys.argv[1:] or list(REFS)) if key in REFS]
    candidate_file = os.environ.get('RENDER_FIT_CANDIDATES')
    initial = json.loads((REPO / candidate_file).read_text()) if candidate_file else export_initial()
    candidates = {key: initial[key] for key in selected}
    observations = {}
    scores = {}
    histories = {key: [] for key in selected}
    scales = {key: .25 for key in selected}
    rejections = {key: 0 for key in selected}
    done = set()
    generations = int(os.environ.get('RENDER_FIT_GENERATIONS', '6'))
    references = {key: normalize(np.array(REFS[key]['points'])[:, :2])[0] for key in selected}
    driver = Driver()
    atexit.register(driver.close)
    with mp.solutions.face_mesh.FaceMesh(static_image_mode=True, max_num_faces=1, refine_landmarks=True,
                                        min_detection_confidence=.3) as face_mesh:
        driver.render(candidates, selected)
        for key in selected:
            observations[key] = detect(face_mesh, key)
            ids = np.array([index for index in FIT if mesh('male' if candidates[key]['male'] else 'female').map_distance[index] < .34])
            weights = landmark_weights(ids)
            scores[key] = score(observations[key], references[key], ids, weights)
            histories[key].append(scores[key])
            shutil.copyfile(BUILD / f'render-fit-{key}.png', BUILD / f'render-fit-best-{key}.png')
            print('initial', key, round(scores[key], 5), flush=True)
        initial_output = {key: {**candidates[key], 'score': scores[key], 'history': histories[key]} for key in selected}
        (BUILD / 'render-fit-results.json').write_text(json.dumps(initial_output, indent=2))
        if os.environ.get('RENDER_FIT_MODE') == 'coordinate':
            coordinate(driver, face_mesh, candidates, observations, scores, histories, references)
            driver.close()
            print('finished', {key: round(scores[key], 5) for key in selected})
            return
        for generation in range(generations):
            active = [key for key in selected if key not in done]
            if not active:
                break
            trial = {key: json.loads(json.dumps(candidates[key])) for key in active}
            deltas = {}
            for key in active:
                delta = propose(key, candidates[key], observations[key], references[key]) * scales[key]
                deltas[key] = delta
                for position, spec_index in enumerate(SELECT):
                    name = SPECS[spec_index]['key']
                    trial[key]['morphs'][name] = int(round((candidates[key]['morphs'][name] / 100 + delta[position]) * 100))
                changed = sum(trial[key]['morphs'][SPECS[index]['key']] != candidates[key]['morphs'][SPECS[index]['key']] for index in SELECT)
                print('trial', key, 'changed sliders', changed, flush=True)
            driver.render(trial, active)
            for key in active:
                points = detect(face_mesh, key)
                ids = np.array([index for index in FIT if mesh('male' if candidates[key]['male'] else 'female').map_distance[index] < .34])
                weights = landmark_weights(ids)
                value = score(points, references[key], ids, weights)
                if value < scores[key] - .00020:
                    candidates[key] = trial[key]
                    observations[key] = points
                    scores[key] = value
                    histories[key].append(value)
                    scales[key] = min(.45, scales[key] * 1.12)
                    rejections[key] = 0
                    shutil.copyfile(BUILD / f'render-fit-{key}.png', BUILD / f'render-fit-best-{key}.png')
                    print('accept', generation, key, round(value, 5), flush=True)
                else:
                    scales[key] *= .5
                    rejections[key] += 1
                    print('reject', generation, key, round(value, 5), 'best', round(scores[key], 5), flush=True)
                    if scales[key] < .07 or rejections[key] >= 4:
                        done.add(key)
            output = {key: {**candidates[key], 'score': scores[key], 'history': histories[key]} for key in selected}
            (BUILD / 'render-fit-results.json').write_text(json.dumps(output, indent=2))
    driver.close()
    print('finished', {key: round(scores[key], 5) for key in selected})


if __name__ == '__main__':
    main()
