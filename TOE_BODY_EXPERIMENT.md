# Visual-first toe study

The blue comparison skeletons now render an early toe-and-forefoot visual study
on both feet. The shape is constructed from each comparison skeleton's own
phalanx and metatarsal vertices, rather than an imported or generic foot mesh.
It has separate toe clefts, subtle joint folds and closed, three-dimensional
nail plates with pale free edges. The right foot is reflected from the fitted
left side; the white skeletons are unchanged.

This is a **static visual stage**, not a finished body. The rounded skin is
placed on the floor independently of the bones, and the toe, sesamoid and
metatarsal bone batches are hidden beneath the opaque study; their source and
rig data remain in place. The experimental skin and nails do not yet follow
the toe joints. Their fit, articulation, contact, the plantar surface, profile
extremes and in-game GPU performance remain to be addressed; no clinical
accuracy or visual approval is claimed. Existing skeletal
intersections and other limitations remain documented in
`BONE_SYMMETRY_CHECKLIST.md`. The toe generator and source-to-comparison motion
helper are separate so that the latter can be revisited after the appearance.

The reference photographs supplied by King show dorsal and plantar views.
Relevant anatomical context includes
[lesser MTP surface anatomy](https://www.orthopaedicsone.com/orthopaedicsone-articles-the-lesser-metatarsal-phalangeal-joints-surface-anatomy/)
and [nail anatomy](https://musculoskeletalkey.com/nails/). These sources do not
provide subject-specific skin thickness, toe contours or deformation weights;
the surface dimensions are modelling estimates, not clinical measurements.
