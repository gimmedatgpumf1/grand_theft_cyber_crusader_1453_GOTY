# Vulkan rigid solver — work in progress

This is **not used by the game yet**. The room still uses Bullet. The compute
kernel, host runtime and buffers have not been run on a Vulkan device. This is
not yet a replacement for a production physics engine or proof of speed.

The prototype has parallel rigid-body integration, graph-coloured positional
joints, limited hinges, floor support and sphere-proxy body contacts. Ball
joints have no angular limits yet; sphere proxies and the current all-pairs
broad phase are approximations and need further work. Rig construction,
contact-proxy fitting, and the renderer/device integration are still missing.

`rigid_runtime` creates the compute pipeline and storage buffers, uploads the
initial data once and records fixed-step dispatches with GPU barriers. It is
intended for a queue that supports both graphics and compute, with body poses
consumed on the GPU by the renderer. Its caller must synchronize prior uses
before replacing or destroying resources. `rigid_solver_data.h` contains the
shared layouts and joint colouring. The shader's contact phases use a symmetric
exclusion bitset and per-body scratch space, so they need no float atomics.

The checked-in SPIR-V was compiled from `rigid_solver.comp` with glslang for
Vulkan. A normal client build needs no shader compiler. There is deliberately
no CPU fallback hidden behind this unfinished compute code.
