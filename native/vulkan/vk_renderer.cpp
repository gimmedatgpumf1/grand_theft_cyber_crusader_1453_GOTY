// The frame loop for the Vulkan backend: targets, textures, vertex upload, one
// recorded pass, one readback. Kept intentional and synchronous -- the frame is
// finished before this function returns, because the result is a NativeFrame
// the rest of the client expects to own.

#include "vk_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace gtc::native {
namespace detail {

bool Renderer::submitNow(const std::function<void(VkCommandBuffer)>& body) {
    VkCommandBuffer buffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = commandPool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    if (allocateCommandBuffers(device_, &allocation, &buffer) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (beginCommandBuffer(buffer, &begin) != VK_SUCCESS) return false;
    body(buffer);
    if (endCommandBuffer(buffer) != VK_SUCCESS) return false;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &buffer;
    resetFences(device_, 1, &fence_);
    if (queueSubmit(queue_, 1, &submit, fence_) != VK_SUCCESS) return false;
    const VkResult waited = waitForFences(device_, 1, &fence_, VK_TRUE, 5000000000ull);
    // The command buffer came from a pool with reset support, so it can go back
    // to the pool rather than being freed one allocation at a time.
    resetCommandBuffer(buffer, 0);
    return waited == VK_SUCCESS;
}

bool Renderer::createRenderPass() {
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = colourFormat_;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // Undefined contents on entry: the colour attachment is always drawn from a
    // clear, so nothing needs to be preserved between frames, and the render
    // pass leaves it ready to be copied out.
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    attachments[1].format = depthFormat_;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colourReference{};
    colourReference.attachment = 0;
    colourReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colourReference;
    subpass.pDepthStencilAttachment = &depthReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    if (createRenderPassFn(device_, &info, nullptr, &renderPass_) != VK_SUCCESS) return false;
    return true;
}

void Renderer::destroyTargets() {
    if (target_.framebuffer) destroyFramebuffer(device_, target_.framebuffer, nullptr);
    if (target_.colourView) destroyImageView(device_, target_.colourView, nullptr);
    if (target_.depthView) destroyImageView(device_, target_.depthView, nullptr);
    if (target_.colour) destroyImage(device_, target_.colour, nullptr);
    if (target_.depth) destroyImage(device_, target_.depth, nullptr);
    if (target_.readback) destroyBuffer(device_, target_.readback, nullptr);
    if (target_.readbackMemory) freeMemory(device_, target_.readbackMemory, nullptr);
    if (target_.colourMemory) freeMemory(device_, target_.colourMemory, nullptr);
    if (target_.depthMemory) freeMemory(device_, target_.depthMemory, nullptr);
    target_ = Target{};
}

bool Renderer::createTargets(int width, int height) {
    destroyTargets();
    VkPhysicalDeviceMemoryProperties memory{};
    getPhysicalDeviceMemoryProperties(physical_, &memory);
    const auto pick = [&](std::uint32_t bits, VkMemoryPropertyFlags wanted) {
        for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
            if ((bits & (1u << index)) == 0) continue;
            if ((memory.memoryTypes[index].propertyFlags & wanted) == wanted) return index;
        }
        return memory.memoryTypeCount; // nothing matched
    };
    const auto makeImage = [&](VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& allocation) {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (createImage(device_, &info, nullptr, &image) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements{};
        getImageMemoryRequirements(device_, image, &requirements);
        const std::uint32_t type = pick(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type == memory.memoryTypeCount) return false;
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;
        if (this->allocateMemory(device_, &allocate, nullptr, &allocation) != VK_SUCCESS) return false;
        return bindImageMemory(device_, image, allocation, 0) == VK_SUCCESS;
    };

    const VkImageUsageFlags colourUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!makeImage(colourFormat_, colourUsage, target_.colour, target_.colourMemory)) return false;
    if (!makeImage(depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, target_.depth, target_.depthMemory)) {
        return false;
    }

    VkImageViewCreateInfo colourView{};

    colourView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colourView.image = target_.colour;
    colourView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colourView.format = colourFormat_;
    colourView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colourView.subresourceRange.levelCount = 1;
    colourView.subresourceRange.layerCount = 1;
    if (createImageView(device_, &colourView, nullptr, &target_.colourView) != VK_SUCCESS) return false;

    VkImageViewCreateInfo depthView{};

    depthView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthView.image = target_.depth;
    depthView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthView.format = depthFormat_;
    depthView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthView.subresourceRange.levelCount = 1;
    depthView.subresourceRange.layerCount = 1;
    if (createImageView(device_, &depthView, nullptr, &target_.depthView) != VK_SUCCESS) return false;

    const VkImageView views[2] = {target_.colourView, target_.depthView};
    VkFramebufferCreateInfo framebuffer{};
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = renderPass_;
    framebuffer.attachmentCount = 2;
    framebuffer.pAttachments = views;
    framebuffer.width = static_cast<std::uint32_t>(width);
    framebuffer.height = static_cast<std::uint32_t>(height);
    framebuffer.layers = 1;
    if (createFramebuffer(device_, &framebuffer, nullptr, &target_.framebuffer) != VK_SUCCESS) return false;

    VkBufferCreateInfo readback{};

    readback.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    readback.size = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
    readback.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    readback.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (createBuffer(device_, &readback, nullptr, &target_.readback) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    getBufferMemoryRequirements(device_, target_.readback, &requirements);
    const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const std::uint32_t type = pick(requirements.memoryTypeBits, wanted);
    if (type == memory.memoryTypeCount) return false;
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    if (this->allocateMemory(device_, &allocate, nullptr, &target_.readbackMemory) != VK_SUCCESS) return false;
    if (bindBufferMemory(device_, target_.readback, target_.readbackMemory, 0) != VK_SUCCESS) return false;
    if (mapMemory(device_, target_.readbackMemory, 0, VK_WHOLE_SIZE, 0, &target_.readbackMap) != VK_SUCCESS) return false;

    target_.width = width;
    target_.height = height;
    return true;
}

bool Renderer::createTexture(const Texture& texture, TextureSlot& slot) {
    const int width = texture.width;
    const int height = texture.height;
    if (width <= 0 || height <= 0 || texture.pixels.empty()) return false;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (createImage(device_, &info, nullptr, &slot.image) != VK_SUCCESS) return false;

    VkPhysicalDeviceMemoryProperties memory{};
    getPhysicalDeviceMemoryProperties(physical_, &memory);
    VkMemoryRequirements imageRequirements{};
    getImageMemoryRequirements(device_, slot.image, &imageRequirements);
    std::uint32_t type = memory.memoryTypeCount;
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        if ((imageRequirements.memoryTypeBits & (1u << index)) == 0) continue;
        if ((memory.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0) continue;
        type = index;
        break;
    }
    if (type == memory.memoryTypeCount) return false;
    VkMemoryAllocateInfo imageAllocation{};
    imageAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAllocation.allocationSize = imageRequirements.size;
    imageAllocation.memoryTypeIndex = type;
    if (allocateMemory(device_, &imageAllocation, nullptr, &slot.memory) != VK_SUCCESS) return false;
    if (bindImageMemory(device_, slot.image, slot.memory, 0) != VK_SUCCESS) return false;

    // The pixels go through a host-visible staging buffer, because the image is
    // device-local and cannot be written from here directly.
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* stagingMap = nullptr;
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = bytes;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (createBuffer(device_, &stagingInfo, nullptr, &staging) != VK_SUCCESS) return false;
    VkMemoryRequirements stagingRequirements{};
    getBufferMemoryRequirements(device_, staging, &stagingRequirements);
    const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    std::uint32_t stagingType = memory.memoryTypeCount;
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        if ((stagingRequirements.memoryTypeBits & (1u << index)) == 0) continue;
        if ((memory.memoryTypes[index].propertyFlags & wanted) != wanted) continue;
        stagingType = index;
        break;
    }
    if (stagingType == memory.memoryTypeCount) return false;
    VkMemoryAllocateInfo stagingAllocation{};
    stagingAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocation.allocationSize = stagingRequirements.size;
    stagingAllocation.memoryTypeIndex = stagingType;
    if (allocateMemory(device_, &stagingAllocation, nullptr, &stagingMemory) != VK_SUCCESS) return false;
    if (bindBufferMemory(device_, staging, stagingMemory, 0) != VK_SUCCESS) return false;
    if (mapMemory(device_, stagingMemory, 0, VK_WHOLE_SIZE, 0, &stagingMap) != VK_SUCCESS) return false;
    std::memcpy(stagingMap, texture.pixels.data(), static_cast<std::size_t>(bytes));
    unmapMemory(device_, stagingMemory);

    const bool copied = submitNow([&](VkCommandBuffer buffer) {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = slot.image;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        pipelineBarrier(buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                        0, nullptr, 0, nullptr, 1, &toTransfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        copyBufferToImage(buffer, staging, slot.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkImageMemoryBarrier toShader{};
        toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.image = slot.image;
        toShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toShader.subresourceRange.levelCount = 1;
        toShader.subresourceRange.layerCount = 1;
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        pipelineBarrier(buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                        0, nullptr, 0, nullptr, 1, &toShader);
    });
    destroyBuffer(device_, staging, nullptr);
    freeMemory(device_, stagingMemory, nullptr);
    if (!copied) return false;

    VkImageViewCreateInfo view{};

    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = slot.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    if (createImageView(device_, &view, nullptr, &slot.view) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo allocation{};

    allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocation.descriptorPool = descriptorPool_;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &textureLayout_;
    if (allocateDescriptorSets(device_, &allocation, &slot.set) != VK_SUCCESS) return false;

    VkDescriptorImageInfo image{};
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.imageView = slot.view;
    image.sampler = sampler_;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = slot.set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    updateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

TextureSlot& Renderer::textureFor(const Material& material) {
    const bool usable = material.texture && !material.texture->pixels.empty() &&
        (material.kind == Material::Kind::Skin || material.kind == Material::Kind::Hair ||
         material.kind == Material::Kind::Cloth || material.kind == Material::Kind::Floor ||
         material.kind == Material::Kind::Textured);
    if (!usable) return white_;
    auto found = textures_.find(material.texture);
    if (found != textures_.end()) return found->second;
    TextureSlot slot;
    if (!createTexture(*material.texture, slot)) {
        // A texture that will not upload is not a reason to stop drawing: the
        // batch keeps its flat colour, which is what the CPU path does when a
        // material has no image.
        return white_;
    }
    return textures_.emplace(material.texture, slot).first->second;
}

bool Renderer::uploadVertices(const GpuScene& scene) {
    std::size_t total = 0;
    for (const GpuBatch& batch : scene.batches) total += batch.vertices.size();
    firstVertex_.assign(scene.batches.size(), 0);
    if (total == 0) return true;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(total) * sizeof(GpuVertex);
    if (bytes > vertexCapacity_) {
        if (vertexBuffer_) destroyBuffer(device_, vertexBuffer_, nullptr);
        if (vertexMemory_) freeMemory(device_, vertexMemory_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
        vertexMemory_ = VK_NULL_HANDLE;
        vertexMap_ = nullptr;
        vertexCapacity_ = 0;
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = bytes;
        info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (createBuffer(device_, &info, nullptr, &vertexBuffer_) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements{};
        getBufferMemoryRequirements(device_, vertexBuffer_, &requirements);
        VkPhysicalDeviceMemoryProperties memory{};
        getPhysicalDeviceMemoryProperties(physical_, &memory);
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        std::uint32_t type = memory.memoryTypeCount;
        for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
            if ((requirements.memoryTypeBits & (1u << index)) == 0) continue;
            if ((memory.memoryTypes[index].propertyFlags & wanted) != wanted) continue;
            type = index;
            break;
        }
        if (type == memory.memoryTypeCount) return false;
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        if (allocateMemory(device_, &allocation, nullptr, &vertexMemory_) != VK_SUCCESS) return false;
        if (bindBufferMemory(device_, vertexBuffer_, vertexMemory_, 0) != VK_SUCCESS) return false;
        if (mapMemory(device_, vertexMemory_, 0, VK_WHOLE_SIZE, 0, &vertexMap_) != VK_SUCCESS) return false;
        vertexCapacity_ = bytes;
    }
    char* cursor = static_cast<char*>(vertexMap_);
    std::uint32_t running = 0;
    for (std::size_t index = 0; index < scene.batches.size(); ++index) {
        const std::vector<GpuVertex>& vertices = scene.batches[index].vertices;
        firstVertex_[index] = running;
        if (!vertices.empty()) {
            std::memcpy(cursor + static_cast<VkDeviceSize>(running) * sizeof(GpuVertex),
                        vertices.data(), vertices.size() * sizeof(GpuVertex));
        }
        running += static_cast<std::uint32_t>(vertices.size());
    }
    uploadedVertices_ = total;
    return true;
}

bool Renderer::draw(const GpuScene& scene, NativeFrame& frame) {
    if (!ready_) return false;
    const int width = std::max(1, scene.width);
    const int height = std::max(1, scene.height);
    if (width != target_.width || height != target_.height) {
        if (!createTargets(width, height)) return false;
    }
    if (white_.set == VK_NULL_HANDLE) {
        // One white pixel stands in for "this material has no image", so the
        // shader can sample unconditionally and stay a single module.
        Texture white;
        white.width = 1;
        white.height = 1;
        white.pixels = {255, 255, 255, 255};
        if (!createTexture(white, white_)) return false;
    }

    // Geometry only needs to travel when it actually changed: the world is
    // static between rebuilds, and the per-batch matrices ride in the push
    // constant block.
    std::uint64_t signature = 1469598103934665603ull;
    std::size_t totalVertices = 0;
    for (const GpuBatch& batch : scene.batches) {
        signature = (signature ^ batch.revision) * 1099511628211ull;
        signature = (signature ^ batch.vertices.size()) * 1099511628211ull;
        totalVertices += batch.vertices.size();
    }
    if (!uploadedValid_ || signature != uploadedSignature_) {
        if (!uploadVertices(scene)) return false;
        uploadedSignature_ = signature;
        uploadedValid_ = true;
    } else if (totalVertices == 0) {
        firstVertex_.assign(scene.batches.size(), 0);
    }
    // The offsets are indexed by batch below, so they are made to fit the batch
    // list that is actually being recorded rather than the one that was
    // uploaded.
    if (firstVertex_.size() != scene.batches.size()) {
        firstVertex_.assign(scene.batches.size(), 0);
        std::uint32_t running = 0;
        for (std::size_t index = 0; index < scene.batches.size(); ++index) {
            firstVertex_[index] = running;
            running += static_cast<std::uint32_t>(scene.batches[index].vertices.size());
        }
    }

    // Textures are uploaded before recording, because each upload submits its
    // own command buffer and commands cannot be nested.
    std::vector<VkDescriptorSet> textureSets(scene.batches.size(), VK_NULL_HANDLE);
    for (std::size_t index = 0; index < scene.batches.size(); ++index) {
        if (scene.batches[index].vertices.empty()) continue;
        textureSets[index] = textureFor(scene.batches[index].material).set;
    }

    const Matrix4 projection = projectionFor(scene);
    projection.columns(frameMap_->projection);
    // The eye point is the inverse of the view matrix applied to the origin;
    // for a rigid view that is -(R transposed) * translation.
    const std::array<double, 16>& view = scene.viewMatrix;
    const double tx = view[12], ty = view[13], tz = view[14];
    frameMap_->eye[0] = static_cast<float>(-(view[0] * tx + view[1] * ty + view[2] * tz));
    frameMap_->eye[1] = static_cast<float>(-(view[4] * tx + view[5] * ty + view[6] * tz));
    frameMap_->eye[2] = static_cast<float>(-(view[8] * tx + view[9] * ty + view[10] * tz));
    frameMap_->eye[3] = 1.0f;

    if (resetCommandBuffer(command_, 0) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (beginCommandBuffer(command_, &begin) != VK_SUCCESS) return false;

    // The vertex and uniform buffers were written from the host this frame.
    // Host writes are made available to the device by the submission itself, so
    // there is deliberately no barrier here: HOST is not a pipeline stage, and
    // passing it to vkCmdPipelineBarrier is invalid usage -- one that a lenient
    // driver ignores and a strict one does not.
    //
    // The only device-side dependency is the readback buffer: last frame's
    // transfer wrote it, this frame's transfer writes it again.
    VkBufferMemoryBarrier reuse{};
    reuse.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    reuse.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    reuse.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    reuse.buffer = target_.readback;
    reuse.offset = 0;
    reuse.size = VK_WHOLE_SIZE;
    reuse.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    reuse.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 1, &reuse, 0, nullptr);

    VkClearValue clears[2]{};
    clears[0].color.float32[0] = static_cast<float>(displayValue(scene.background.r));
    clears[0].color.float32[1] = static_cast<float>(displayValue(scene.background.g));
    clears[0].color.float32[2] = static_cast<float>(displayValue(scene.background.b));
    clears[0].color.float32[3] = 1.0f;
    clears[1].depthStencil.depth = 1.0f;
    clears[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo pass{};

    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = renderPass_;
    pass.framebuffer = target_.framebuffer;
    pass.renderArea.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    pass.clearValueCount = 2;
    pass.pClearValues = clears;
    beginRenderPass(command_, &pass, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.maxDepth = 1.0f;
    setViewport(command_, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    setScissor(command_, 0, 1, &scissor);

    bindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    bindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &frameSet_, 0, nullptr);
    const VkDeviceSize offset = 0;
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        bindVertexBuffers(command_, 0, 1, &vertexBuffer_, &offset);
    }

    for (std::size_t index = 0; index < scene.batches.size(); ++index) {
        const GpuBatch& batch = scene.batches[index];
        if (batch.vertices.empty()) continue;
        const VkDescriptorSet textureSet = textureSets[index] != VK_NULL_HANDLE ? textureSets[index] : white_.set;
        bindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 1, 1, &textureSet, 0, nullptr);
        PushConstants push{};
        for (int word = 0; word < 16; ++word) push.model[word] = static_cast<float>(batch.modelMatrix[word]);
        push.colour[0] = static_cast<float>(batch.material.color.r);
        push.colour[1] = static_cast<float>(batch.material.color.g);
        push.colour[2] = static_cast<float>(batch.material.color.b);
        push.colour[3] = static_cast<float>(batch.material.opacity);
        const bool usable = batch.material.texture && !batch.material.texture->pixels.empty() &&
            (batch.material.kind == Material::Kind::Skin || batch.material.kind == Material::Kind::Hair ||
             batch.material.kind == Material::Kind::Cloth || batch.material.kind == Material::Kind::Floor ||
             batch.material.kind == Material::Kind::Textured);
        push.flags[0] = usable ? 1.0f : 0.0f;
        push.flags[1] = (batch.material.constantLighting || batch.material.unlit) ? 1.0f : 0.0f;
        pushConstants(command_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(PushConstants), &push);
        drawFn(command_, static_cast<std::uint32_t>(batch.vertices.size()), 1, firstVertex_[index], 0);
    }
    endRenderPass(command_);

    // The render pass leaves the colour image ready to be read; a barrier makes
    // that read explicit rather than implied.
    VkImageMemoryBarrier toCopy{};
    toCopy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toCopy.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toCopy.image = target_.colour;
    toCopy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toCopy.subresourceRange.levelCount = 1;
    toCopy.subresourceRange.layerCount = 1;
    toCopy.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    pipelineBarrier(command_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &toCopy);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    copyImageToBuffer(command_, target_.colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target_.readback, 1, &region);

    // Nothing else to order here: the fence below is what makes the copy
    // visible to the host read, and HOST is not a stage a barrier may name.
    if (endCommandBuffer(command_) != VK_SUCCESS) return false;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_;
    resetFences(device_, 1, &fence_);
    if (queueSubmit(queue_, 1, &submit, fence_) != VK_SUCCESS) return false;
    if (waitForFences(device_, 1, &fence_, VK_TRUE, 5000000000ull) != VK_SUCCESS) return false;

    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    frame.width = width;
    frame.height = height;
    frame.rgb.assign(count * 3, 0);
    // The coverage mask is part of the frame contract, not an extra: the caller
    // checks it before it shows anything, and every pixel this pass returns was
    // drawn by it, so it is fully covered.
    frame.alpha.assign(count, 255);
    const unsigned char* source = static_cast<const unsigned char*>(target_.readbackMap);
    const int redOffset = bgra_ ? 2 : 0;
    const int blueOffset = bgra_ ? 0 : 2;
    for (std::size_t index = 0; index < count; ++index) {
        frame.rgb[index * 3 + 0] = source[index * 4 + redOffset];
        frame.rgb[index * 3 + 1] = source[index * 4 + 1];
        frame.rgb[index * 3 + 2] = source[index * 4 + blueOffset];
    }
    return true;
}

} // namespace detail

namespace {

detail::Renderer& instance() {
    static detail::Renderer renderer;
    return renderer;
}

} // namespace

bool vulkanAvailable() {
    static const bool available = instance().start();
    return available;
}

bool renderVulkanScene(const GpuScene& scene, NativeFrame& frame) {
    if (!vulkanAvailable()) return false;
    if (instance().draw(scene, frame)) return true;
    // A frame that will not draw is not allowed to end the session on its own:
    // the device is rebuilt once and the frame retried. Only a second failure
    // is reported upward.
    std::fprintf(stderr, "[vk] frame failed, rebuilding the device once\n");
    instance().stop();
    if (!instance().start()) return false;
    return instance().draw(scene, frame);
}

void shutdownVulkan() {
    instance().stop();
}

} // namespace gtc::native
