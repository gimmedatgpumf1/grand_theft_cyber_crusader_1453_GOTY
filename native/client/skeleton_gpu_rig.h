#pragma once

#include "client/raster.h"
#include "vulkan/physics/rigid_solver_data.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gtc::native {
class SkeletonModel;
class SkeletonComparison;

// Startup data for the unfinished GPU solver. No Bullet objects are used.
// A binding's matrix maps the batch's original vertices into body-local space;
// the renderer will apply the GPU body's current orientation and position.
struct GpuSkeletonBinding {
    std::uint32_t objectId=0, body=0;
    std::array<float,16> localFromMesh{};
};
struct GpuSkeletonRig {
    std::vector<gpu_physics::Body> bodies;
    std::vector<gpu_physics::Joint> joints;
    std::vector<std::array<float,4>> spheres;
    std::vector<std::uint32_t> exclusions;
    std::vector<gpu_physics::JointBatch> batches;
    std::vector<GpuSkeletonBinding> bindings;
};

// Male and female are processed concurrently; each thread builds its white and
// blue figures in sequence because each source model owns mutable rig caches.
// This prepares static input only; simulation and rendering stay on the GPU.
bool buildGpuSkeletonRig(const SkeletonModel& male, Vec3 maleOrigin,
                         const SkeletonComparison& blueMale,
                         const SkeletonModel& female, Vec3 femaleOrigin,
                         const SkeletonComparison& blueFemale,
                         GpuSkeletonRig& out, std::string& error);
} // namespace gtc::native
