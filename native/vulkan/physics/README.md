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
Contact search uses four GPU lanes per body, conservative world-space bounds,
and a parallel bitset broad phase to avoid repeatedly searching distant proxies.
It runs four contact searches per eight joint iterations. Independent joint
islands execute two coloured joint/floor passes per workgroup dispatch, using
workgroup barriers between colours; cross-island contacts remain global. This
reduces dispatch overhead, but limits parallelism when there are few islands;
its effect on integrated-GPU frame rate is not yet measured. An ordinary
per-colour dispatch path remains for unsupported schedules. Fewer contact
iterations reduce work but can reduce contact convergence. The solver dissipates
velocity from projection corrections and uses a non-bouncing floor. Floor-supported
joint islands that move slowly across two one-second windows sleep together on
the GPU; a nearby moving island can wake them, and resetting R rebuilds the rig.
Sleeping bodies skip joint and contact work. The sleep thresholds and wake-up
heuristic are approximations, not a general-purpose contact-island sleep system.
The broad phase still scans all body pairs; it and the overlapping-proxy exclusions
need further work. Limits, collisions, settling and body stability have not been
fully validated. The current approximation can produce contorted poses. Replacing
or expanding this solver requires further visual and physical validation.

The room path was inspected using SwiftShader's software Vulkan implementation,
not a physical GPU. Windows/MSVC, integrated-GPU compatibility and performance
have not been verified; no speed or frame-rate claim is made. GPU simulation
steps are capped at two per rendered frame to avoid a catch-up spiral; below
60 rendered frames per second the simulation runs slower than wall-clock time.

`rigid_runtime` owns compute pipelines and buffers. The renderer requires a
queue with graphics and compute support, dispatches fixed steps, and writes the
resulting matrices directly into the per-frame graphics buffer. Synchronization
is required before replacing or destroying these resources. `rigid_solver_data.h`
contains the shared layouts and constraint colouring; the contact passes use a
symmetric exclusion bitset and per-body scratch space instead of float atomics.

The checked-in SPIR-V modules are compiled from the compute shaders with
glslang for Vulkan. `make_compute_headers.py` regenerates the embedded C++
headers from those modules. Client builds do not require a shader compiler.
