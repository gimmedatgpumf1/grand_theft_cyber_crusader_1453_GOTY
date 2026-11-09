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

// GPU-resident compute pipeline. Creation/upload happens once; record()
// only records device-side work. The owner must wait
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
                std::span<const std::array<float,4>> vertices, const FluidEnvironment& environment);
    // command must be recording on a compute-capable queue, outside a render
    // pass. Call once per fixed step. The owner must serialize uses of this
    // state across submissions and apply a vertex-stage dependency before draw.
    bool record(VkCommandBuffer command, float dt, std::uint32_t iterations,
                const std::array<float,4>& dragOrigin, const std::array<float,4>& dragDirection,
                bool dragging, std::uint32_t dragSequence) const;
    // Establish the constrained pose before the first simulation step.
    bool projectInitialJoints(VkCommandBuffer command) const;
    // Output buffers are renderer-owned storage-buffer ranges, one per
    // in-flight frame. Retire previous uses before replacing these bindings.
    bool createSkin(std::span<const std::uint32_t> shader,
                    std::span<const SkinBinding> bindings,
                    std::span<const VkDescriptorBufferInfo> outputs);
    bool recordSkin(VkCommandBuffer command, std::size_t frameSlot) const;
    VkBuffer bodyBuffer() const { return buffers_[0].buffer; }
    void reset();

private:
    struct Buffer { VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; };
    bool makeBuffer(Buffer& out, VkDeviceSize size, const void* data, VkBufferUsageFlags extraUsage=0);
    void resetSkin();
    void barrier(VkCommandBuffer command, VkPipelineStageFlags destination, bool indirectSource=false) const;
    VkDevice device_=VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory_{};
    std::array<Buffer,4> buffers_{};
    VkDescriptorSetLayout setLayout_=VK_NULL_HANDLE;
    VkDescriptorPool pool_=VK_NULL_HANDLE;
    VkDescriptorSet set_=VK_NULL_HANDLE;
    VkPipelineLayout layout_=VK_NULL_HANDLE;
    VkPipeline pipeline_=VK_NULL_HANDLE;
    std::uint32_t bodyCount_=0, jointCount_=0;
    FluidEnvironment environment_{};
    Buffer skinBindings_{};
    VkDescriptorSetLayout skinSetLayout_=VK_NULL_HANDLE;
    VkDescriptorPool skinPool_=VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> skinSets_;
    VkPipelineLayout skinLayout_=VK_NULL_HANDLE;
    VkPipeline skinPipeline_=VK_NULL_HANDLE;
    std::uint32_t skinCount_=0;

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
    PFN_vkCmdDispatchIndirect cmdDispatchIndirect_=nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier_=nullptr;
};

} // namespace gtc::native::gpu_physics
