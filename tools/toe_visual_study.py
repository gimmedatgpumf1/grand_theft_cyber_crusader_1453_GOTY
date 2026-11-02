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
from PIL import Image
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
        [root_width, root_width * 1.02, root_width * .91, distal_width * .96,
         distal_width * .98, distal_width * .96, distal_width * .78, .00005 * scale])
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
            "top": top, "bottom": bottom, "tip_scale": 1.,
            "x": [b[2][0] for b in bounds], "z": [b[2][2] for b in bounds]}


def separate_toe_tips(specs, scale):
    # Reduce only distal flesh where neighbouring toes would fuse together.
    # This is an artistic shape constraint, not proof of bone clearance.
    for first, second in zip(specs[:-1], specs[1:]):
        gap = second["x"][-1] - first["x"][-1]
        if gap <= 0:
            raise ValueError("Unexpected toe order in the blue skeleton")
        available = max(gap - .0006 * scale, gap * .7)
        total = float(first["width"](.85) + second["width"](.85))
        factor = min(1., available / total)
        first["tip_scale"] = min(first["tip_scale"], factor)
        second["tip_scale"] = min(second["tip_scale"], factor)


def toe_section(s, t):
    t = np.asarray(t)
    # Round the end in both planes, so the side silhouette is a soft toe pad
    # rather than a pointed cone and the nail need not follow an abrupt bend.
    shoulder = .88
    core_t = np.minimum(t, shoulder)
    q = np.clip((t - shoulder) / (1 - shoulder), 0, 1)
    cap = np.sqrt(np.maximum(0, 1 - q * q))
    upper0, lower0 = s["top"](core_t), s["bottom"](core_t)
    middle = (upper0 + lower0) * .5
    upper = middle + (upper0 - middle) * cap
    lower = middle + (lower0 - middle) * cap
    blend = np.clip((t - .4) / .35, 0, 1)
    blend = blend * blend * (3 - 2 * blend)
    narrowing = 1 - blend * (1 - s["tip_scale"])
    return s["width"](core_t) * cap * narrowing, upper, lower


def smooth_union(a, b, k):
    h = np.maximum(k - np.abs(a - b), 0) / k
    return np.minimum(a, b) - h * h * k * .25


def surface(specs, scale):
    step = .0008 * scale
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
        w, upper, lower = toe_section(s, t)
        w = np.maximum(.0001 * scale, w)
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
    # Shallow, curved transverse folds at the phalanx joints.  These deform
    # the surface itself, with a separate crease beneath the toe pad.
    for s in specs:
        for first, second in zip(s["bounds"][:-1], s["bounds"][1:]):
            joint = (first[1][2] + second[0][2]) * .5
            xc = np.interp(joint, s["z"], s["x"])
            t = np.clip((joint - s["start"]) / (s["end"] - s["start"]), 0, 1)
            width, upper, lower = toe_section(s, t)
            u = (x - xc) / max(float(width), .001 * scale)
            dorsal = np.clip((y - (upper + lower) * .5) / (.004 * scale), 0, 1)
            plantar = np.clip(((upper + lower) * .5 - y) / (.004 * scale), 0, 1)
            along = z - joint - .0012 * scale * u * u
            envelope = np.exp(-(u / .78) ** 6)
            crease = np.exp(-(along / (.00085 * scale)) ** 2)
            echo = np.exp(-((along - .0018 * scale) / (.00075 * scale)) ** 2)
            field += scale * envelope * (.00038 * dorsal * (crease + .35 * echo) +
                                          .00032 * plantar * crease)
    vertices, faces, _, _ = marching_cubes(field, level=0, spacing=(step, step, step))
    vertices += np.array([xs[0], ys[0], zs[0]])
    return trimesh.Trimesh(vertices=vertices, faces=faces, process=False)


def nail(s, digit, skin, scale):
    columns, rows = 15, 17
    start, end = (.66 if digit == 1 else .72), .92
    xz = []
    for row in range(rows):
        v = row / (rows - 1)
        taper = .84 + .16 * np.sqrt(max(0, np.sin(np.pi * v)))
        for col in range(columns):
            u = 2 * col / (columns - 1) - 1
            # Rounded proximal and distal corners; the plate follows the toe
            # rather than being a rectangular block placed on top.
            t = start + (end - start) * v + .018 * u ** 4 * (1 - 2 * v)
            z = s["start"] + (s["end"] - s["start"]) * t
            xc = np.interp(z, s["z"], s["x"])
            width, _, _ = toe_section(s, t)
            xz.append((xc + u * float(width) * .68 * taper, z))
    origins = np.array([(x, max(skin.vertices[:, 1]) + .05 * scale, z) for x, z in xz])
    directions = np.tile([0., -1., 0.], (len(origins), 1))
    hits, indices, _ = skin.ray.intersects_location(origins, directions, multiple_hits=False)
    if len(indices) != len(origins):
        raise RuntimeError(f"Nail {digit} does not lie on the sculpted dorsal skin")
    lower = origins.copy()
    lower[indices, 1] = hits[:, 1] + .00010 * scale
    upper = lower.copy()
    for row in range(rows):
        v = row / (rows - 1)
        for col in range(columns):
            u = 2 * col / (columns - 1) - 1
            # Soft edges and an arched plate, not a floating flat decal.
            relief = .35 + .65 * (1 - u * u) * np.sqrt(max(0., np.sin(np.pi * v)))
            upper[row * columns + col, 1] += .00055 * scale * relief
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
    mesh = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
    # Subtle vertex colours for the translucent-looking plate and free edge;
    # the nail is still a separate closed volume, not a decal or texture.
    colors = np.empty((len(vertices), 4), dtype=np.uint8)
    for row in range(rows):
        v = row / (rows - 1)
        for col in range(columns):
            u = 2 * col / (columns - 1) - 1
            edge = max(0., (abs(u) - .7) / .3)
            tip = max(0., (v - .86) / .14)
            base = np.array([210., 157., 147.])
            color = base + np.array([9., 9., 8.]) * (1 - edge) + np.array([14., 20., 23.]) * tip
            colors[row * columns + col] = (*color.astype(np.uint8), 255)
            colors[layer + row * columns + col] = (*color.astype(np.uint8), 255)
    mesh.visual.vertex_colors = colors
    return mesh


def render(objects, matrix, light, path):
    # A depth buffer is important here: sorting translucent-looking thin nail
    # triangles by their centroid makes the skin appear to punch holes in them.
    light = np.asarray(light, dtype=float)
    light /= np.linalg.norm(light)
    projected = [mesh.vertices @ matrix.T for mesh, _ in objects]
    xy = np.vstack([points[:, :2] for points in projected])
    low, high = xy.min(0), xy.max(0)
    scale = min(900 / (high[0] - low[0]), 800 / (high[1] - low[1]))
    image = np.empty((900, 1100, 3), dtype=np.uint8)
    image[:] = (35, 40, 39)
    depth = np.full((900, 1100), np.inf)
    for (mesh, color), points in zip(objects, projected):
        normals = mesh.vertex_normals
        screen = (points[:, :2] - (low + high) / 2) * scale + np.array([550, 450])
        for face in mesh.faces:
            p = screen[face]
            x0 = max(0, int(np.floor(p[:, 0].min())))
            x1 = min(1099, int(np.ceil(p[:, 0].max())))
            y0 = max(0, int(np.floor(p[:, 1].min())))
            y1 = min(899, int(np.ceil(p[:, 1].max())))
            if x0 > x1 or y0 > y1:
                continue
            a, b, c = p
            denom = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if abs(denom) < 1e-12:
                continue
            xx, yy = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
            u = ((b[1] - c[1]) * (xx - c[0]) + (c[0] - b[0]) * (yy - c[1])) / denom
            v = ((c[1] - a[1]) * (xx - c[0]) + (a[0] - c[0]) * (yy - c[1])) / denom
            w = 1 - u - v
            z = u * points[face[0], 2] + v * points[face[1], 2] + w * points[face[2], 2]
            target_depth = depth[y0:y1 + 1, x0:x1 + 1]
            mask = (u >= 0) & (v >= 0) & (w >= 0) & (z < target_depth)
            if not np.any(mask):
                continue
            n = (u[..., None] * normals[face[0]] + v[..., None] * normals[face[1]] +
                 w[..., None] * normals[face[2]])
            n /= np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-18)
            brightness = .55 + .45 * np.abs(n @ light)
            if mesh.visual.kind == "vertex":
                colors = mesh.visual.vertex_colors[face, :3].astype(float)
                pigment = (u[..., None] * colors[0] + v[..., None] * colors[1] + w[..., None] * colors[2])
            else:
                pigment = np.asarray(color)
            pixels = np.clip(brightness[..., None] * pigment, 0, 255).astype(np.uint8)
            image[y0:y1 + 1, x0:x1 + 1][mask] = pixels[mask]
            target_depth[mask] = z[mask]
    Image.fromarray(image).save(path)


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
    separate_toe_tips(specs, args.scale)
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
