"""Orthographic z-buffer renderer for the skeleton OBJ, so the mesh can be checked
before anything else is built on it.

  python3 render.py --obj vhf-dense.obj --out front.png --view front
"""
import argparse
import json

import numpy as np
from PIL import Image


def load(path):
    parts, verts, current = {}, [], None
    for line in open(path):
        if line.startswith('o '):
            current = line[2:].strip()
            parts[current] = {'v': [], 'f': []}
        elif line.startswith('v '):
            verts.append([float(x) for x in line.split()[1:4]])
            if current:
                parts[current]['v'].append(len(verts) - 1)
        elif line.startswith('f '):
            parts[current]['f'].append([int(t.split('/')[0]) - 1 for t in line.split()[1:4]])
    return np.array(verts), parts


PALETTE = [(226, 206, 178), (198, 176, 148), (240, 224, 198), (176, 156, 132),
           (214, 194, 168), (188, 168, 142), (232, 212, 186), (166, 148, 126)]


def render(verts, parts, view, size, centre, half, highlight=None):
    axes = {'front': (0, 1, 2), 'side': (2, 1, 0), 'top': (0, 2, 1)}[view]
    u, v, depth = axes
    scale = size / (2 * half)
    light = np.array([0.4, 0.7, 0.6])
    light /= np.linalg.norm(light)
    img = np.zeros((size, size, 3), dtype=np.float32)
    zbuf = np.full((size, size), -1e9, dtype=np.float32)

    def project(p):
        x = (p[:, u] - centre[u]) * scale + size / 2
        y = size / 2 - (p[:, v] - centre[v]) * scale
        z = p[:, depth]
        return x, y, z

    for slot, (name, part) in enumerate(sorted(parts.items())):
        if not part['f']:
            continue
        p = verts[np.array(part['v'])]
        x, y, z = project(p)
        tris = np.array(part['f'])
        local = {g: i for i, g in enumerate(part['v'])}
        tris = np.array([[local[a] for a in t] for t in tris])
        colour = np.array(PALETTE[slot % len(PALETTE)], dtype=np.float32) / 255.0
        if highlight and highlight in name:
            colour = np.array([1.0, 0.55, 0.2], dtype=np.float32)
        a, b, c = x[tris[:, 0]], x[tris[:, 1]], x[tris[:, 2]]
        ay, by, cy = y[tris[:, 0]], y[tris[:, 1]], y[tris[:, 2]]
        az, bz, cz = z[tris[:, 0]], z[tris[:, 1]], z[tris[:, 2]]

        e1x, e1y, e1z = b - a, by - ay, bz - az
        e2x, e2y, e2z = c - a, cy - ay, cz - az
        nx, ny, nz = e1y * e2z - e1z * e2y, e1z * e2x - e1x * e2z, e1x * e2y - e1y * e2x
        norm = np.sqrt(nx * nx + ny * ny + nz * nz) + 1e-9
        shade = np.abs(nx / norm * light[0] + ny / norm * light[1] + nz / norm * light[2])
        shade = 0.35 + 0.75 * shade

        x0 = np.maximum(np.floor(np.minimum(np.minimum(a, b), c)).astype(int), 0)
        x1 = np.minimum(np.ceil(np.maximum(np.maximum(a, b), c)).astype(int), size - 1)
        y0 = np.maximum(np.floor(np.minimum(np.minimum(ay, by), cy)).astype(int), 0)
        y1 = np.minimum(np.ceil(np.maximum(np.maximum(ay, by), cy)).astype(int), size - 1)

        area = (b - a) * (cy - ay) - (by - ay) * (c - a)
        keep = (x1 >= x0) & (y1 >= y0) & (np.abs(area) > 1e-6)
        for t in np.flatnonzero(keep):
            xs = np.arange(x0[t], x1[t] + 1)
            ys = np.arange(y0[t], y1[t] + 1)
            gx, gy = np.meshgrid(xs, ys)
            w0 = ((b[t] - gx) * (cy[t] - gy) - (by[t] - gy) * (c[t] - gx)) / area[t]
            w1 = ((c[t] - gx) * (ay[t] - gy) - (cy[t] - gy) * (a[t] - gx)) / area[t]
            w2 = 1.0 - w0 - w1
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            gz = w0 * az[t] + w1 * bz[t] + w2 * cz[t]
            sub_z = zbuf[y0[t]:y1[t] + 1, x0[t]:x1[t] + 1]
            win = inside & (gz > sub_z)
            if not win.any():
                continue
            sub_z[win] = gz[win]
            img[y0[t]:y1[t] + 1, x0[t]:x1[t] + 1][win] = colour * shade[t]
    return img


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--obj', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--view', default='front')
    parser.add_argument('--size', type=int, default=900)
    parser.add_argument('--centre', default=None)
    parser.add_argument('--half', type=float, default=None)
    args = parser.parse_args()

    verts, parts = load(args.obj)
    low, high = verts.min(0), verts.max(0)
    centre = np.array(args.centre.split(','), dtype=float) if args.centre else (low + high) / 2
    half = args.half or float(max(high - low)) / 2 * 1.05
    img = render(verts, parts, args.view, args.size, centre, half)
    Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).save(args.out)
    print(f'{args.out}: {len(verts)} vertices, {len(parts)} parts, bbox '
          f'{np.round(low, 3)} .. {np.round(high, 3)}')


if __name__ == '__main__':
    main()
