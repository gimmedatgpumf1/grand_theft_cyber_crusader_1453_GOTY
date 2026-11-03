# Vulkan rigid solver — experimental

The test room's R toggle now builds four skeleton rigs and submits the simulation
and skinning to Vulkan compute. Both the bone meshes and V-view inspection shapes
follow GPU body transforms; no physics state is read back to the CPU each frame.
Rig construction runs male and female preparation concurrently on the CPU.
The normal Linux and Windows native build scripts no longer link Bullet; the
older Bullet sources and Bullet-specific historical tooling remain in the tree.
The historical test and measurement build commands still reference the old room
API and source lists and need migration to the new GPU path.

This is **not a PhysX-equivalent engine**. The solver uses graph-coloured joint
constraints, approximate angular limits, floor support and sphere-proxy contacts.
Room rigs use a coarser proxy grid with at most 64 cells per body; rendered bones
are unchanged, but collision shapes are less precise. Contact search uses four
GPU lanes per body, conservative world-space bounds, and a parallel bitset broad
phase to avoid repeatedly searching distant proxies.
Sphere proxies are sorted once along each body's longest local axis; the narrow
phase searches only a conservative interval without changing the proxy shapes.
Pair transforms are composed once per candidate rather than twice per proxy,
floor scanning projects proxy centres directly onto the floor normal instead
of rotating every point in full, and per-step damping factors are computed once
on the host instead of as exponentials for every body.
It runs two contact searches per eight joint iterations, halving contact searches
and their associated dispatches relative to the previous schedule. Independent joint
islands execute two coloured joint/floor passes per workgroup dispatch, using
workgroup barriers between colours; cross-island contacts remain global. This
reduces dispatch overhead, but limits parallelism when there are few islands;
its effect on integrated-GPU frame rate is not yet measured. An ordinary
per-colour dispatch path remains for unsupported schedules. Fewer contact
iterations reduce work but can reduce contact convergence. The solver dissipates
velocity from projection corrections and uses a non-bouncing floor. Floor and
body contacts apply bounded friction. Body penetration correction is capped and
kept out of reconstructed velocity to reduce energy injected into piles. A
sleeping body acts immovable until it is woken. Joint islands supported by the
floor or another figure can sleep on the GPU after two quiet half-second windows;
the quiet threshold is deliberately tolerant of small solver jitter. An approaching
impact or a grab can wake them, and resetting R rebuilds the rig. Sleeping bodies
skip joint and contact work, and GPU-generated indirect
dispatches skip those phases altogether while every island sleeps. The friction,
impact damping, sleep thresholds and wake-up heuristic are approximations, not
a general-purpose contact-island sleep system; slow impacts can fail to wake a
sleeper. In the room, after R starts the ragdolls, left-click and drag a bone; with
the cursor released, right-click returns to camera look. Picking and the drag
constraint execute on the GPU without per-frame pose readback.
The broad phase still scans all body pairs; it and the overlapping-proxy exclusions
need further work. Limits, collisions, settling and body stability have not been
fully validated. The current approximation can produce contorted poses. Replacing
or expanding this solver requires further visual and physical validation.

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
0.1 mm from a flat sheet. Reference-pose toe and ankle overlaps are trimmed;
independently moving toes and ankle bones can still intersect the sheet. If the
tilt search cannot clear fixed bones, their overlaps are trimmed too rather than
raising the sheet. A separate near-vertical medial sheet follows the big-toe
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
Vertical front panels follow these segments and meet the extended top, sole and
sides. Disconnected reference-pose bone and toe-skin cross-sections are cut out
separately, preserving the individually moving toes and clear areas between
them. Those cutouts can shorten a visible edge or leave openings where seams
meet the ankle or toes. The sole is visual geometry, not a new collision proxy.
The mesh moves with the foot body, so independently moving toes or ankle bones
can still intersect it.

The checked-in SPIR-V modules are compiled from the compute shaders with
glslang for Vulkan. `make_compute_headers.py` regenerates the embedded C++
headers from those modules. Client builds do not require a shader compiler.
