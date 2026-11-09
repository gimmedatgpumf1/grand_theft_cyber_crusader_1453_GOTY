#pragma once

#include "client/raster.h"
#include "vulkan/physics/rigid_solver_data.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace gtc::native {
// Generations must be unique across every independent room sharing a renderer.
inline std::uint64_t nextPhysicsGeneration() {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1,std::memory_order_relaxed);
}

// Startup data for the planar GPU solver.
// A binding's matrix maps the batch's original vertices into body-local space;
// the renderer will apply the GPU body's current orientation and position.
struct GpuPlanarBinding {
    std::uint32_t objectId=0, body=0;
    std::array<float,16> localFromMesh{};
};
struct GpuPlanarRig {
    gpu_physics::FluidEnvironment environment;
    std::vector<gpu_physics::Body> bodies;
    std::vector<gpu_physics::Joint> joints;
    std::vector<std::array<float,4>> vertices;
    std::vector<GpuPlanarBinding> bindings;
};

} // namespace gtc::native
