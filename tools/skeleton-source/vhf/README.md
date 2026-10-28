# Skeleton source: the Visible Human cadavers

The game's skeleton is not an artist's model and not a polygon library's
anatomy. It is segmented from two real cadaver CT scans, one per sex, so the
bones are the right size, the right shape and the right number, and the two
bodies differ the way two people differ rather than the way one model and one
warp differ.

| | female | male |
|---|---|---|
| dataset | Visible Human Female CT | Visible Human Male CT |
| slices | 1,727 | 1,873 |
| in-plane | 0.7227 mm | 0.9375 mm |
| slice spacing | 1.0 mm | 1.0 mm |
| matrix | 670 x 673 | 552 x 520 |
| age at death | 59 | 39 |
| stature, bone only | 1.713 m | measured by `verify-obj.mjs` |

Both are published by the NLM Visible Human Project and redistributed as DICOM
by the COMODO research group under CC BY 4.0. See `ATTRIBUTION.md`.

## Why the whole body is segmented at native resolution

An earlier revision of this directory downsampled the volume to
1.4 x 1.4 x 2.0 mm before thresholding, to make the arithmetic cheap. That was
wrong, and it is worth writing down why so nobody does it again.

The joint between two bones is a gap of one to two millimetres of cartilage. At
2.0 mm slice spacing that gap is smaller than a voxel, so the partial-volume
average bridges it and the bones merge. The arm became a single 20,675-voxel
blob, and her feet collapsed from 26 bones each into 65 fused fragments for the
whole body. Every downstream failure -- the arm that could not be split, the
region fits that diverged by 140 mm, the parts that would not pass the
verifier -- descended from that one averaging step.

At native resolution the gap is one to three voxels wide and it survives. Her
feet now separate into **29 components each**, which is what a foot looks like
when its tarsals, metatarsals and phalanges are distinct.

## Pipeline

Three steps, no dependencies beyond numpy, scipy, scikit-image and
fast-simplification.

```sh
# 1. threshold, fill, label -- in overlapping 240-slice slabs so the whole
#    volume never has to be resident at once.
python3 01-segments.py --subject VHF --dicom VHF/DICOM --labels labF \
    --hu 1600 --min-voxels 60 --table labF.json

# 2. one mesh per bone, named against the atlas, decimated per bone.
python3 02-bones.py --table labF.json --atlas ../skeleton-male.obj \
    --obj skeleton-female.obj --parts skeleton-female.parts.json --triangles 60000

# 3. look at it, because the numbers cannot tell you whether it reads as a person.
python3 render-obj.py --obj skeleton-female.obj --out front.png --view front
```

### Threshold

1600 HU. Bone is above 1300 HU, but the scanner table and the cadaver's
restraint frame are dense enough to weld the body into one component at 1250
and 1400; 1600 leaves bone and discards both. Cortical bone is what survives,
which is why the mesh is a clean shell rather than a solid.

### Naming

The atlas (`../skeleton-male.obj`, 28 parts) is used **only to decide which part
a voxel belongs to**. No shape, no proportion and no vertex comes from it. It is
fitted on the two things a cadaver scan measures reliably -- the soles and the
stature -- and then every voxel of every component is assigned to the nearest
bone axis. That is what cuts a component that spans two bones: the boundary
between two axes is the joint, so a femur-plus-tibia blob is divided at the
knee without anyone having to find the knee.

### Decimation

Quadric edge collapse (`fast-simplification`), applied per bone with a floor of
44 triangles. A grid cluster was tried first and turned every phalanx into a
tetrahedron -- at 1,000 voxels there are not enough grid cells for a toe to keep
a toe's shape.

## What is still wrong

**Her arms.** The Visible Human Female was scanned supine with both arms flexed
90 degrees at the elbow and her hands resting on her iliac crests. Two
consequences, both visible in `evidence/female-arm-radiograph.png`:

1. The hand is in physical contact with the pelvis, so at 1600 HU the arm and
   the pelvis are one connected component. Thresholding cannot separate them
   because there is no gap to find.
2. The atlas has its arms straight down, so nearest-axis assignment cannot place
   the elbow or the wrist correctly on a bent arm. `hand_l` and `hand_r` come
   out missing and the humerus measures short.

The fix is to separate the arm along its own medial path -- skeletonise the
component, take the shoulder-to-hand geodesic, cut at the bend and at the wrist
-- and then to repose it to a rest stance. Until that lands, the shipped
`skeleton-female.obj` stays as it is and the female does not pass
`verify-obj.mjs`.

Everything else segments correctly: skull, mandible and teeth, the full spine,
the rib cage and sternum, both scapulae and clavicles, the pelvis, and both
legs from femur to phalanx.

## Files

- `01-segments.py` -- slab-wise threshold, hole fill, labelling, seam merge.
- `02-bones.py` -- atlas naming, per-voxel bone assignment, marching cubes,
  per-bone decimation, OBJ and parts JSON.
- `render-obj.py` -- the orthographic renderer used for every image here. It has
  no dependencies beyond numpy and Pillow, because it has to be usable to check
  a claim rather than to make a picture.
- `evidence/` -- the images referred to above.
