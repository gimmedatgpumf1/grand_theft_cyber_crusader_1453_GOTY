// Offscreen targets and textures: the images the backend draws into, the
// buffers it copies frames back through, and the material textures it samples.
// Kept apart from the frame loop so the frame loop reads as a frame.

#include "../client/diagnostics.h"
#include "vk_internal.h"

#include <cstdio>
#include <cstring>

namespace gtc::native {
namespace detail {

// The two render passes: one that clears the colour attachment and one that
// leaves it, for a scene that covers every pixel.
bool Renderer::createRenderPass() {
    // Two passes that differ in one byte's worth of behaviour: whether the
    // colour attachment is cleared before drawing or left as it was. An empty
    // frame costs 2.55 ms on this hardware and most of it is memory traffic, so
    // a scene that covers every pixel -- the test room is a closed box -- does
    // not have to pay to have 5.8 MB of background written first.
    const auto buildPass = [this](VkAttachmentLoadOp colourLoad, VkRenderPass* out) {
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = colourFormat_;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = colourLoad;
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
    return createRenderPassFn(device_, &info, nullptr, out) == VK_SUCCESS;
    };
    if (!buildPass(VK_ATTACHMENT_LOAD_OP_CLEAR, &renderPass_)) return false;
    return buildPass(VK_ATTACHMENT_LOAD_OP_DONT_CARE, &renderPassCovered_);
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
    const auto makeImage = [&](VkFormat format, VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& allocation,
                               bool allowLinear) {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        if (allowLinear && linearTarget) {
            // Only where the device says a linear image of this format can be a
            // colour attachment; otherwise the tiled path stays.
            VkFormatProperties properties{};
            getPhysicalDeviceFormatProperties(physical_, format, &properties);
            if ((properties.linearTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) {
                info.tiling = VK_IMAGE_TILING_LINEAR;
            } else {
                trace("vk: this device will not render into a linear image; staying tiled");
            }
        }
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
    if (!makeImage(colourFormat_, colourUsage, target_.colour, target_.colourMemory, true)) return false;
    if (!makeImage(depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, target_.depth, target_.depthMemory,
                   false)) {
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
    for (int slot = 0; slot < kFramesInFlight; ++slot) {
    if (createBuffer(device_, &readback, nullptr, &target_.readback[slot]) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    getBufferMemoryRequirements(device_, target_.readback[slot], &requirements);
    // Cached and coherent if the device offers it: the host reads this buffer
    // every frame, and write-combined memory turns that read into a stream of
    // uncached bus transactions.
    const VkMemoryPropertyFlags coherent = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkMemoryPropertyFlags requested =
        readbackMode == 1 ? coherent | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
      : readbackMode == 2 ? coherent
      : coherent | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    std::uint32_t type = pick(requirements.memoryTypeBits, requested);
    target_.readbackCached = readbackMode == 0 && type != memory.memoryTypeCount;
    if (type == memory.memoryTypeCount) type = pick(requirements.memoryTypeBits, coherent);
    tracef("vk: readback memory %s", target_.readbackCached ? "host cached" : "host visible only");
    if (profiling()) {
        // This single fact decides whether the GPU's copy out of the image is a
        // fast write into cacheable memory or a stream of uncached writes, so it
        // is printed rather than buried in the log.
        const char* name = readbackMode == 0 ? "host cached"
            : readbackMode == 1 ? "device local + host visible" : "host visible only";
        std::printf("    [vk] readback buffer memory requested: %s%s\n", name,
                    type == memory.memoryTypeCount ? " (unavailable)" : "");
    }
    if (type == memory.memoryTypeCount) return false;
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    if (this->allocateMemory(device_, &allocate, nullptr, &target_.readbackMemory[slot]) != VK_SUCCESS) return false;
    if (bindBufferMemory(device_, target_.readback[slot], target_.readbackMemory[slot], 0) != VK_SUCCESS) return false;
    if (mapMemory(device_, target_.readbackMemory[slot], 0, VK_WHOLE_SIZE, 0, &target_.readbackMap[slot]) != VK_SUCCESS) {
        return false;
    }
    }

    if (profiling()) {
        std::printf("    [vk] colour image: %s\n",
                    linearTarget ? "linear (linear tiling requested)" : "tiled");
    }
    for (int slot = 0; slot < kFramesInFlight; ++slot) {
        target_.staging[slot].resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    }
    target_.width = width;
    target_.height = height;
    return true;
}

// The sRGB transfer curve as a texture: row 0 decodes, row 1 encodes. Built
// here rather than evaluated in the shader, where pow() per channel per pixel
// was the most expensive thing in the fragment stage.
bool Renderer::createTransferTable() {
    constexpr int width = 1024;
    Texture table;
    table.width = width;
    table.height = 2;
    table.pixels.resize(static_cast<std::size_t>(width) * 2 * 4);
    const auto encode = [](double value) {
        if (value <= 0.0) return 0.0;
        if (value >= 1.0) return 1.0;
        return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    };
    const auto decode = [](double value) {
        if (value <= 0.0) return 0.0;
        if (value >= 1.0) return 1.0;
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    for (int row = 0; row < 2; ++row) {
        for (int index = 0; index < width; ++index) {
            const double x = index / static_cast<double>(width - 1);
            const double value = row == 0 ? decode(x) : encode(x);
            const auto byte = static_cast<unsigned char>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
            const std::size_t offset = (static_cast<std::size_t>(row) * width + index) * 4;
            table.pixels[offset + 0] = byte;
            table.pixels[offset + 1] = byte;
            table.pixels[offset + 2] = byte;
            table.pixels[offset + 3] = 255;
        }
    }
    if (!createTexture(table, transfer_)) return false;
    VkDescriptorImageInfo image{};
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.imageView = transfer_.view;
    image.sampler = sampler_;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = frameSet_;
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    updateDescriptorSets(device_, 1, &write, 0, nullptr);
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

} // namespace detail
} // namespace gtc::native
