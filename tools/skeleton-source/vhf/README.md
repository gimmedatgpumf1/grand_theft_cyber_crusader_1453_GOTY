# The female body

`tools/skeleton-source/skeleton-female.obj` is **not** a female skeleton. This
directory is the attempt to replace it with a real one, and this file records
exactly how far that got.

## Where a real female skeleton comes from

The Visible Human Female — the NLM's cadaver dataset, a 59-year-old woman — has a
full-body CT in a reachable, MIT-licensed mirror:
https://github.com/COMODO-research/Comodo_VisibleHuman (`VHF/DICOM`, 1,727 slices
of 670 x 673 at 0.7227 x 0.7227 x 1.0 mm, `VHF_FullBody_CT0000.dcm` … `1726`).

That is the only complete female skeleton found so far. Everything else in the
open anatomy world is one of three things:

| source | what it actually contains |
| --- | --- |
| Z-Anatomy / BodyParts3D | one adult male. BodyParts3D's own description says "an adult human male". |
| Human Reference Atlas v1.10 (united-female) | skin, organs, pelvis, legs and brain. No skull, no ribs, no shoulder girdle, no arms, no feet. |
| open-anatomy-3d female | the male skeleton pushed through a thin-plate-spline warp. Not a scan. |
| Andreassen et al. (2023), VHF STL set | the same woman, pelvis to ankle only — 28 bones, no skull, ribs or arms. |

## What the pipeline does

```bash
# 1. DICOM -> a 1.446 x 1.446 x 2.0 mm volume (2.0 mm down the body's long axis)
python3 01-volume.py /path/to/VHF/DICOM /tmp/vhf/vol2.npy

# 2. threshold, separate, name, and mesh each bone
python3 02-segment.py /tmp/vhf/vol2.npy /tmp/vhf/dense.obj

# 3. simplify each bone on its own into the shipped low-poly asset
python3 03-decimate.py /tmp/vhf/dense.obj /tmp/vhf/female.obj
```

Requirements: `numpy`, `scipy`, `scikit-image`, `pydicom`.

## What works, with numbers

* **Bone threshold 1600 HU.** Muscle and fat sit near 900–1100 in this scan and
  bone well above 1300. At 1250 HU the scanner table welds every bone into a
  single 943,161-voxel blob; at **1600 HU the skeleton separates into 238
  connected components**, which is the right order for a human skeleton.
* **The bones are individually separated where it matters.** Individual
  vertebrae, every rib, the teeth, the mandible, and — the hard case — the foot:
  at 1600 HU one foot alone resolves into 65 components, with the calcaneus,
  talus, navicular, cuboid, the three cuneiforms, all five metatarsals and every
  phalanx as its own piece.
* **Her size is her size.** Her bone height, lowest calcaneus voxel to the top of
  the cranium, is **1.718 m** across 859 slices at 2.0 mm. Fitting the atlas to
  her bones puts the best scale at **1.00**; nothing is rescaled.
* **The assembled mesh is 287,939 vertices / 581,438 triangles** at full
  resolution, and 5,475 / 13,582 after per-bone clustering into the 28 rig parts.
  `segmentation-bones.png` is that segmentation, each bone a different colour:
  skull, individual vertebrae down the spine, all the ribs, both scapulae and
  clavicles, both arms with their fingers, the pelvis, both legs and both feet.

## What does not work yet, and why the file is not in the game

Naming the components is reliable — the skull, mandible, pelvis, both femora,
both tibiae, both patellae, both clavicles and both feet all match the expected
bone by centroid and by size, and the whole 28-part set gets an assignment.

**Cutting the fused joints is not solved.** Her arms are the problem case: at
1600 HU each arm is a single connected blob of humerus + radius + ulna + hand
(20,675 voxels, 396 mm long), because the joint cartilage is thin and the scan's
partial volume bridges it. Splitting that blob along its own principal axis and
cutting at the deepest cross-section minima put both cuts within 6 mm of each
other in the middle of the arm — the arms are lying across the body and the
cross-section profile has no clean neck. Registering the atlas per region to find
the cut lines failed the other way: the region ICP diverged to 50–140 mm RMS
because her arms are not in the atlas's pose and there is nothing but bone to
constrain the rotation.

So the honest state is: the voxels are hers, the shapes are hers, the height is
hers, the separation into individual bones is right for most of the body — and
the arm chains need a joint-locating method that has not been found yet. Passing
off an arm that is one bone as "all fingers and joints working" would be exactly
the kind of claim that started this rewrite.

Until that is fixed, `skeleton-female.obj` stays the old warped-male mesh and the
test room says so on screen.
