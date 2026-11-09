# Planar Vulkan physics

Press P then 2 for the planar room. R resets it, and W toggles water and resets
the simulation. Drag with the left mouse button. The old 3D ragdoll solver and
Bullet sources are removed; historical Bullet-based scripts still need migration.
The skeleton room has no R-mode ragdolls.

The simulation and picking are GPU-resident; the renderer consumes body
transforms without a per-frame physics readback or CPU pixel upload. A Vulkan
workgroup uses 32 lanes: body integration and collision discovery run in
parallel. A round-robin edge coloring groups pairs that share no bodies, so
contacts and joints in a group can update velocities and positions in parallel
without floating-point atomic writes. Groups execute in order with barriers;
constraint solving is therefore not fully parallel. Small worlds expose only a
few simultaneous pairs, and the current code does not distribute one world
across multiple workgroups. Bodies and contact scratch now live in sized storage
buffers rather than fixed-size workgroup arrays. The former 24-body ceiling is
removed, but each world still allocates all unordered pairs and processes all
colors: memory and collision work grow quadratically, and buffer-size limits,
GPU memory, dispatch time and watchdogs constrain feasible world sizes.

The intended server tick is 20 Hz (50 ms). This local room subdivides each
such interval into twelve 1/240 s physics steps; it is not server-authoritative.
Each semi-implicit step uses mass, planar moment of inertia, gravity,
unilateral contact impulses, Coulomb friction and point-to-point pin joints.
Contacts use at most two points per pair. The friction coefficient for a pair
is the geometric mean of the two surface coefficients. Restitution uses the
larger coefficient and is suppressed for contact speeds below two gravity
steps, to prevent gravity-induced resting jitter. Position projection reduces
joint drift and overlap without adding the projection directly to velocity.
All of these are finite-iteration approximations, not an exact simultaneous
constraint solution.

Within each physics step, the workgroup scans non-overlapping convex pairs for
the earliest predicted impact, advances the whole world to just before it,
applies an impact impulse, solves current contacts and joints, and repeats
collision discovery with the updated velocities. Translation uses swept
separating-axis intervals. Rotating polygons use conservative advancement with
separating-axis gaps and a linear-plus-angular speed bound when the maximum
relative point displacement exceeds one quarter of the narrower polygon's
minimum width. Below that threshold the translation sweep holds orientation
fixed and the regular discrete contacts handle rotation; this avoids repeated
zero-time rotational events in slow resting contacts, but can miss rotational
collisions. Sweeps assume velocities are constant between events; forces are
integrated at the beginning of the step. Events are ordered globally, while
detection and non-conflicting constraint pairs still use the 32 lanes.

There is a 64-event budget per physics step. On exhaustion, or when the
rotational query fails to converge in 24 iterations, the unresolved portion of
that step is dropped rather than advanced blindly. For existing contacts, the
remaining advance is also stopped when residual closing displacement exceeds
one quarter of the narrower shape's minimum width. Smaller residuals are left
to the finite contact and position solvers; they are not a zero-penetration
guarantee. These fallbacks can still slow or freeze the *whole* world and become
expensive in crowded or repeated-impact situations. A 1-micrometre contact
envelope and support-feature fallback help solve near-touching contacts.

The planar polygons represent prisms with an explicit depth. Buoyancy is the
weight of the displaced air and liquid and acts through each displaced-volume
centroid. The liquid is a horizontal half-plane; clipping the polygon gives the
submerged area and centroid. Fluid density and flow velocity are parameters.
Pressure drag on each exposed edge uses a quadratic law and the rigid-body
velocity at that edge, including rotation. Two quadrature points apply local
quadratic-drag impulses sequentially, using the exact normal-speed decay with
frozen coefficient at each point to avoid explicit-Euler reversal at high
speeds. The splitting and frozen geometry are approximations. This empirical
pressure-drag model is not a fluid simulation: it does not model viscosity,
compressibility, wakes, splashes or waves, and is not a validated model at
100 km/s. The
seesaw scene uses air density 1.225 kg/m³, water density 1000 kg/m³ when
selected, friction 0.5, pressure coefficient 1.05, restitution 0.15, and solid
density 500 kg/m³; the coefficients are illustrative, not measured material
properties.

References:
- https://www.grc.nasa.gov/www/k-12/VirtualAero/BottleRocket/airplane/drageq.html
- https://openstax.org/books/college-physics-2e/pages/11-7-archimedes-principle
- https://interactivecomputergraphics.github.io/physics-simulation/examples/rigid_body_collision.html
- https://files.slembcke.net/misc/RigidBodies.pdf
- https://www.slideshare.net/slideshow/gpu-rigid-body-simulation-gdc-2013/20212240
- https://box2d.org/posts/2023/01/starting-box2d-3.0/
- https://www.gamedevs.org/uploads/continuous-collision-detection-and-physics.pdf
- https://box2d.org/files/ErinCatto_ContinuousCollision_GDC2013.pdf
- https://www.sfu.ca/~boal/211lecs/211lec4.pdf

## Remaining limitations

This is not yet a general, complete physics engine. It supports single convex
polygons of up to 16 vertices, not arbitrary concave or compound shapes. The
continuous collision handling still does not guarantee against tunneling in all
cases. In particular, initially touching or overlapping pairs are handled by
finite contact constraints rather than a complete continuous rotational query;
position projection can move shapes without a swept check. Floating-point
precision, simultaneous impacts, numerical tolerances and limited iterations
also affect correctness. Fixed iterations and position projection can introduce
energy and multi-contact errors. Only pin joints are implemented. There is no
full fluid solver. At most twelve physics steps are submitted per rendered frame,
so a 50 ms frame can process a full 20 Hz tick. Up to a quarter-second backlog
is retained, after which excess time is dropped. Real device compatibility,
Windows, integrated-GPU performance and long-term
stability are unverified. SwiftShader Vulkan images were inspected for the dry
and flooded seesaw, drag and reset, a 41-body world, a rotating impact, an
isolated 100 km/s impact, a 100 km/s two-body impact chain, and repeated bounces
between thin walls. These observations are not proof of general correctness.

Rebuild shader modules with a Vulkan GLSL compiler, then run
`python3 native/vulkan/physics/make_compute_headers.py`.
