#include "vk_internal.h"
#include "../client/planar_gpu_scene.h"
#include "physics/rigid_solver_spirv.h"
#include "physics/rigid_skin_spirv.h"
#include "physics/rigid_timing.h"
#include <cstdio>
#include <unordered_map>

namespace gtc::native::detail {

bool Renderer::preparePhysics(const GpuScene& scene) {
    // A different scene can be rendered while the room is hidden. Keep the
    // device state so returning to the room resumes instead of replaying every
    // elapsed step in one submission. A new generation replaces it safely.
    if (!scene.physicsRig) return true;
    if (scene.physicsWorld>=physicsWorlds_.size()) return false;
    auto& world=physicsWorlds_[scene.physicsWorld];
    const auto& rig=*scene.physicsRig;
    if (!scene.physicsGeneration) return false;
    if (scene.physicsGeneration!=world.generation) {
        if (world.generation && deviceWaitIdle(device_)!=VK_SUCCESS) return false;
        world.physics.reset(); world.matrixSlots.clear(); world.plan.clear(); world.step=0; world.generation=0;
        const auto getDevice=reinterpret_cast<PFN_vkGetDeviceProcAddr>(getInstanceProcAddr_(instance_,"vkGetDeviceProcAddr"));
        if (!world.physics.create(physical_,device_,getDevice,getPhysicalDeviceMemoryProperties,getPhysicalDeviceProperties,
                             gpu_physics::rigid_solver_spirv,rig.bodies,rig.joints,rig.vertices,rig.environment)) {
            std::fprintf(stderr,"[vk] unable to initialize compute physics\n"); return false;
        }
        world.generation=scene.physicsGeneration;
    }
    if (scene.physicsSteps<world.step) return false;
    auto& plan=world.nextPlan;
    plan.clear();
    plan.reserve(scene.batches.size()+2);
    plan.push_back((std::uint64_t(scene.maleSkinCount)<<32)|scene.femaleSkinCount);
    plan.push_back(scene.physicsLineView ? 1u : 0u);
    for (std::size_t i=0;i<scene.batches.size();++i)
        if (i<slots_.size() && slots_[i]>=0)
            plan.push_back((std::uint64_t(scene.batches[i].objectId)<<32)|std::uint32_t(slots_[i]));
    if (plan==world.plan && !world.matrixSlots.empty()) return true;
    std::unordered_map<std::uint32_t,std::uint32_t> byObject;
    byObject.reserve(scene.batches.size());
    for (std::size_t i=0;i<scene.batches.size();++i) {
        if (i<slots_.size() && slots_[i]>=0) byObject.emplace(scene.batches[i].objectId,std::uint32_t(slots_[i]));
    }
    std::vector<std::uint32_t> matrixSlots;
    std::vector<gpu_physics::SkinBinding> bindings;
    matrixSlots.reserve(rig.bindings.size()); bindings.reserve(rig.bindings.size());
    for (const auto& binding:rig.bindings) {
        std::uint32_t slot=0;
        const auto found=byObject.find(binding.objectId);
        if (found==byObject.end()) { std::fprintf(stderr,"[vk] missing physics batch %u\n",binding.objectId); return false; }
        slot=found->second;
        if (slot>=kBatchSlots) return false;
        matrixSlots.push_back(slot);
        gpu_physics::SkinBinding skin;
        skin.localFromMesh=binding.localFromMesh;
        skin.indices={binding.body,slot,0,0};
        bindings.push_back(skin);
    }
    if (matrixSlots!=world.matrixSlots || scene.physicsLineView!=world.lineView) {
        if (!world.matrixSlots.empty() && deviceWaitIdle(device_)!=VK_SUCCESS) return false;
        std::array<VkDescriptorBufferInfo,kFramesInFlight> outputs{};
        for (int slot=0;slot<kFramesInFlight;++slot) outputs[slot]={frameBuffers_[slot],kSlotOffset,kSlotBlock};
        if (!world.physics.createSkin(gpu_physics::rigid_skin_spirv,bindings,outputs)) {
            std::fprintf(stderr,"[vk] unable to initialize compute skinning\n"); return false;
        }
        world.matrixSlots=std::move(matrixSlots);
        world.lineView=scene.physicsLineView;
    }
    world.plan.swap(plan);
    return true;
}

bool Renderer::recordPhysics(const GpuScene& scene, VkCommandBuffer command, int frameSlot) {
    if (!scene.physicsRig) return true;
    if (scene.physicsWorld>=physicsWorlds_.size()) return false;
    auto& world=physicsWorlds_[scene.physicsWorld];
    if (world.step==0 && !world.physics.projectInitialJoints(command)) return false;
    while (world.step<scene.physicsSteps) {
        if (!world.physics.record(command,gpu_physics::kPhysicsStep,8,scene.physicsDragOrigin,scene.physicsDragDirection,
                             scene.physicsDragging,scene.physicsDragSequence)) return false;
        ++world.step;
    }
    return world.physics.recordSkin(command,std::size_t(frameSlot));
}
} // namespace gtc::native::detail
