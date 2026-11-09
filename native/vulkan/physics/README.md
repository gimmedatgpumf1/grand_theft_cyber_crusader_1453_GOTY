# Planar Vulkan rigid-body prototype

The old three-dimensional ragdoll solver and unused Bullet sources have been removed.
Historical Bullet-based scripts and tests still need migration. Press P and then 2
for the planar room; R resets it. The skeleton room no longer has R-mode
ragdolls. The planar bodies and transforms remain on the GPU: there is no
per-frame physics readback or CPU pixel upload. Dragging applies a damped spring
at the picked point, with GPU-side picking. The seesaw axle is a generic pin
joint; its triangular support and floor are static collision polygons (the
slender axle is a visual pin).

Each body has mass, planar moment of inertia, linear and angular velocity. A
fixed 1/240 s semi-implicit step integrates gravity, finds overlaps between
convex polygons, and applies sequential equal-and-opposite impulses. Point
velocity is v + ω×r. Normal impulses are unilateral, and tangential impulses
are clamped to ±μ times the accumulated normal impulse; μ is the geometric mean
of the two bodies' coefficients. The pin solves its two point-velocity
constraints. Separate position projection reduces accumulated overlap and
pin drift without directly adding that correction to kinetic velocity. This
is an iterative approximation to constrained Newton–Euler dynamics, not an
exact solution of the simultaneous contact problem.

References:
- https://interactivecomputergraphics.github.io/physics-simulation/examples/rigid_body_collision.html
- https://files.slembcke.net/misc/RigidBodies.pdf
- https://web.cels.anl.gov/~anitescu/Presentations/2003/mihaiICIAMJul03.pdf

Limitations: this is a small, serial-per-world GPU prototype, not a parallel
large-world solver. It permits up to 16 bodies and 8 vertices per convex
polygon. Contacts are discrete (no continuous collision detection), perfectly
inelastic, and bounded to two points per pair. Projection and finite iterations
are approximate; tunnelling, accumulated energy errors, and difficult
multi-contact configurations are not guaranteed solved. When rendering below
30 FPS, the per-frame step limit deliberately slows simulated time.
The 2D scene has been inspected using SwiftShader Vulkan, not Windows or an
integrated GPU. Frame rate and long-term stability are not established.

Rebuild the shader modules with a Vulkan GLSL compiler, then run
`python3 native/vulkan/physics/make_compute_headers.py`.
