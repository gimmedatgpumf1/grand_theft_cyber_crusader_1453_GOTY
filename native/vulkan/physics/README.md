# Vulkan rigid solver — work in progress

This is **not used by the game yet**. The room still uses Bullet. The compute
kernel, host runtime and buffers have not been run on a Vulkan device. This is
not yet a replacement for a production physics engine or proof of speed.

The prototype has parallel rigid-body integration, graph-coloured positional
joints, limited hinges, floor support and sphere-proxy body contacts. Ball
joints have no angular limits yet; sphere proxies and the current all-pairs
broad phase are approximations and need further work. The initial skeleton-rig
builder is in `client/skeleton_gpu_rig.cpp`: it builds male and female data on
separate CPU threads at startup, including the blue toe proxies. This builder is
not wired into the room. Renderer/device integration is still missing.

`rigid_runtime` creates compute pipelines and storage buffers, uploads the
initial data once and records fixed-step dispatches with GPU barriers. The
separate `rigid_skin.comp` writes body transforms into renderer-owned matrix
slots on the GPU, without a CPU readback. These entry points are not yet called
by the renderer. The runtime requires a queue supporting graphics and compute;
the caller must synchronize prior uses before replacing or destroying resources.
`rigid_solver_data.h` contains the shared layouts and joint colouring. The
contact passes use a symmetric exclusion bitset and per-body scratch space, so
they need no float atomics.

The checked-in SPIR-V modules were compiled from the compute shaders with
glslang for Vulkan. A normal client build needs no shader compiler. There is deliberately
no CPU fallback hidden behind this unfinished compute code.
