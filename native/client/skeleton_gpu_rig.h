#pragma once

#include "client/raster.h"
#include "vulkan/physics/rigid_solver_data.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace gtc::native {
class SkeletonModel;
class SkeletonComparison;
struct ComparisonToeAttachment;

// Generations must be unique across every independent room sharing a renderer.
inline std::uint64_t nextPhysicsGeneration() {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1,std::memory_order_relaxed);
}

// Startup data for the experimental GPU solver.
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
                         const std::vector<std::array<double,16>>& maleBluePose,
                         const std::vector<std::array<double,16>>& femaleBluePose,
                         const std::vector<ComparisonToeAttachment>& maleSurfaceJoints,
                         const std::vector<ComparisonToeAttachment>& femaleSurfaceJoints,
                         const std::vector<ComparisonToeAttachment>& maleWhiteSurfaceJoints,
                         const std::vector<ComparisonToeAttachment>& femaleWhiteSurfaceJoints,
                         GpuSkeletonRig& out, std::string& error);
} // namespace gtc::native
