#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "../../vendor/vulkan/include/vulkan/vulkan.h"
#include "rigid_solver_data.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gtc::native::gpu_physics {

// GPU-resident compute pipeline, not yet wired into the room. Creation/upload
// happens once; record() only records device-side work. The owner must wait
// for all submitted uses before calling reset() or destroying the device.
class RigidRuntime {
public:
    RigidRuntime() = default;
    ~RigidRuntime() { reset(); }
    RigidRuntime(const RigidRuntime&) = delete;
    RigidRuntime& operator=(const RigidRuntime&) = delete;

    bool create(VkPhysicalDevice physical, VkDevice device,
                PFN_vkGetDeviceProcAddr getDeviceProcAddr,
                PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties,
                std::span<const std::uint32_t> shader,
                std::span<const Body> bodies, std::span<const Joint> joints,
                std::span<const std::array<float,4>> spheres,
                std::span<const std::uint32_t> exclusions,
                std::span<const JointBatch> batches);
    // command must be recording on a compute-capable queue, outside a render
    // pass. Call once per fixed step. The owner must serialize uses of this
    // state across submissions and apply a vertex-stage dependency before draw.
    bool record(VkCommandBuffer command, float dt, std::uint32_t iterations) const;
    VkBuffer bodyBuffer() const { return buffers_[0].buffer; }
    void reset();

private:
    struct Buffer { VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; };
    bool makeBuffer(Buffer& out, VkDeviceSize size, const void* data);
    void barrier(VkCommandBuffer command, VkPipelineStageFlags destination) const;
    void dispatch(VkCommandBuffer command, const Step& step) const;
    VkDevice device_=VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory_{};
    std::array<Buffer,4> buffers_{};
    VkDescriptorSetLayout setLayout_=VK_NULL_HANDLE;
    VkDescriptorPool pool_=VK_NULL_HANDLE;
    VkDescriptorSet set_=VK_NULL_HANDLE;
    VkPipelineLayout layout_=VK_NULL_HANDLE;
    VkPipeline pipeline_=VK_NULL_HANDLE;
    std::uint32_t bodyCount_=0;
    std::vector<JointBatch> batches_;

    PFN_vkCreateBuffer createBuffer_=nullptr;
    PFN_vkDestroyBuffer destroyBuffer_=nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements_=nullptr;
    PFN_vkAllocateMemory allocateMemory_=nullptr;
    PFN_vkFreeMemory freeMemory_=nullptr;
    PFN_vkBindBufferMemory bindBufferMemory_=nullptr;
    PFN_vkMapMemory mapMemory_=nullptr;
    PFN_vkUnmapMemory unmapMemory_=nullptr;
    PFN_vkCreateDescriptorSetLayout createDescriptorSetLayout_=nullptr;
    PFN_vkDestroyDescriptorSetLayout destroyDescriptorSetLayout_=nullptr;
    PFN_vkCreateDescriptorPool createDescriptorPool_=nullptr;
    PFN_vkDestroyDescriptorPool destroyDescriptorPool_=nullptr;
    PFN_vkAllocateDescriptorSets allocateDescriptorSets_=nullptr;
    PFN_vkUpdateDescriptorSets updateDescriptorSets_=nullptr;
    PFN_vkCreatePipelineLayout createPipelineLayout_=nullptr;
    PFN_vkDestroyPipelineLayout destroyPipelineLayout_=nullptr;
    PFN_vkCreateShaderModule createShaderModule_=nullptr;
    PFN_vkDestroyShaderModule destroyShaderModule_=nullptr;
    PFN_vkCreateComputePipelines createComputePipelines_=nullptr;
    PFN_vkDestroyPipeline destroyPipeline_=nullptr;
    PFN_vkCmdBindPipeline cmdBindPipeline_=nullptr;
    PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets_=nullptr;
    PFN_vkCmdPushConstants cmdPushConstants_=nullptr;
    PFN_vkCmdDispatch cmdDispatch_=nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier_=nullptr;
};

} // namespace gtc::native::gpu_physics
