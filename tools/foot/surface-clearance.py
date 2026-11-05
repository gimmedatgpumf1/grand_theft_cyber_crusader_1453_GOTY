"""Offline triangle-mesh clearance fitting for the experimental foot.

All units are metres. This is not a runtime constraint solver. It uses FCL's
triangle-mesh collision and distance queries, and rejects output if the surface
cannot be validated; it also verifies containment using a temporary ankle cap.
"""
import numpy as np
import trimesh
import networkx as nx
import fcl

MINIMUM = 0.0001
# Extra room accommodates numerical rounding in the generated C++ data.
FIT_MARGIN = 0.0002


def collision_object(vertices, faces):
    model = fcl.BVHModel()
    model.beginModel(len(vertices), len(faces))
    model.addSubModel(np.asarray(vertices, dtype=np.float64), np.asarray(faces, dtype=np.int32))
    model.endModel()
    return fcl.CollisionObject(model)


def load_bones(folder):
    paths = sorted(path for path in folder.glob('*.obj') if not path.name.startswith('skin'))
    if not paths:
        raise ValueError(f'{folder}: no exported bone meshes')
    return [(path.stem, trimesh.load_mesh(path, process=False)) for path in paths]


def load_ankle_bones(folder):
    path = folder / 'ankle-bones'
    paths = sorted(path.glob('*.obj'))
    if len(paths) != 2 or {p.stem.split('_')[0] for p in paths} != {'Tibia','Fibula'}:
        raise ValueError(f'{folder}: expected the tibia and fibula exports')
    return [(p.stem, trimesh.load_mesh(p, process=False)) for p in paths]


def closed_mesh(vertices, faces):
    mesh = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
    boundary = mesh.edges[trimesh.grouping.group_rows(mesh.edges_sorted, require_count=1)]
    loops = nx.cycle_basis(nx.from_edgelist(boundary))
    if len(loops) != 1:
        raise ValueError(f'expected one ankle boundary, found {len(loops)}')
    loop = loops[0]
    directed = {tuple(edge) for edge in boundary.tolist()}
    cap = []
    for i, a in enumerate(loop):
        b = loop[(i + 1) % len(loop)]
        cap.append([b, a, len(vertices)] if (a, b) in directed else [a, b, len(vertices)])
    result = trimesh.Trimesh(vertices=np.vstack([vertices, vertices[loop].mean(axis=0)]),
                              faces=np.vstack([faces, cap]), process=False)
    if not result.is_watertight or not result.is_winding_consistent or result.volume <= 0:
        raise ValueError('foot surface is not a consistently wound enclosure')
    return result


def clearance_valid(vertices, faces, bones, margin=MINIMUM, ankle_bones=()):
    """Fast accept/reject: surface intersections, distance and containment."""
    skin = collision_object(vertices, faces)
    for _, bone in list(bones) + list(ankle_bones):
        other = collision_object(bone.vertices, bone.faces)
        result = fcl.CollisionResult()
        if fcl.collide(skin, other, fcl.CollisionRequest(), result):
            return False
        result = fcl.DistanceResult()
        if fcl.distance(skin, other, fcl.DistanceRequest(), result) < margin:
            return False
    enclosure = closed_mesh(vertices, faces)
    # Once surfaces are disjoint, one inside point per connected component is
    # sufficient; some source bones have more than one component.
    representatives = [component.vertices[0] for _,bone in bones
                       for component in bone.split(only_watertight=False)]
    return bool(enclosure.contains(np.array(representatives)).all())


def fit_clearance(vertices, faces, bones, margin=FIT_MARGIN, max_steps=80, ankle_bones=()):
    """Push intersecting or near skin facets out until all bone surfaces clear.

    The exact check is for these provided mesh surfaces and this one pose only.
    Caller must separately validate other poses/profiles and any reduced mesh.
    """
    if margin < MINIMUM:
        raise ValueError('fit margin is below the required 0.1 mm clearance')
    vertices = np.array(vertices, dtype=np.float64, copy=True)
    faces = np.asarray(faces, dtype=np.int32)
    all_bones = list(bones) + list(ankle_bones)
    bone_objects = [collision_object(bone.vertices, bone.faces) for _, bone in all_bones]
    neighbors = [set() for _ in vertices]
    for a, b, c in faces:
        neighbors[a].update((b, c)); neighbors[b].update((a, c)); neighbors[c].update((a, b))
    for step in range(max_steps):
        mesh = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
        skin = collision_object(vertices, faces)
        normals = mesh.face_normals
        push = np.zeros(len(faces))
        contacts = 0
        minimum = float('inf')
        for (_, bone), other in zip(all_bones, bone_objects):
            result = fcl.CollisionResult()
            fcl.collide(skin, other, fcl.CollisionRequest(num_max_contacts=5000, enable_contact=True), result)
            if len(result.contacts) >= 5000:
                raise ValueError('contact limit reached; cannot safely fit this surface')
            if result.contacts:
                contacts += len(result.contacts)
                for contact in result.contacts:
                    face, bone_face = contact.b1, contact.b2
                    normal = normals[face]
                    if np.linalg.norm(normal) < 0.9:
                        raise ValueError('degenerate contact facet')
                    depths = (bone.triangles[bone_face] - mesh.triangles[face, 0]) @ normal
                    push[face] = max(push[face], min(.001, max(depths.max() + margin, .00003)))
                continue
            result = fcl.DistanceResult()
            distance = fcl.distance(skin, other, fcl.DistanceRequest(enable_nearest_points=True), result)
            minimum = min(minimum, distance)
            if distance < margin:
                push[result.b1] = max(push[result.b1], margin - distance + .00005)
        if not np.any(push):
            enclosure = closed_mesh(vertices, faces)
            for name, bone in bones:
                if not enclosure.contains(bone.vertices).all():
                    raise ValueError(f'{name}: bone is not wholly inside the fitted skin')
            print(f'  triangle clearance >= {minimum*1000:.3f} mm, no surface intersections')
            return vertices
        displacement = np.zeros_like(vertices)
        counts = np.zeros(len(vertices))
        for face in np.flatnonzero(push):
            for vertex in faces[face]:
                displacement[vertex] += normals[face] * push[face]
                counts[vertex] += 1
        active = counts > 0
        displacement[active] /= counts[active, None]
        smooth = np.zeros_like(vertices)
        weight = np.zeros(len(vertices))
        for i in np.flatnonzero(active):
            for j in neighbors[i]:
                smooth[j] += displacement[i]
                weight[j] += 1
        active_neighbors = weight > 0
        smooth[active_neighbors] /= weight[active_neighbors, None]
        vertices += .55 * (.75 * displacement + .25 * smooth)
        print(f'  clearance step {step}: {contacts} contacts, {len(np.flatnonzero(push))} facets', flush=True)
    raise ValueError(f'clearance did not converge in {max_steps} steps')
