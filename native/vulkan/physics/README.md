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
across multiple workgroups.

A 1/240 s semi-implicit step uses mass, planar moment of inertia, gravity,
unilateral contact impulses, Coulomb friction and point-to-point pin joints.
Contacts use at most two points per pair. The friction coefficient for a pair
is the geometric mean of the two surface coefficients. Restitution uses the
larger coefficient and is suppressed for contact speeds below two gravity
steps, to prevent gravity-induced resting jitter. Position projection reduces
joint drift and overlap without adding the projection directly to velocity.
All of these are finite-iteration approximations, not an exact simultaneous
constraint solution.

Before integration, non-overlapping convex pairs are also checked for predicted
impacts over the step. Constant-orientation motion uses swept separating-axis
intervals. For rotating polygons, conservative advancement uses separating-axis
gaps and a linear-plus-angular speed bound (up to 24 iterations). A predicted
impact adds a velocity constraint, restitution and friction; the impulse's
position and angle effects are weighted by the remaining time after impact.
These predictions assume unchanged velocities during the step. They are not an
event-ordered continuous solver: other contacts, joints and forces can change
the motion, and convergence is limited. Impacts can still be missed or occur
at the wrong time, especially in coupled or rapidly rotating configurations.

The planar polygons represent prisms with an explicit depth. Buoyancy is the
weight of the displaced air and liquid and acts through each displaced-volume
centroid. The liquid is a horizontal half-plane; clipping the polygon gives the
submerged area and centroid. Fluid density and flow velocity are parameters.
Pressure drag on each exposed edge uses a quadratic law and the rigid-body
velocity at that edge, including rotation; two-point quadrature integrates its
force and torque. This is an empirical pressure-drag approximation, not a
fluid simulation: it does not model viscosity, wakes, splashes or waves. The
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

## Remaining limitations

This is not yet a general, complete physics engine. It has a 24-body limit per
world and supports single convex polygons of up to 16 vertices, not arbitrary
concave or compound shapes. The approximate continuous collision handling does
not guarantee against tunneling. Fixed iterations and position projection can
introduce energy and multi-contact errors. Only pin joints are implemented.
There is no full fluid solver. At most eight physics steps are submitted per
rendered frame; up to a quarter-second backlog is retained, after which excess
time is dropped. The workgroup uses close to the Vulkan minimum shared-memory
limit; real device compatibility, Windows, integrated-GPU performance and
long-term stability are unverified. SwiftShader Vulkan images were inspected for
the dry and flooded seesaw, drag, reset, and two fast-impact scenarios; these
observations are not proof of general correctness.

Rebuild shader modules with a Vulkan GLSL compiler, then run
`python3 native/vulkan/physics/make_compute_headers.py`.
