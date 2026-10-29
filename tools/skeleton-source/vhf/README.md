> Historical pipeline: the current room assets are OpenSkeleton, not the
> Visible Human Female output described below. See `../README.md`.

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

# 2. one mesh per bone, cut at the joints, named, decimated per bone.
python3 02-bones.py --table labF.json --atlas ../skeleton-male.obj \
    --raw vol-full.npy --obj skeleton-female.obj \
    --parts skeleton-female.parts.json --triangles 60000

# 3. look at it, because the numbers cannot tell you whether it reads as a person.
python3 render-obj.py --obj skeleton-female.obj --out front.png --view front
```

### Threshold

1600 HU. Bone is above 1300 HU, but the scanner table and the cadaver's
restraint frame are dense enough to weld the body into one component at 1250
and 1400; 1600 leaves bone and discards both. Cortical bone is what survives,
which is why the mesh is a clean shell rather than a solid.

### Cutting a fused limb

Thresholding gives one connected mass for a whole arm and one for a whole
thigh-plus-shin, because cartilage is a millimetre thick and partial volume
bridges it. Two facts about bone undo that:

1. **Cortical bone reaches 2,000-3,000 HU; the bridge across a joint does not.**
   At 2400 HU her left arm leaves exactly three dense islands -- humerus,
   radius, ulna -- and her legs leave thigh and shin.
2. **Two bones that share a joint are further apart along the bone than across
   it.** Assigning each voxel to the dense island it is nearest *along the bone*
   (a geodesic distance through the mask) puts the border in the middle of the
   bridge, which is where the joint is.

An intensity watershed was tried first and is wrong: the humerus seed is the
brightest and floods down the fused forearm, swallowing it.

### Naming

The appendicular skeleton is named by **height band and side**, measured on the
subject (`anatomy.py`, knee 482 mm, ankle 157 mm, hip 898 mm up from her soles),
because a folded arm has no usable relationship to an atlas pose -- naming her
arm parts by atlas position handed the left humerus to the pelvis and the right
one to nothing. The axial skeleton -- skull, spine, ribs, sternum, pelvis,
scapulae, clavicles -- does not move between poses and is named against the
atlas.

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

## Result

`skeleton-female.obj` is hers, segmented from her own CT. 28 rig parts, 442
individual bones, 62,077 vertices, 130,728 triangles, **1.713 m**, soles at
y = 0, no scaling anywhere in the pipeline.

| | |
| --- | --- |
| components at 1600 HU | 401 |
| bones in each foot | **29** (a foot has 26; the extras are a sesamoid and a split cuneiform) |
| fused masses cut at their joints | 5 -- both arms, both thigh-plus-shin, one more |
| landmarks measured | knee 482 mm, hip 898 mm, ankle 147 mm above her soles |
| hip height | 52.0% of stature |
| knee height | 27.0% of stature |

Those two joint fractions are the same as any skeleton's, male or female,
because they are set by how a body is put together rather than by how big it is.
They are what proves the mesh has not been stretched or squeezed.

### Her proportions are hers

She is short-limbed: her humerus is 15.6-16.0% of her stature where the female
Trotter & Gleser mean is 17.2-20.9%, which is how a person's build works out
when the only thing measured is the bone. That is not a deformation, and three
measurements say so:

- the deviation is **bilateral** -- left and right humerus differ by 10.8 mm,
  forearm by 9.8 mm, femur by 4.5 mm;
- the **joint fractions are exactly right**, so nothing has been scaled;
- the foot count, the pelvis, the ribs and the spine all come out clean.

`verify-obj.mjs --sex female --scanned` therefore reports these as
`individual`, not `FAIL`, and fails only a part that is both out of band and
disagrees with its own reflection. On her, nothing does.

### What the scan cannot give

Her arms were folded across her abdomen with her hands against her ilia. The
bones of the hand are all present and separate -- 25 to 27 pieces per side -- but
the wrist is fused to the pelvis at 1600 HU, so the carpus is assigned by
distance from the end of the forearm rather than by a joint gap. The parts are
correct; the split between the last radius and the first carpal is a judgement
rather than a measurement.

The patella is not isolated either: at the knee the patella touches the femoral
condyles, and no separate dense island appears between them.

## Files

- `01-segments.py` -- slab-wise threshold, hole fill, labelling, seam merge.
- `02-bones.py` -- atlas naming, per-voxel bone assignment, marching cubes,
  per-bone decimation, OBJ and parts JSON.
- `render-obj.py` -- the orthographic renderer used for every image here. It has
  no dependencies beyond numpy and Pillow, because it has to be usable to check
  a claim rather than to make a picture.
- `evidence/` -- the images referred to above.
