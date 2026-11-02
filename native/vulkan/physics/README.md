# Vulkan rigid solver — work in progress

This is **not used by the game yet**. The game still uses Bullet. Do not treat
this shader as a complete replacement: it has positional joint anchors and
floor support points, but not joint angular limits, bone-to-bone collision,
rig construction, or rendering integration. It has not run on a Vulkan GPU.

`rigid_solver.comp` is a parallel position-based rigid-body kernel. Its host
layouts and joint graph colouring are in `rigid_solver_data.h`. At each fixed
step the future Vulkan host must dispatch predict, then joint-colour and floor
passes (repeated for the desired solver iterations), then velocity update.
Each dispatch needs a compute-write to compute-read/write barrier before the
next one. Body poses must remain on the device and be consumed by the renderer;
the production path must not read them back to the CPU every frame.

The checked-in SPIR-V was compiled from the GLSL using glslang for Vulkan.
A normal client build does not need a shader compiler. There is deliberately no
CPU fallback hidden behind this unfinished compute code.
