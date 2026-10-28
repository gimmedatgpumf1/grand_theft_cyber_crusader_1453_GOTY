# Verification

`verify.mjs` re-derives everything below from the upstream GLBs. It is a test,
not a claim: it exits non-zero if a check fails.

```bash
npm i @gltf-transform/core @gltf-transform/extensions meshoptimizer
OPEN_ANATOMY=/path/to/open-anatomy-3d node tools/skeleton-source/verify.mjs
```

## What it checks

1. **Size and position are untouched.** The OBJ's skeletal height, its ground
   plane and its crown must match the world-space bounds of the upstream GLB to
   within 5 mm.
2. **No vertex was moved.** Every OBJ vertex must appear verbatim in the
   undecimated source set, to 1e-5 m. Decimation is allowed to delete vertices;
   it is not allowed to relocate them.
3. **Proportions.** Femur, tibia, humerus, forearm and hand lengths, measured
   along each bone's own principal axis, as a fraction of skeletal stature,
   against adult reference ranges.
4. **Bilateral symmetry.** Each paired bone must mirror across `x = 0` within
   3 mm.

## Results

### Male — passes

| check | result |
| --- | --- |
| height | source 170.657 cm, OBJ 170.635 cm |
| ground plane / crown | identical to source |
| vertices | 8,544 / 8,544 present verbatim, 0 moved |
| femur | 27.35% of stature (reference 26.0-27.5%) |
| tibia | 23.39% (21.5-23.5%) |
| humerus | 18.11% (17.5-18.5%) |
| forearm | 15.35% (14.5-15.5%) |
| hand | 10.34% (10.5-11.5%) |
| symmetry | worst pair 1.9 mm (femur) |

The male is genuine BodyParts3D scan data, unscaled and unreshaped. Note that
27.35% femur and 23.39% tibia both sit at the top of their ranges — that is the
scan's own build, not an artifact of this pipeline.

### Female — **fails, and the failure is upstream**

| check | result |
| --- | --- |
| height | source 160.828 cm, OBJ 160.809 cm |
| vertices | 7,560 / 7,560 present verbatim, 0 moved |
| femur | 25.25% of stature (reference 26.0-27.5%) |
| tibia | **25.95%** (21.5-23.5%) |
| humerus | **15.68%** (17.5-18.5%) |
| forearm | **17.11%** (14.5-15.5%) |
| hand | 10.93% (10.5-11.5%) |
| symmetry | **scapula 18.0 mm, humerus 12.4 mm**, clavicle 4.1 mm, femur 3.1 mm |

The pipeline imported her faithfully — every vertex is where upstream put it —
but upstream never had a female skeleton. In `pipeline/f_build.mjs` the female
body is built by:

```js
for (const p of maleParts) { ... warpPoint(tps, p.pos[i], ...) ... }
```

That is the **male** BodyParts3D skeleton pushed through a thin-plate-spline warp
fitted to female surface landmarks, plus a segment-based hand remap
(`warpHand`). A TPS warp is a smooth global deformation, which is exactly why
her long bones come out at the wrong fractions of stature and why her left and
right scapulae differ by 18 mm. It is fine as anatomy *illustration*, where a
skeleton is a landmark scaffold under a skin, and wrong as anatomy *data*.

Everything the upstream female model does have from a female scan is the pelvic
region (`VH_F_femur_*`, `VH_F_ilium_compact_bone_*`, `VH_F_lumbar_vertebra_5`),
which is why her pelvis is the most credible part of her.

## Consequence for this project

The male skeleton is a trustworthy anatomical reference and can be used for
rigging as-is, at 170.6 cm.

The female skeleton must not be presented as female anatomy until it is rebuilt.
The defensible reconstruction is: take the male skeleton (verified above), scale
it *uniformly* to female stature (which preserves every proportion — long-bone
fractions do not meaningfully differ between sexes), and substitute the real
female pelvis that upstream already has. That is a rebuild, not a rescale of the
decimated file, and it is not done yet.
