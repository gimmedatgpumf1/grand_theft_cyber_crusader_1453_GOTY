#include "rigid_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace gtc::native::gpu_physics {

bool RigidRuntime::makeBuffer(Buffer& out, VkDeviceSize size, const void* data, VkBufferUsageFlags extraUsage) {
    VkBufferCreateInfo info{};
    info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size=std::max<VkDeviceSize>(size,16);
    info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|extraUsage;
    info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if (createBuffer_(device_,&info,nullptr,&out.buffer)!=VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    getBufferMemoryRequirements_(device_,out.buffer,&requirements);
    const VkMemoryPropertyFlags required=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    std::uint32_t selected=memory_.memoryTypeCount;
    for (std::uint32_t i=0;i<memory_.memoryTypeCount;++i) {
        if (!(requirements.memoryTypeBits&(1u<<i))) continue;
        const auto flags=memory_.memoryTypes[i].propertyFlags;
        if ((flags&required)!=required) continue;
        if (selected==memory_.memoryTypeCount || (flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) selected=i;
        if (flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) break;
    }
    if (selected==memory_.memoryTypeCount) return false;
    VkMemoryAllocateInfo allocation{};
    allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize=requirements.size;
    allocation.memoryTypeIndex=selected;
    if (allocateMemory_(device_,&allocation,nullptr,&out.memory)!=VK_SUCCESS) return false;
    if (bindBufferMemory_(device_,out.buffer,out.memory,0)!=VK_SUCCESS) return false;
    void* map=nullptr;
    if (mapMemory_(device_,out.memory,0,info.size,0,&map)!=VK_SUCCESS) return false;
    std::memset(map,0,static_cast<std::size_t>(info.size));
    if (data && size) std::memcpy(map,data,static_cast<std::size_t>(size));
    unmapMemory_(device_,out.memory);
    return true;
}

bool RigidRuntime::create(VkPhysicalDevice physical, VkDevice device,
                          PFN_vkGetDeviceProcAddr getDeviceProcAddr,
                          PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties,
                          PFN_vkGetPhysicalDeviceProperties getProperties,
                          std::span<const std::uint32_t> shader,
                          std::span<const Body> bodies, std::span<const Joint> joints,
                          std::span<const std::array<float,4>> vertices, const FluidEnvironment& environment) {
    reset();
    if (!physical || !device || !getDeviceProcAddr || !getMemoryProperties || !getProperties ||
        shader.size()<5 || shader[0]!=0x07230203u || bodies.empty() ||
        bodies.size()>std::numeric_limits<std::int32_t>::max() || joints.size()>std::numeric_limits<std::int32_t>::max()) return false;
    for (float v:environment.medium) if (!std::isfinite(v)) return false;
    for (float v:environment.velocity) if (!std::isfinite(v)) return false;
    if (environment.medium[0]<0 || environment.medium[1]<0) return false;
    for (const Body& b:bodies) {
        for (const auto* data:{&b.positionMass,&b.rotation,&b.linearVelocity,&b.angularVelocity,&b.inverseInertia,&b.material})
            for (float value:*data) if (!std::isfinite(value)) return false;
        if (b.positionMass[3]<0 || b.inverseInertia[2]<0 || b.material[0]<0 || b.material[1]<0 ||
            b.material[2]<0 || b.material[3]<0 || b.material[3]>1 ||
            (b.positionMass[3]>0 && (b.inverseInertia[2]<=0 || b.material[1]<=0))) return false;
        if (!b.shapes[1]) continue;
        const auto n=b.shapes[1];const auto start=std::size_t(b.shapes[0]);
        if (n<3 || n>16 || start+n>vertices.size()) return false;
        double area=0;
        for (std::uint32_t i=0;i<n;++i) {
            const auto& a=vertices[start+i];const auto& p=vertices[start+(i+1)%n];const auto& q=vertices[start+(i+2)%n];
            if (!std::isfinite(a[0]) || !std::isfinite(a[1])) return false;
            const double dx=double(p[0])-a[0],dy=double(p[1])-a[1];
            if (dx*dx+dy*dy<1e-16 || dx*(double(q[1])-p[1])-dy*(double(q[0])-p[0]) < -1e-10) return false;
            area+=double(a[0])*p[1]-double(a[1])*p[0];
        }
        if (!(area>1e-12)) return false;
    }
    for (const Joint& j:joints) if (j.bodies[0]>=bodies.size() || j.bodies[1]>=bodies.size() || j.bodies[0]==j.bodies[1]) return false;
    const auto count=bodies.size();
    // Scratch contains a four-word drag state and twelve words per unordered
    // pair. Reject only arithmetic/device resource limits.
    const std::uint64_t n=count;
    if (n*(n-1)>std::numeric_limits<std::uint32_t>::max()) return false;
    const std::uint64_t words=4+n*(n-1)/2*12;
    if (words>std::numeric_limits<std::uint32_t>::max()) return false;
    VkPhysicalDeviceProperties properties{};getProperties(physical,&properties);
    const VkDeviceSize sizes[4]={bodies.size_bytes(),joints.size_bytes(),vertices.size_bytes(),words*sizeof(std::uint32_t)};
    for (VkDeviceSize size:sizes) if (std::max<VkDeviceSize>(size,16)>properties.limits.maxStorageBufferRange) return false;
    device_=device;
#define LOAD(member, name) member=reinterpret_cast<decltype(member)>(getDeviceProcAddr(device_,name)); if (!member) { reset(); return false; }
    LOAD(createBuffer_,"vkCreateBuffer")
    LOAD(destroyBuffer_,"vkDestroyBuffer")
    LOAD(getBufferMemoryRequirements_,"vkGetBufferMemoryRequirements")
    LOAD(allocateMemory_,"vkAllocateMemory")
    LOAD(freeMemory_,"vkFreeMemory")
    LOAD(bindBufferMemory_,"vkBindBufferMemory")
    LOAD(mapMemory_,"vkMapMemory")
    LOAD(unmapMemory_,"vkUnmapMemory")
    LOAD(createDescriptorSetLayout_,"vkCreateDescriptorSetLayout")
    LOAD(destroyDescriptorSetLayout_,"vkDestroyDescriptorSetLayout")
    LOAD(createDescriptorPool_,"vkCreateDescriptorPool")
    LOAD(destroyDescriptorPool_,"vkDestroyDescriptorPool")
    LOAD(allocateDescriptorSets_,"vkAllocateDescriptorSets")
    LOAD(updateDescriptorSets_,"vkUpdateDescriptorSets")
    LOAD(createPipelineLayout_,"vkCreatePipelineLayout")
    LOAD(destroyPipelineLayout_,"vkDestroyPipelineLayout")
    LOAD(createShaderModule_,"vkCreateShaderModule")
    LOAD(destroyShaderModule_,"vkDestroyShaderModule")
    LOAD(createComputePipelines_,"vkCreateComputePipelines")
    LOAD(destroyPipeline_,"vkDestroyPipeline")
    LOAD(cmdBindPipeline_,"vkCmdBindPipeline")
    LOAD(cmdBindDescriptorSets_,"vkCmdBindDescriptorSets")
    LOAD(cmdPushConstants_,"vkCmdPushConstants")
    LOAD(cmdDispatch_,"vkCmdDispatch")
    LOAD(cmdDispatchIndirect_,"vkCmdDispatchIndirect")
    LOAD(cmdPipelineBarrier_,"vkCmdPipelineBarrier")
#undef LOAD
    getMemoryProperties(physical,&memory_);
    const void* data[4]={bodies.data(),joints.data(),vertices.data(),nullptr};
    for (std::size_t i=0;i<buffers_.size();++i)
        if (!makeBuffer(buffers_[i],sizes[i],data[i])) { reset(); return false; }

    std::array<VkDescriptorSetLayoutBinding,4> bindings{};
    for (std::uint32_t i=0;i<bindings.size();++i) {
        bindings[i].binding=i; bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount=1; bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount=static_cast<std::uint32_t>(bindings.size()); setInfo.pBindings=bindings.data();
    if (createDescriptorSetLayout_(device_,&setInfo,nullptr,&setLayout_)!=VK_SUCCESS) { reset(); return false; }
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,static_cast<std::uint32_t>(bindings.size())};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets=1; poolInfo.poolSizeCount=1; poolInfo.pPoolSizes=&poolSize;
    if (createDescriptorPool_(device_,&poolInfo,nullptr,&pool_)!=VK_SUCCESS) { reset(); return false; }
    VkDescriptorSetAllocateInfo setAllocation{};
    setAllocation.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocation.descriptorPool=pool_; setAllocation.descriptorSetCount=1; setAllocation.pSetLayouts=&setLayout_;
    if (allocateDescriptorSets_(device_,&setAllocation,&set_)!=VK_SUCCESS) { reset(); return false; }
    std::array<VkDescriptorBufferInfo,4> bufferInfos{};
    std::array<VkWriteDescriptorSet,4> writes{};
    for (std::uint32_t i=0;i<writes.size();++i) {
        bufferInfos[i]={buffers_[i].buffer,0,VK_WHOLE_SIZE};
        writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet=set_; writes[i].dstBinding=i; writes[i].descriptorCount=1;
        writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo=&bufferInfos[i];
    }
    updateDescriptorSets_(device_,static_cast<std::uint32_t>(writes.size()),writes.data(),0,nullptr);
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Step)};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount=1; layoutInfo.pSetLayouts=&setLayout_;
    layoutInfo.pushConstantRangeCount=1; layoutInfo.pPushConstantRanges=&range;
    if (createPipelineLayout_(device_,&layoutInfo,nullptr,&layout_)!=VK_SUCCESS) { reset(); return false; }
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize=shader.size_bytes(); moduleInfo.pCode=shader.data();
    VkShaderModule module=VK_NULL_HANDLE;
    if (createShaderModule_(device_,&moduleInfo,nullptr,&module)!=VK_SUCCESS) { reset(); return false; }
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; stage.module=module; stage.pName="main";
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage=stage; pipelineInfo.layout=layout_;
    const VkResult result=createComputePipelines_(device_,VK_NULL_HANDLE,1,&pipelineInfo,nullptr,&pipeline_);
    destroyShaderModule_(device_,module,nullptr);
    if (result!=VK_SUCCESS) { reset(); return false; }
    environment_=environment;
    bodyCount_=static_cast<std::uint32_t>(count);
    jointCount_=static_cast<std::uint32_t>(joints.size());
    return true;
}

void RigidRuntime::barrier(VkCommandBuffer command, VkPipelineStageFlags destination, bool indirectSource) const {
    VkMemoryBarrier memory{};
    memory.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memory.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;
    memory.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    VkPipelineStageFlags source=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (indirectSource) {
        source|=VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        memory.srcAccessMask|=VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }
    if (destination&VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT) memory.dstAccessMask|=VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    cmdPipelineBarrier_(command,source,destination,0,1,&memory,0,nullptr,0,nullptr);
}
bool RigidRuntime::projectInitialJoints(VkCommandBuffer command) const {
    return command && pipeline_;
}
bool RigidRuntime::record(VkCommandBuffer command, float dt, std::uint32_t iterations,
                          const std::array<float,4>& dragOrigin, const std::array<float,4>& dragDirection,
                          bool dragging, std::uint32_t dragSequence) const {
    if (!command || !pipeline_ || !(dt>0) || !std::isfinite(dt) || !iterations) return false;
    barrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    cmdBindPipeline_(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_);
    cmdBindDescriptorSets_(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout_,0,1,&set_,0,nullptr);
    Step step{}; step.dt=dt; step.first=bodyCount_; step.count=jointCount_;
    step.medium=environment_.medium; step.fluidVelocity=environment_.velocity;
    step.dragOrigin=dragOrigin; step.dragDirection=dragDirection;
    step.dragControl={dragging ? 1u : 0u,dragSequence,0u,0u};
    cmdPushConstants_(command,layout_,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(step),&step);
    cmdDispatch_(command,1,1,1);
    barrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
    return true;
}
bool RigidRuntime::createSkin(std::span<const std::uint32_t> shader,
                              std::span<const SkinBinding> bindings,
                              std::span<const VkDescriptorBufferInfo> outputs) {
    resetSkin();
    if (!device_ || !pipeline_ || shader.size()<5 || shader[0]!=0x07230203u ||
        bindings.empty() || bindings.size()>4194240u || outputs.empty() || outputs.size()>16) return false;
    std::uint32_t maxSlot=0;
    std::vector<std::uint32_t> slots;
    slots.reserve(bindings.size());
    for (const SkinBinding& binding:bindings) {
        if (binding.indices[0]>=bodyCount_) return false;
        for (float word:binding.localFromMesh) if (!std::isfinite(word)) return false;
        maxSlot=std::max(maxSlot,binding.indices[1]); slots.push_back(binding.indices[1]);
    }
    std::sort(slots.begin(),slots.end());
    if (std::adjacent_find(slots.begin(),slots.end())!=slots.end()) return false;
    for (const auto& output:outputs)
        if (!output.buffer || output.range==VK_WHOLE_SIZE || output.range<(VkDeviceSize(maxSlot)+1)*64) return false;
    if (!makeBuffer(skinBindings_,bindings.size_bytes(),bindings.data())) { resetSkin(); return false; }
    std::array<VkDescriptorSetLayoutBinding,3> descriptions{};
    for (std::uint32_t i=0;i<descriptions.size();++i) {
        descriptions[i].binding=i; descriptions[i].descriptorCount=1;
        descriptions[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptions[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount=static_cast<std::uint32_t>(descriptions.size()); setInfo.pBindings=descriptions.data();
    if (createDescriptorSetLayout_(device_,&setInfo,nullptr,&skinSetLayout_)!=VK_SUCCESS) { resetSkin(); return false; }
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,std::uint32_t(outputs.size()*3)};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets=std::uint32_t(outputs.size()); poolInfo.poolSizeCount=1; poolInfo.pPoolSizes=&poolSize;
    if (createDescriptorPool_(device_,&poolInfo,nullptr,&skinPool_)!=VK_SUCCESS) { resetSkin(); return false; }
    std::vector<VkDescriptorSetLayout> layouts(outputs.size(),skinSetLayout_);
    skinSets_.resize(outputs.size());
    VkDescriptorSetAllocateInfo allocation{};
    allocation.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocation.descriptorPool=skinPool_; allocation.descriptorSetCount=std::uint32_t(outputs.size());
    allocation.pSetLayouts=layouts.data();
    if (allocateDescriptorSets_(device_,&allocation,skinSets_.data())!=VK_SUCCESS) { resetSkin(); return false; }
    for (std::size_t i=0;i<outputs.size();++i) {
        const std::array<VkDescriptorBufferInfo,3> infos={VkDescriptorBufferInfo{buffers_[0].buffer,0,VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{skinBindings_.buffer,0,VK_WHOLE_SIZE},outputs[i]};
        std::array<VkWriteDescriptorSet,3> writes{};
        for (std::uint32_t j=0;j<writes.size();++j) {
            writes[j].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[j].dstSet=skinSets_[i];
            writes[j].dstBinding=j; writes[j].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[j].descriptorCount=1; writes[j].pBufferInfo=&infos[j];
        }
        updateDescriptorSets_(device_,std::uint32_t(writes.size()),writes.data(),0,nullptr);
    }
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(std::uint32_t)};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount=1; layoutInfo.pSetLayouts=&skinSetLayout_;
    layoutInfo.pushConstantRangeCount=1; layoutInfo.pPushConstantRanges=&range;
    if (createPipelineLayout_(device_,&layoutInfo,nullptr,&skinLayout_)!=VK_SUCCESS) { resetSkin(); return false; }
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize=shader.size_bytes(); moduleInfo.pCode=shader.data();
    VkShaderModule module=VK_NULL_HANDLE;
    if (createShaderModule_(device_,&moduleInfo,nullptr,&module)!=VK_SUCCESS) { resetSkin(); return false; }
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; stage.module=module; stage.pName="main";
    VkComputePipelineCreateInfo info{};
    info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO; info.stage=stage; info.layout=skinLayout_;
    const VkResult result=createComputePipelines_(device_,VK_NULL_HANDLE,1,&info,nullptr,&skinPipeline_);
    destroyShaderModule_(device_,module,nullptr);
    if (result!=VK_SUCCESS) { resetSkin(); return false; }
    skinCount_=std::uint32_t(bindings.size());
    return true;
}
bool RigidRuntime::recordSkin(VkCommandBuffer command, std::size_t frameSlot) const {
    if (!command || !skinPipeline_ || frameSlot>=skinSets_.size()) return false;
    barrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    cmdBindPipeline_(command,VK_PIPELINE_BIND_POINT_COMPUTE,skinPipeline_);
    cmdBindDescriptorSets_(command,VK_PIPELINE_BIND_POINT_COMPUTE,skinLayout_,0,1,&skinSets_[frameSlot],0,nullptr);
    cmdPushConstants_(command,skinLayout_,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(skinCount_),&skinCount_);
    cmdDispatch_(command,(skinCount_+63u)/64u,1,1);
    barrier(command,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
    return true;
}
void RigidRuntime::resetSkin() {
    if (device_) {
        if (skinPipeline_ && destroyPipeline_) destroyPipeline_(device_,skinPipeline_,nullptr);
        if (skinLayout_ && destroyPipelineLayout_) destroyPipelineLayout_(device_,skinLayout_,nullptr);
        if (skinPool_ && destroyDescriptorPool_) destroyDescriptorPool_(device_,skinPool_,nullptr);
        if (skinSetLayout_ && destroyDescriptorSetLayout_) destroyDescriptorSetLayout_(device_,skinSetLayout_,nullptr);
        if (skinBindings_.buffer && destroyBuffer_) destroyBuffer_(device_,skinBindings_.buffer,nullptr);
        if (skinBindings_.memory && freeMemory_) freeMemory_(device_,skinBindings_.memory,nullptr);
    }
    skinPipeline_=VK_NULL_HANDLE; skinLayout_=VK_NULL_HANDLE; skinPool_=VK_NULL_HANDLE;
    skinSetLayout_=VK_NULL_HANDLE; skinBindings_={}; skinSets_.clear(); skinCount_=0;
}
void RigidRuntime::reset() {
    resetSkin();
    // The caller must have retired all work referencing these resources.
    if (device_) {
        if (pipeline_ && destroyPipeline_) destroyPipeline_(device_,pipeline_,nullptr);
        if (layout_ && destroyPipelineLayout_) destroyPipelineLayout_(device_,layout_,nullptr);
        if (pool_ && destroyDescriptorPool_) destroyDescriptorPool_(device_,pool_,nullptr);
        if (setLayout_ && destroyDescriptorSetLayout_) destroyDescriptorSetLayout_(device_,setLayout_,nullptr);
        for (auto& buffer:buffers_) {
            if (buffer.buffer && destroyBuffer_) destroyBuffer_(device_,buffer.buffer,nullptr);
            if (buffer.memory && freeMemory_) freeMemory_(device_,buffer.memory,nullptr);
            buffer={};
        }
    }
    pipeline_=VK_NULL_HANDLE; layout_=VK_NULL_HANDLE; pool_=VK_NULL_HANDLE;
    setLayout_=VK_NULL_HANDLE; set_=VK_NULL_HANDLE; device_=VK_NULL_HANDLE;
    bodyCount_=0; jointCount_=0;
}

} // namespace gtc::native::gpu_physics
