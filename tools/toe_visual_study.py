#!/usr/bin/env python3
"""Offline visual sculpting study, not a game asset or anatomical model.

Input is the blue comparison phalanx OBJ export from skeleton_cranial_export
(--all). Nothing here imports a generic foot mesh. The actual game does not
load the generated files. Inspect dorsal, plantar AND side views: the current
surface still has an artificial rear cutoff and needs further visual work.

Dependencies: numpy, scipy, scikit-image, trimesh, rtree, pillow.
"""
import argparse
from pathlib import Path

import numpy as np
import trimesh
from PIL import Image, ImageDraw
from scipy.interpolate import PchipInterpolator
from skimage.measure import marching_cubes


def read_bone(directory, sex, part, digit):
    path = directory / f"{sex}_blue_Foot{part}P{digit}_L.obj"
    return np.asarray(trimesh.load(path, process=False).vertices)


def describe_toe(directory, sex, digit, scale):
    segments = [read_bone(directory, sex, "Prox", digit)]
    if digit != 1:
        segments.append(read_bone(directory, sex, "Mid", digit))
    segments.append(read_bone(directory, sex, "Dist", digit))
    bounds = [(p.min(0), p.max(0), p.mean(0)) for p in segments]
    start = bounds[0][0][2] - .0015 * scale
    end = bounds[-1][1][2] + .003 * scale
    typical = np.mean([np.ptp(p[:, 0]) for p in segments])
    if digit == 1:
        distal_width = np.ptp(segments[-1][:, 0]) * .5 + .0018 * scale
        root_width = np.ptp(segments[0][:, 0]) * .5 + .0015 * scale
    else:
        distal_width = max(.0055 * scale, typical * .55 + .0012 * scale)
        root_width = max(.0056 * scale, typical * .56 + .0013 * scale)
    width = PchipInterpolator(
        [0, .12, .34, .57, .75, .88, .95, 1],
        [root_width, root_width * .99, root_width * .94, distal_width * .91,
         distal_width, distal_width * .96, distal_width * .78, .00005 * scale])
    low = min(float(p[:, 1].min()) for p in segments) - .003 * scale
    root_top = bounds[0][1][1] + .003 * scale
    tip_top = bounds[-1][1][1] + .004 * scale
    tip_center = (tip_top + low) * .46
    top = PchipInterpolator([0, .20, .52, .78, .94, 1],
                            [root_top, root_top * .96, root_top * .78, tip_top,
                             tip_top * .78, tip_center])
    bottom = PchipInterpolator([0, .20, .60, .80, .93, 1],
                               [low, low - .0003 * scale, low - .0025 * scale,
                                low - .004 * scale, low - .0015 * scale, tip_center])
    return {"bounds": bounds, "start": start, "end": end, "width": width,
            "top": top, "bottom": bottom,
            "x": [b[2][0] for b in bounds], "z": [b[2][2] for b in bounds]}


def smooth_union(a, b, k):
    h = np.maximum(k - np.abs(a - b), 0) / k
    return np.minimum(a, b) - h * h * k * .25


def surface(specs, scale):
    step = .0012 * scale
    xs = np.arange(min(s["x"][0] for s in specs) - .018 * scale,
                   max(s["x"][0] for s in specs) + .019 * scale, step)
    ys = np.arange(min(s["bottom"](0) for s in specs) - .015 * scale,
                   max(s["top"](0) for s in specs) + .020 * scale, step)
    zs = np.arange(min(s["start"] for s in specs) - .013 * scale,
                   max(s["end"] for s in specs) + .007 * scale, step)
    x, y, z = np.meshgrid(xs, ys, zs, indexing="ij")

    def toe_field(s):
        t = np.clip((z - s["start"]) / (s["end"] - s["start"]), 0, 1)
        xc = np.interp(z, s["z"], s["x"])
        w = np.maximum(.0001 * scale, s["width"](t))
        upper, lower = s["top"](t), s["bottom"](t)
        half = np.maximum(.0001 * scale, (upper - lower) * .5)
        cy = (upper + lower) * .5
        distance = (np.sqrt(((x - xc) / w) ** 2 + ((y - cy) / half) ** 2) - 1) * np.minimum(w, half)
        return np.maximum(distance, z - s["end"])

    field = toe_field(specs[0])
    for s in specs[1:]:
        field = smooth_union(field, toe_field(s), .0006 * scale)
    proximal_x = np.array([s["bounds"][0][2][0] for s in specs])
    proximal_z = np.array([s["bounds"][0][2][2] for s in specs])
    front = np.interp(x, proximal_x, proximal_z) + .009 * scale
    back = min(s["start"] for s in specs) - .009 * scale
    xmin = min(s["x"][0] - float(s["width"](0)) for s in specs)
    xmax = max(s["x"][0] + float(s["width"](0)) for s in specs)
    center, radius = (xmin + xmax) * .5, (xmax - xmin) * .5
    upper = np.interp(x, proximal_x, [s["bounds"][0][1][1] + .003 * scale for s in specs])
    lower = min(s["bottom"](0) for s in specs)
    middle, yradius = (upper + lower) * .5, (upper - lower) * .5
    cross = ((np.abs((x - center) / radius) ** 3.5 +
              np.abs((y - middle) / yradius) ** 3.5) ** (1 / 3.5) - 1) * np.minimum(radius, yradius)
    web = cross + .008 * scale * np.logaddexp(0, (z - front) / (.005 * scale))
    field = np.maximum(smooth_union(field, web, .006 * scale), back - z)
    # Small sculpted folds near the source phalanx joints, not texture lines.
    for s in specs:
        for first, second in zip(s["bounds"][:-1], s["bounds"][1:]):
            joint = (first[1][2] + second[0][2]) * .5
            xc = np.interp(joint, s["z"], s["x"])
            u = np.abs((x - xc) / ((.006 if len(s["bounds"]) > 2 else .009) * scale))
            v = (z - joint) / (.001 * scale)
            dorsal = np.clip((y - .006 * scale) / (.005 * scale), 0, 1)
            field += .00025 * scale * np.exp(-v * v) * np.exp(-u ** 4) * dorsal
    vertices, faces, _, _ = marching_cubes(field, level=0, spacing=(step, step, step))
    vertices += np.array([xs[0], ys[0], zs[0]])
    return trimesh.Trimesh(vertices=vertices, faces=faces, process=False)


def nail(s, digit, skin, scale):
    columns, rows = 15, 17
    start, end = (.70 if digit == 1 else .77), .95
    xz = []
    for row in range(rows):
        v = row / (rows - 1)
        t = start + (end - start) * v
        z = s["start"] + (s["end"] - s["start"]) * t
        xc = np.interp(z, s["z"], s["x"])
        taper = .77 + .23 * np.sqrt(max(0, np.sin(np.pi * v)))
        for col in range(columns):
            u = 2 * col / (columns - 1) - 1
            xz.append((xc + u * float(s["width"](t)) * .68 * taper, z))
    origins = np.array([(x, max(skin.vertices[:, 1]) + .05 * scale, z) for x, z in xz])
    directions = np.tile([0., -1., 0.], (len(origins), 1))
    hits, indices, _ = skin.ray.intersects_location(origins, directions, multiple_hits=False)
    if len(indices) != len(origins):
        raise RuntimeError(f"Nail {digit} does not lie on the sculpted dorsal skin")
    lower = origins.copy()
    lower[indices, 1] = hits[:, 1] + .00010 * scale
    upper = lower.copy()
    upper[:, 1] += .00055 * scale
    vertices = np.concatenate((lower, upper))
    layer = len(lower)
    faces = []
    for row in range(rows - 1):
        for col in range(columns - 1):
            a = row * columns + col
            b, c, d = a + 1, a + columns, a + columns + 1
            faces.extend([(a, c, b), (b, c, d),
                          (a + layer, b + layer, c + layer), (b + layer, d + layer, c + layer)])

    def wall(a, b):
        faces.extend([(a, b, a + layer), (b, b + layer, a + layer)])

    for col in range(columns - 1):
        wall(col, col + 1)
        wall((rows - 1) * columns + col + 1, (rows - 1) * columns + col)
    for row in range(rows - 1):
        wall((row + 1) * columns, row * columns)
        wall(row * columns + columns - 1, (row + 1) * columns + columns - 1)
    return trimesh.Trimesh(vertices=vertices, faces=faces, process=False)


def render(objects, matrix, light, path):
    triangles = []
    light = np.asarray(light) / np.linalg.norm(light)
    for mesh, color in objects:
        normals = mesh.vertex_normals
        for face in mesh.faces:
            vertices = mesh.vertices[face]
            projected = vertices @ matrix.T
            normal = normals[face].mean(axis=0)
            normal /= max(1e-18, np.linalg.norm(normal))
            brightness = .55 + .45 * abs(np.dot(normal, light))
            triangles.append((projected[:, 2].mean(), projected[:, :2], brightness, color))
    xy = np.vstack([triangle[1] for triangle in triangles])
    low, high = xy.min(0), xy.max(0)
    scale = min(900 / (high[0] - low[0]), 800 / (high[1] - low[1]))
    image = Image.new("RGB", (1100, 900), (35, 40, 39))
    painter = ImageDraw.Draw(image)
    for _, xy, brightness, color in sorted(triangles, key=lambda triangle: triangle[0], reverse=True):
        xy = (xy - (low + high) / 2) * scale + np.array([550, 450])
        painter.polygon([tuple(p) for p in xy], fill=tuple(int(c * brightness) for c in color))
    image.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bones", type=Path, help="directory of --all blue comparison OBJ exports")
    parser.add_argument("output", type=Path, help="output directory outside the repository")
    parser.add_argument("--sex", choices=["male", "female"], default="female")
    parser.add_argument("--scale", type=float, default=1., help="height in cm divided by 170")
    args = parser.parse_args()
    if args.scale <= 0:
        parser.error("scale must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    specs = [describe_toe(args.bones, args.sex, d, args.scale) for d in range(1, 6)]
    skin = surface(specs, args.scale)
    skin.export(args.output / "skin.obj")
    objects = [(skin, (194, 143, 123))]
    for digit, s in enumerate(specs, 1):
        plate = nail(s, digit, skin, args.scale)
        plate.export(args.output / f"nail-{digit}.obj")
        objects.append((plate, (224, 178, 168)))
    views = {
        "dorsal": ([[1, 0, 0], [0, -.25, -1], [0, -1, .25]], [-.2, .9, .3]),
        "side": ([[0, 0, -1], [0, -1, 0], [1, 0, 0]], [-.7, .65, .25]),
        "plantar": ([[1, 0, 0], [0, .25, -1], [0, 1, .25]], [-.2, -.9, .3]),
    }
    for name, (matrix, light) in views.items():
        render(objects, np.array(matrix, dtype=float), light, args.output / f"{name}.png")
    print("Visual study only: inspect all three angles; this is not a finished replacement.")


if __name__ == "__main__":
    main()
