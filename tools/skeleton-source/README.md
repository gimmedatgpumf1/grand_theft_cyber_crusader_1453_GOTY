# Skeleton source

The C room uses two CT-derived, registered OpenSkeleton subjects from the
[Zenodo dataset](https://zenodo.org/records/22833546), CC BY 4.0:

- Male `N197925`: `skeleton-male.obj`.
- Female `N186416`: `skeleton-female.obj`.

The included NPZ and JSON files contain the extracted source vertices and
faces for each subject. Each OBJ retains all 182 source structure names in
`g` groups and 27 runtime `o` groups. The room renders individual source
structures with rigid transforms. Each source structure is simplified separately
into an overall budget near 180,000 triangles. No bones are mirrored, scaled
or repositioned individually. A single coordinate conversion and translation
places each body in the game. The registered pose is the unscaled bind pose;
the room starts in a straight-arm reference T-pose made from rigid joint
rotations and a whole-body floor translation, without scaling the source bones.

Both source files contain *registered* meshes, not necessarily the original CT
patient pose. Our source-fidelity tests check names, face indices and the
proximity of every simplified vertex to its corresponding source structure;
they do not certify the anatomy, registration, assembly or source labels.
The male source has 182 labels including `Head` and `Jaw` as whole structures;
separate teeth or skull-bone labels are not provided in this dataset.

## Rebuild and check

Python, NumPy and `fast-simplification` are required for regeneration:

```sh
python3 tools/skeleton-source/openskeleton/03-import.py
python3 tools/skeleton-source/openskeleton/03-import.py --male
python3 tests/openskeleton_source_pose.py female
python3 tests/openskeleton_source_pose.py male
```

See `ATTRIBUTION.md` for attribution and `openskeleton/README.md` for the
original source files and extraction. The reference PNGs are historical and do
not show these current models. The Z-Anatomy importer remains for historical
reproducibility; its output is not the currently shipped male.

## C room line view

Both C-room skeletons use the same estimated source-rig pose in mesh and V views.
Press V to replace both skeleton meshes with the simplified view;
press V again to return to the mesh view. Joint keys act on both subjects:
`1`–`5` bend each finger, `6`–`0` bend each toe, `Q` shoulders, `E` elbows,
`F` wrists, `G` hips, `H` knees, `J` ankles, `K` spine, `L` jaw/neck,
`Z` shoulder girdle, `X` thumb base, `N` hip rotation and `M` grouped forearm rotation. For the finger
keys, hold Ctrl for MCP only, Alt for PIP (thumb IP), or Ctrl+Alt for DIP
(fingers 2–5). With no modifier each whole finger bends. Hold Shift to reverse;
`R` restores the T-pose and `B` restores the original registered source pose.
The approximate reference angles and frames are described in
[JOINT_MOBILITY.md](JOINT_MOBILITY.md); these controls are not medical validation.
Pale lines approximate the supplied
bone shapes, pale dots represent the 24 cervical, thoracic and lumbar vertebrae,
cyan lines connect anatomical neighbours, and orange lines are inferred costal
cartilage (not present in the scans). Each palm is one filled, closed 3D shape
for the seven non-thumb carpals and metacarpals 2–5; the trapezium, thumb
metacarpal, and all finger bones stay separate. Each palm is a thin continuous
triangulated surface anchored to the individual source bones. On each side,
the radius and ulna share one thin filled forearm plane, and the tibia and
fibula share one thin filled shin plane. Each plane's outline is fitted to the
projected contours of both source bones; this deliberately flattens their 3D
depth rather than presenting a full anatomical surface. All 24 ribs form one
continuous thin filled rib-cage surface: successive source-derived curves are
joined on each side, with a rear seam connecting the sides. Filled inferred
cartilage areas join ribs 1–7 to the source-derived sternum centreline. Ribs
8–12 have no invented direct sternal attachments. The surface is a visual
simplification, not additional anatomical joints.

Both scapulae, both clavicles and an approximate manubrium are grouped into
one thin, filled shoulder-girdle surface. Narrow strips follow successive
source-derived cross-sections of each bone rather than filling the convex hull
across the shoulders and chest. The source labels the sternum as a single bone rather than separately labelling
the manubrium, so the upper 35% of its displayed centreline is an approximation;
the rest of the sternum remains separate and connects to the surface.

Each shoulder, elbow, hip and knee has a low-poly 3D ball fitted to the
source bone contact areas and the displayed connection endpoints. The kneecap
(patella) is enclosed by its knee ball rather than drawn as a separate bone.
The other long bones and grouped surfaces remain separate. OpenSkeleton does
not label individual joint centres or other small bones at these joints; the
balls are visual approximations, not measured joint surfaces. Their triangles
are cached with the rest of the V-view scene until a pose changes.

Each foot base is a **closed ten-point volume**: four corners at the front,
two at the narrow back of the heel, and four at the back and front of the
ankle. The front's top and bottom are fitted to the actual heights and
positions of the first and fifth metatarsal heads. The ankle top covers the
projected distal tibia and fibula without being stretched forward to the toes;
a separate instep joins it to the toe front. The sole reaches the back of the
calcaneus, and the toe phalanges remain separate. The source feet point down in
the registered pose, so the solid follows their anatomical direction rather
than lying level in world space. This is an approximation, not an anatomical
surface or a change to the animation rig.

Both models use the same explicit, connected table of 182 structures and 209
relationships. Internal links within each unified shape are hidden and
external links are retained, leaving 106 graph nodes and 142 visible links;
the eight joint balls visually cover the corresponding joint areas.
The table is based on OpenStax's
[thoracic cage](https://openstax.org/books/anatomy-and-physiology-2e/pages/7-4-the-thoracic-cage),
[upper limb](https://openstax.org/books/anatomy-and-physiology/pages/8-2-bones-of-the-upper-limb)
and [lower limb](https://openstax.org/books/anatomy-and-physiology-2e/pages/8-4-bones-of-the-lower-limb)
descriptions. The underlying graph connects ribs 1–7 via inferred cartilage
to the sternum, ribs 8–10 via the preceding rib, and leaves ribs 11–12 without
an anterior joint. Links internal to the rib-cage surface are hidden; each
rib's spinal connection and inferred sternum connections remain visible.

The source supplies surfaces, not joint landmarks. The anatomical table selects
relationships, never proximity alone. The room's estimated tree uses the 182
source mesh batches, population bend references and fitted or approximate
pivots. Each V-view vertex is attached to a named structure or a declared group,
so it follows poses; grouped surfaces deform as their owners move. This is not a
complete or medically validated 182-joint rig: some links are redundant or
fixed, some axes and source neutral angles are estimates, and cartilage,
closed loops and subject-specific constraints are not modelled. See
[JOINT_MOBILITY.md](JOINT_MOBILITY.md). To regenerate these inspection surfaces, NumPy and
SciPy are required in addition to the OBJ importer
requirements. Regenerate and check after rebuilding the OBJs with:

```sh
python3 tools/skeleton-source/openskeleton/04-line-graph.py
python3 tools/skeleton-source/openskeleton/05-joint-links.py
python3 tests/skeleton_line_graph.py
```
