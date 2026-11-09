# Vulkan rigid solver — experimental

The test room's R toggle now builds four skeleton rigs and submits the simulation
and skinning to Vulkan compute. Both the bone meshes and V-view inspection shapes
follow GPU body transforms; no physics state is read back to the CPU each frame.
Rig construction runs male and female preparation concurrently on the CPU.
The normal Linux and Windows native build scripts no longer link Bullet; the
older Bullet sources and Bullet-specific historical tooling remain in the tree.
The historical test and measurement build commands still reference the old room
API and source lists and need migration to the new GPU path.

This is **not a PhysX-equivalent engine**. The shared Vulkan contact path uses
oriented box cells, the same code for all bodies. The Playground beam and loose
blocks use their actual box dimensions; the triangular support is a stepped
approximation. Skeleton collision cells are fitted to occupied mesh-vertex
cells (up to 64 per body), not an exact triangle mesh or a volumetric anatomical
model. Rig construction still uses spheres for its initial overlap exclusions
and approximate rib attachment search, not for runtime body or floor contacts.
The broad phase uses conservative bounds and a per-body bitset; cell ranges are
sorted by their longest axis for narrow-phase culling. Picking uses the same
oriented cells. No CPU pose readback is required for simulation or picking.

Every simulation step uses eight joint/floor passes and two contact searches.
Body contacts use one representative point per pair, bounded position
correction and friction; they are not a full contact manifold or continuous
collision detection. The final articulated graph reconstruction enforces the
existing joint constraints, then adjusts the previous pose by the reconstruction
correction so that this repair is not treated as extra velocity. This does not
make the solver physically exact or guarantee stability. SwiftShader Vulkan
captures showed improved initial cube/plank contact and fewer violent early
skeleton kicks, but the fallen skeletons continued drifting and interacting
unnaturally after several seconds. Settling and energy behavior remain open.
Dragging and releasing a block and a skeleton were inspected visually. No
claim is made that fast impacts, all contact configurations, strict joints,
concave bone shapes or long-term stability are solved.

The checked-in SPIR-V can be rebuilt with a Vulkan GLSL compiler. For example,
`glslangValidator -V --target-env vulkan1.0 -Inative/vulkan/physics -o native/vulkan/physics/rigid_solver.comp.spv native/vulkan/physics/rigid_solver.comp`
and the corresponding command for `rigid_skin.comp`, followed by
`python3 native/vulkan/physics/make_compute_headers.py`.

The room path was inspected using SwiftShader's software Vulkan implementation,
not a physical GPU. Windows/MSVC, integrated-GPU compatibility and performance
have not been verified; no speed or frame-rate claim is made. The server ticks at
20 Hz. The local-only room simulation uses three fixed 60 Hz substeps per 50 ms
interval; its ragdolls and dragging are not server-authoritative or replicated.
GPU simulation steps are capped at two per rendered frame to avoid a catch-up
spiral; below 30 rendered frames per second it runs slower than wall-clock time.

`rigid_runtime` owns compute pipelines and buffers. The renderer requires a
queue with graphics and compute support, dispatches fixed steps, and writes the
resulting matrices directly into the per-frame graphics buffer. Synchronization
is required before replacing or destroying these resources. `rigid_solver_data.h`
contains the shared layouts and constraint colouring; the contact passes use a
symmetric exclusion bitset and per-body scratch space instead of float atomics.

Each blue toe now has one connected exterior mesh. Its rings follow their owning
phalanges through the same GPU bone matrices, and triangles bridge the joints;
the buried prism caps and rear tip faces are omitted. Side faces between separate
toes remain because independent motion can expose them. The low-poly bridges are
not a soft-tissue simulation and can still intersect or expose bone at extreme
poses. Flat nail meshes follow their distal bones; their vertices are included
in the corresponding collision proxy. The proxy continues to approximate the
underlying rigid pieces rather than resolving an exact thin nail surface.

Each foot also has a flat sheet attached to the metatarsal body, extending to
the ankle. It slopes towards the little toe; its sagittal tilt clears adjacent
fixed foot bones, and its supporting metatarsal is 0.1 mm from the plane along
its normal in the reference pose. The irregular bones cannot all be uniformly
0.1 mm from a flat sheet. Reference-pose fixed foot and ankle bone overlaps are
trimmed if needed; the final sheets pass through the toe geometry without toe
cutouts. Independently moving toes and ankle bones can still intersect the
sheet. If the tilt search cannot clear fixed bones, their overlaps are trimmed
rather than raising the sheet. A separate near-vertical medial sheet follows the big-toe
side from the first metatarsal to the heel. It is fitted to the reference foot
bones. A matching near-vertical lateral sheet follows the little-toe side to
the heel. Two near-vertical heel panels meet at a rear hinge, and a flat sole
lies 0.1 mm below the lowest fixed foot bone in the reference pose. The six
fitted planes are extended to their mutual intersections, clipped to remove
protruding portions, and welded into one foot-owned indexed mesh. Their fitted
plane equations are retained. In plan view the front follows straight
segments through the 50% points of the five proximal octagonal toe prisms
(measured between their ring centers). The outer segments also pass through
the outer vertices of those toes' 50% rings before reaching the side sheets.
Uninterrupted vertical front panels follow these segments, meet the extended
top, sole and sides, and pass directly through the toes. The toe geometry is
not cut out from any of the stitched sheets. The sole is visual geometry, not a
new collision proxy. The foot mesh moves with the foot body; the separately
moving toes intersect the panels by design, and ankle bones can still intersect
it in moving poses.

The checked-in SPIR-V modules are compiled from the compute shaders with
glslang for Vulkan. `make_compute_headers.py` regenerates the embedded C++
headers from those modules. Client builds do not require a shader compiler.
