#include "vk_internal.h"
#include "../client/skeleton_gpu_rig.h"
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
    const auto& rig=*scene.physicsRig;
    if (!scene.physicsGeneration) return false;
    if (scene.physicsGeneration!=physicsGeneration_) {
        if (physicsGeneration_ && deviceWaitIdle(device_)!=VK_SUCCESS) return false;
        physics_.reset(); physicsMatrixSlots_.clear(); physicsPlan_.clear(); physicsStep_=0; physicsGeneration_=0;
        const auto getDevice=reinterpret_cast<PFN_vkGetDeviceProcAddr>(getInstanceProcAddr_(instance_,"vkGetDeviceProcAddr"));
        if (!physics_.create(physical_,device_,getDevice,getPhysicalDeviceMemoryProperties,
                             gpu_physics::rigid_solver_spirv,rig.bodies,rig.joints,rig.spheres,rig.exclusions,rig.batches)) {
            std::fprintf(stderr,"[vk] unable to initialize compute physics\n"); return false;
        }
        physicsGeneration_=scene.physicsGeneration;
    }
    if (scene.physicsSteps<physicsStep_) return false;
    auto& plan=physicsNextPlan_;
    plan.clear();
    plan.reserve(scene.batches.size()+2);
    plan.push_back((std::uint64_t(scene.maleSkinCount)<<32)|scene.femaleSkinCount);
    plan.push_back(scene.physicsLineView ? 1u : 0u);
    for (std::size_t i=0;i<scene.batches.size();++i)
        if (i<slots_.size() && slots_[i]>=0)
            plan.push_back((std::uint64_t(scene.batches[i].objectId)<<32)|std::uint32_t(slots_[i]));
    if (plan==physicsPlan_ && !physicsMatrixSlots_.empty()) return true;
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
        const bool maleLine=scene.physicsLineView && binding.objectId>=9001 && binding.objectId<9001+scene.maleSkinCount;
        const bool femaleLine=scene.physicsLineView && binding.objectId>=8001 && binding.objectId<8001+scene.femaleSkinCount;
        if (maleLine) slot=binding.objectId-9001;
        else if (femaleLine) slot=scene.maleSkinCount+binding.objectId-8001;
        else {
            const auto found=byObject.find(binding.objectId);
            if (found==byObject.end()) { std::fprintf(stderr,"[vk] missing physics batch %u\n",binding.objectId); return false; }
            slot=found->second;
        }
        if (slot>=kBatchSlots) return false;
        matrixSlots.push_back(slot);
        gpu_physics::SkinBinding skin;
        skin.localFromMesh=binding.localFromMesh;
        if (maleLine || femaleLine) {
            const auto& p=rig.bodies[binding.body].positionMass;
            skin.localFromMesh={1,0,0,0,0,1,0,0,0,0,1,0,-p[0],-p[1],-p[2],1};
        }
        skin.indices={binding.body,slot,0,0};
        bindings.push_back(skin);
    }
    if (matrixSlots!=physicsMatrixSlots_ || scene.physicsLineView!=physicsLineView_) {
        if (!physicsMatrixSlots_.empty() && deviceWaitIdle(device_)!=VK_SUCCESS) return false;
        std::array<VkDescriptorBufferInfo,kFramesInFlight> outputs{};
        for (int slot=0;slot<kFramesInFlight;++slot) outputs[slot]={frameBuffers_[slot],kSlotOffset,kSlotBlock};
        if (!physics_.createSkin(gpu_physics::rigid_skin_spirv,bindings,outputs)) {
            std::fprintf(stderr,"[vk] unable to initialize compute skinning\n"); return false;
        }
        physicsMatrixSlots_=std::move(matrixSlots);
        physicsLineView_=scene.physicsLineView;
    }
    physicsPlan_.swap(plan);
    return true;
}

bool Renderer::recordPhysics(const GpuScene& scene, VkCommandBuffer command, int frameSlot) {
    if (!scene.physicsRig) return true;
    if (physicsStep_==0 && scene.physicsSteps==0 && !physics_.projectInitialToes(command)) return false;
    while (physicsStep_<scene.physicsSteps) {
        if (!physics_.record(command,gpu_physics::kPhysicsStep,8,scene.physicsDragOrigin,scene.physicsDragDirection,
                             scene.physicsDragging,scene.physicsDragSequence)) return false;
        ++physicsStep_;
    }
    return physics_.recordSkin(command,std::size_t(frameSlot));
}
} // namespace gtc::native::detail
