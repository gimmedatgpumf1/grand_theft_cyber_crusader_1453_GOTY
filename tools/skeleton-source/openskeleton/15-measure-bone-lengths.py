"""Measure limb-bone mesh dimensions in the shipped OBJ and source NPZ.

From the repository root: python3 tools/skeleton-source/openskeleton/15-measure-bone-lengths.py
Requires NumPy and SciPy. Writes limb and pelvis measurement CSV files in the parent directory. These are
geometric mesh measures, not expert-selected clinical osteometric landmarks.
"""
import csv
from pathlib import Path

import numpy as np
from scipy.spatial import ConvexHull, distance

HERE = Path(__file__).resolve().parent
BASE = HERE.parent
SUBJECTS = {
    "male": ("N197925", "male-N197925.npz"),
    "female": ("N186416", "skelly-out.npz"),
}


def limb_names():
    names = ["Humerus", "Radius", "Ulna", "Femur", "Tibia", "Fibula"]
    for region in ("Hand", "Foot"):
        for digit in range(1, 6):
            for segment in ("Meta", "ProxP", "MidP", "DistP"):
                if segment == "MidP" and digit == 1:
                    continue  # first digit has only two phalanges
                names.append(f"{region}{segment}{digit}")
    return names


def read_obj(path):
    groups = {}
    name = None
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            if line.startswith("g "):
                name = line.split()[1]
                if name in groups:
                    raise ValueError(f"duplicate OBJ group: {name}")
                groups[name] = []
            elif line.startswith("v "):
                if name is None:
                    raise ValueError("vertex before bone group")
                groups[name].append([float(v) * 1000 for v in line.split()[1:4]])
    return groups


def dimensions(points):
    points = np.asarray(points, dtype=np.float64)
    if points.ndim != 2 or points.shape[1] != 3 or len(points) < 4:
        raise ValueError("invalid bone vertices")
    direction = np.linalg.svd(points - points.mean(axis=0), full_matrices=False)[2][0]
    axial_span = np.ptp(points @ direction)
    # A farthest pair of a point set always lies on its convex hull.
    hull = ConvexHull(points)
    diameter = distance.pdist(points[hull.vertices]).max()
    return float(diameter), float(axial_span)


def main():
    output = BASE / "BONE_LENGTH_MEASUREMENTS.csv"
    pelvis_rows = []
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["subject", "sex_label", "bone", "side", "game_diameter_mm",
                         "source_diameter_mm", "game_pca_span_mm", "source_pca_span_mm"])
        for sex, (subject, filename) in SUBJECTS.items():
            game = read_obj(BASE / f"skeleton-{sex}.obj")
            with np.load(HERE / "data" / filename) as source:
                # Approximate breadth in the registered source's transverse X
                # direction. Extremes are not verified iliac-crest landmarks.
                game_pelvis = np.concatenate([game["Pelvis_L"], game["Pelvis_R"]])
                source_pelvis = np.concatenate([source["v_Pelvis_L"], source["v_Pelvis_R"]])
                pelvis_rows.append([subject, sex, f"{np.ptp(game_pelvis[:, 0]):.4f}",
                                    f"{np.ptp(source_pelvis[:, 0]):.4f}"])
                for side in ("L", "R"):
                    for bone in limb_names():
                        key = f"{bone}_{side}"
                        game_d, game_axis = dimensions(game[key])
                        source_d, source_axis = dimensions(source[f"v_{key}"])
                        writer.writerow([subject, sex, bone, side, f"{game_d:.4f}",
                                         f"{source_d:.4f}", f"{game_axis:.4f}",
                                         f"{source_axis:.4f}"])
    pelvis_output = BASE / "PELVIS_WIDTH_MEASUREMENTS.csv"
    with pelvis_output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["subject", "sex_label", "game_transverse_width_mm", "source_transverse_width_mm"])
        writer.writerows(pelvis_rows)
    print(f"Wrote {output} and {pelvis_output}")


if __name__ == "__main__":
    main()
