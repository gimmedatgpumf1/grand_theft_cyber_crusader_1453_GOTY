// The frame loop for the Vulkan backend: targets, textures, vertex upload, one
// recorded pass, one readback. Kept intentional and synchronous -- the frame is
// finished before this function returns, because the result is a NativeFrame
// the rest of the client expects to own.

#include "../client/diagnostics.h"
#include "vk_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>

namespace gtc::native {
namespace detail {

// Whether a drawn frame is copied back to host memory; see vk_renderer.h.
bool readbackEnabled = std::getenv("GTC_VK_NO_COPY") == nullptr;
bool linearTarget = std::getenv("GTC_VK_LINEAR") != nullptr;
int readbackMode = 0;

// Stage timing, off unless asked for: `GTC_VK_PROFILE=1`. The point is to name
// the stage that costs, because "Vulkan is slow" says nothing about which of
// four things is slow.
bool profiling() {
    static const bool on = std::getenv("GTC_VK_PROFILE") != nullptr;
    return on;
}

namespace {

// The frame can be drawn without being copied out, which separates "how long
// does the GPU take to draw" from "how long does it take to hand the result
// back". Only the benchmarks set this; a normal run always reads the frame.
bool skipCopy() { return !readbackEnabled; }

// Half the machine, honouring GTC_THREADS if it is set. The frame loop is not
// the only thing running, and a client that takes every core to shuffle bytes
// makes the rest of the process slower than it makes this part faster.
std::size_t conversionThreads() {
    static const std::size_t count = [] {
        if (const char* setting = std::getenv("GTC_THREADS")) {
            const int value = std::atoi(setting);
            if (value > 0) return static_cast<std::size_t>(value);
        }
        const unsigned hardware = std::thread::hardware_concurrency();
        return static_cast<std::size_t>(hardware == 0 ? 2 : std::max(2u, hardware / 2));
    }();
    return count;
}

double milliseconds() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

bool Renderer::submitNow(const std::function<void(VkCommandBuffer)>& body) {
    // Its own buffer and fence: sharing the frame's would reset a fence the
    // pending frame is still using, and silently corrupt the pipeline.
    if (uploadCommand_ == VK_NULL_HANDLE || uploadFence_ == VK_NULL_HANDLE) return false;
    VkCommandBuffer buffer = uploadCommand_;
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
    resetFences(device_, 1, &uploadFence_);
    if (queueSubmit(queue_, 1, &submit, uploadFence_) != VK_SUCCESS) return false;
    const VkResult waited = waitForFences(device_, 1, &uploadFence_, VK_TRUE, 5000000000ull);
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
    for (int slot = 0; slot < kFramesInFlight; ++slot) {
        if (target_.readback[slot]) destroyBuffer(device_, target_.readback[slot], nullptr);
        if (target_.readbackMemory[slot]) freeMemory(device_, target_.readbackMemory[slot], nullptr);
    }
    if (target_.colourMemory) freeMemory(device_, target_.colourMemory, nullptr);
    if (target_.depthMemory) freeMemory(device_, target_.depthMemory, nullptr);
    target_ = Target{};
}

bool Renderer::uploadVertices(const GpuScene& scene) {
    std::size_t total = 0;
    for (const GpuBatch& batch : scene.batches) total += batch.vertices.size();
    firstVertex_.assign(scene.batches.size(), 0);
    if (total == 0) return true;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(total) * sizeof(GpuVertex);

    VkPhysicalDeviceMemoryProperties memory{};
    getPhysicalDeviceMemoryProperties(physical_, &memory);
    const auto pickType = [&](const VkMemoryRequirements& requirements, VkMemoryPropertyFlags wanted) {
        for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
            if ((requirements.memoryTypeBits & (1u << index)) == 0) continue;
            if ((memory.memoryTypes[index].propertyFlags & wanted) != wanted) continue;
            return index;
        }
        return memory.memoryTypeCount;
    };

    if (bytes > vertexCapacity_) {
        if (vertexBuffer_) destroyBuffer(device_, vertexBuffer_, nullptr);
        if (vertexMemory_) freeMemory(device_, vertexMemory_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
        vertexMemory_ = VK_NULL_HANDLE;
        vertexMap_ = nullptr;
        vertexCapacity_ = 0;
        vertexStaged_ = false;

        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = bytes;
        // Transfer destination so a device-local buffer can be filled from a
        // staging buffer where there is no memory type that is both.
        info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (createBuffer(device_, &info, nullptr, &vertexBuffer_) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements{};
        getBufferMemoryRequirements(device_, vertexBuffer_, &requirements);

        // The geometry is read by the GPU every frame and written by the host
        // only when it changes, so device-local memory is the right home for it.
        // Host-visible memory that is *not* device local is often write-combined
        // system memory, where the GPU's read of a few megabytes drags -- which
        // is exactly what made a single skull cost milliseconds.
        const VkMemoryPropertyFlags ideal = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        std::uint32_t type = pickType(requirements, ideal);
        if (type != memory.memoryTypeCount) {
            VkMemoryAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = type;
            if (allocateMemory(device_, &allocation, nullptr, &vertexMemory_) != VK_SUCCESS) return false;
            if (bindBufferMemory(device_, vertexBuffer_, vertexMemory_, 0) != VK_SUCCESS) return false;
            if (mapMemory(device_, vertexMemory_, 0, VK_WHOLE_SIZE, 0, &vertexMap_) != VK_SUCCESS) return false;
        } else {
            // No such type: device-local buffer, filled through a staging buffer.
            type = pickType(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (type == memory.memoryTypeCount) type = pickType(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            if (type == memory.memoryTypeCount) return false;
            VkMemoryAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = type;
            if (allocateMemory(device_, &allocation, nullptr, &vertexMemory_) != VK_SUCCESS) return false;
            if (bindBufferMemory(device_, vertexBuffer_, vertexMemory_, 0) != VK_SUCCESS) return false;
            vertexStaged_ = true;
        }
        vertexCapacity_ = bytes;
    }

    if (vertexMap_) {
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

    // Staged path: one host-visible buffer, reused, then copied on the device.
    if (bytes > stagingCapacity_) {
        if (stagingBuffer_) destroyBuffer(device_, stagingBuffer_, nullptr);
        if (stagingMemory_) freeMemory(device_, stagingMemory_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        stagingMemory_ = VK_NULL_HANDLE;
        stagingMap_ = nullptr;
        stagingCapacity_ = 0;
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = bytes;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (createBuffer(device_, &info, nullptr, &stagingBuffer_) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements{};
        getBufferMemoryRequirements(device_, stagingBuffer_, &requirements);
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        const std::uint32_t type = pickType(requirements, wanted);
        if (type == memory.memoryTypeCount) return false;
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        if (allocateMemory(device_, &allocation, nullptr, &stagingMemory_) != VK_SUCCESS) return false;
        if (bindBufferMemory(device_, stagingBuffer_, stagingMemory_, 0) != VK_SUCCESS) return false;
        if (mapMemory(device_, stagingMemory_, 0, VK_WHOLE_SIZE, 0, &stagingMap_) != VK_SUCCESS) return false;
        stagingCapacity_ = bytes;
    }
    char* cursor = static_cast<char*>(stagingMap_);
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
    const VkDeviceSize copyBytes = static_cast<VkDeviceSize>(total) * sizeof(GpuVertex);
    if (!submitNow([&](VkCommandBuffer buffer) {
            VkBufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = copyBytes;
            copyBufferFn(buffer, stagingBuffer_, vertexBuffer_, 1, &region);
        })) {
        return false;
    }
    uploadedVertices_ = total;
    return true;
}

bool Renderer::draw(const GpuScene& scene, NativeFrame& frame) {
    if (!ready_) return false;
    // The first frames are written down step by step: if the process dies in a
    // driver, the last line names the call that was in flight.
    const bool traced = frames_ < 3;
    const bool profile = profiling();
    const double setupStart = profile ? milliseconds() : 0.0;
    if (traced) tracef("vk: frame %d begin %dx%d", frames_, scene.width, scene.height);
    const int width = std::max(1, scene.width);
    const int height = std::max(1, scene.height);
    if (width != target_.width || height != target_.height) {
        if (traced) tracef("vk: frame %d targets %dx%d", frames_, width, height);
        if (!createTargets(width, height)) { tracef("vk: frame %d targets failed", frames_); return false; }
        if (traced) tracef("vk: frame %d targets ready", frames_);
    }
    if (white_.set == VK_NULL_HANDLE) {
        // One white pixel stands in for "this material has no image", so the
        // shader can sample unconditionally and stay a single module.
        Texture white;
        white.width = 1;
        white.height = 1;
        white.pixels = {255, 255, 255, 255};
        if (!createTexture(white, white_)) { tracef("vk: frame %d white texture failed", frames_); return false; }
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
        if (traced) tracef("vk: frame %d upload %zu batches", frames_, scene.batches.size());
        if (!uploadVertices(scene)) { tracef("vk: frame %d upload failed", frames_); return false; }
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

    const double recordedStart = profile ? milliseconds() : 0.0;
    if (profile && frames_ < 5) {
        std::printf("    [vk] setup (targets, upload, textures): %.2f ms\n", recordedStart - setupStart);
    }
    const int slot = static_cast<int>(frames_ % kFramesInFlight);
    command_ = commands_[slot];
    if (resetCommandBuffer(command_, 0) != VK_SUCCESS) { tracef("vk: frame %d reset failed", frames_); return false; }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (beginCommandBuffer(command_, &begin) != VK_SUCCESS) { tracef("vk: frame %d begin failed", frames_); return false; }

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
    // This slot's buffer was written by an earlier transfer and read by the
    // host since; the write-after-write is stated for the same buffer.
    reuse.buffer = target_.readback[slot];
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
    if (!skipCopy()) {
        copyImageToBuffer(command_, target_.colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target_.readback[slot], 1,
                          &region);
    }

    // Nothing else to order here: the fence below is what makes the copy
    // visible to the host read, and HOST is not a stage a barrier may name.
    const double submittedStart = profile ? milliseconds() : 0.0;
    if (profile && frames_ < 5) {
        std::printf("    [vk] record (barriers, pass, %zu draws): %.2f ms\n",
                    scene.batches.size(), submittedStart - recordedStart);
    }
    if (traced) tracef("vk: frame %d recorded", frames_);
    if (endCommandBuffer(command_) != VK_SUCCESS) { tracef("vk: frame %d end failed", frames_); return false; }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_;
    resetFences(device_, 1, &fences_[slot]);
    fenceArmed_[slot] = true;
    if (traced) tracef("vk: frame %d submitting", frames_);
    if (queueSubmit(queue_, 1, &submit, fences_[slot]) != VK_SUCCESS) {
        tracef("vk: frame %d submit failed", frames_);
        return false;
    }
    const double submitted = profile ? milliseconds() : 0.0;

    // Collect the frame that was submitted before this one. Waiting for *this*
    // frame would leave the device idle while the host copies the picture out;
    // waiting for the previous one lets the two overlap, which is the whole
    // point of keeping more than one in flight.
    const int collect = pendingSlot_ >= 0 ? pendingSlot_ : slot;
    if (waitForFences(device_, 1, &fences_[collect], VK_TRUE, 5000000000ull) != VK_SUCCESS) {
        tracef("vk: frame %d wait failed", frames_);
        return false;
    }
    fenceArmed_[collect] = false;
    pendingSlot_ = slot;
    const double gpuDone = profile ? milliseconds() : 0.0;
    if (profile && frames_ < 5) {
        std::printf("    [vk] submit + wait for the device: %.2f ms\n", gpuDone - submittedStart);
    }
    if (traced) tracef("vk: frame %d presented", frames_);
    ++frames_;

    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (skipCopy()) return true;
    const double readbackStart = profiling() && frames_ < 6 ? milliseconds() : 0.0;
    frame.width = width;
    frame.height = height;
    // Every pixel below is overwritten, so the buffers are only grown, never
    // cleared: zero-filling 5.8 MB per frame is real time at 60 Hz.
    frame.rgb.resize(count * 3);
    // The coverage mask is part of the frame contract, not an extra: the caller
    // checks it before it shows anything, and every pixel this pass returns was
    // drawn by it, so it is fully covered.
    frame.alpha.resize(count);
    std::memset(frame.alpha.data(), 255, count);
    // One wide move out of the mapped range, then convert from ordinary
    // memory: a memcpy of 5.8 MB is a handful of vector loads per cache line,
    // where the byte-by-byte read that used to be here was one transaction per
    // channel per pixel on a write-combined mapping.
    const std::size_t bytes = count * 4;
    const int readSlot = collect;
    if (target_.staging[readSlot].size() < bytes) target_.staging[readSlot].resize(bytes);
    std::memcpy(target_.staging[readSlot].data(), target_.readbackMap[readSlot], bytes);
    // One 32-bit load per pixel and shifts, rather than four byte loads, and the
    // band is split across cores: 1.45 M pixels of channel shuffling is exactly
    // the sort of work that is embarrassingly parallel, and doing it on one
    // thread was leaving three cores idle every frame.
    const std::uint32_t* texels = reinterpret_cast<const std::uint32_t*>(target_.staging[readSlot].data());
    const int redShift = bgra_ ? 16 : 0;
    const int blueShift = bgra_ ? 0 : 16;
    const auto convert = [&](std::size_t begin, std::size_t end) {
        unsigned char* out = frame.rgb.data() + begin * 3;
        for (std::size_t index = begin; index < end; ++index, out += 3) {
            const std::uint32_t texel = texels[index];
            out[0] = static_cast<unsigned char>((texel >> redShift) & 0xFFu);
            out[1] = static_cast<unsigned char>((texel >> 8) & 0xFFu);
            out[2] = static_cast<unsigned char>((texel >> blueShift) & 0xFFu);
        }
    };
    const std::size_t workers = std::max<std::size_t>(1,
        std::min<std::size_t>(conversionThreads(), std::max<std::size_t>(1, count / 65536)));
    if (workers > 1) {
        std::vector<std::thread> pool;
        pool.reserve(workers - 1);
        for (std::size_t worker = 1; worker < workers; ++worker) {
            const std::size_t begin = count * worker / workers;
            const std::size_t end = count * (worker + 1) / workers;
            pool.emplace_back([&convert, begin, end] { convert(begin, end); });
        }
        convert(0, count / workers);
        for (std::thread& thread : pool) thread.join();
    } else {
        convert(0, count);
    }
    if (readbackStart > 0.0) {
        std::printf("    [vk] readback (copy out + convert): %.2f ms\n", milliseconds() - readbackStart);
    }
    return true;
}

} // namespace detail

namespace {

detail::Renderer& instance() {
    static detail::Renderer renderer;
    return renderer;
}

// Set once the owner has asked for shutdown. Nothing may start the device again
// after that, which is what keeps a closed window from drawing into a dead
// renderer.
bool closed = false;

// One small frame through the whole path before the window commits to this
// backend. A device that cannot finish a 32x24 frame cannot finish a 1568x929
// one either, and finding that out here keeps the session alive on the backend
// that does work.
bool selfTest() {
    detail::Renderer& renderer = instance();
    GpuScene scene;
    scene.width = 32;
    scene.height = 24;
    scene.focal = 30;
    scene.revision = 1;
    scene.background = {0.0, 0.0, 0.0};
    GpuBatch batch;
    batch.revision = 1;
    const float vertices[9] = {-0.4f, -0.4f, -2.0f, 0.4f, -0.4f, -2.0f, 0.0f, 0.4f, -2.0f};
    for (int index = 0; index < 3; ++index) {
        GpuVertex vertex;
        vertex.x = vertices[index * 3 + 0];
        vertex.y = vertices[index * 3 + 1];
        vertex.z = vertices[index * 3 + 2];
        vertex.nz = -1.0f;
        batch.vertices.push_back(vertex);
    }
    scene.batches.push_back(batch);
    NativeFrame frame;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!renderer.draw(scene, frame)) {
            tracef("vk: self test frame %d failed", attempt);
            return false;
        }
        const std::size_t pixels = static_cast<std::size_t>(frame.width) * frame.height;
        if (frame.width != 32 || frame.height != 24 || frame.rgb.size() < pixels * 3 ||
            frame.alpha.size() < pixels) {
            trace("vk: self test frame broke the frame contract");
            return false;
        }
    }
    trace("vk: self test passed");
    return true;
}

} // namespace

bool vulkanAvailable() {
    if (closed) return false;
    static bool tried = false;
    static bool available = false;
    if (!tried) {
        tried = true;
        available = instance().start();
        if (available && !selfTest()) {
            trace("vk: self test failed, staying on the other backend");
            instance().stop();
            available = false;
        }
        tracef("vk: available=%d", available ? 1 : 0);
    }
    return available;
}

bool renderVulkanScene(const GpuScene& scene, NativeFrame& frame) {
    if (!vulkanAvailable()) return false;
    if (instance().draw(scene, frame)) return true;
    // One rebuild per process. Retrying on every frame turned a single failure
    // into a device teardown and a shader recompile per frame, which is slower
    // than not drawing at all.
    static bool rebuilt = false;
    if (rebuilt) return false;
    rebuilt = true;
    // A frame that will not draw is not allowed to end the session on its own:
    // the device is rebuilt once and the frame retried. Only a second failure
    // is reported upward.
    std::fprintf(stderr, "[vk] frame failed, rebuilding the device once\n");
    instance().stop();
    if (!instance().start()) return false;
    return instance().draw(scene, frame);
}

void setFrameReadback(bool enabled) { detail::readbackEnabled = enabled; }

void setReadbackMemory(int mode) {
    if (detail::readbackMode == mode) return;
    detail::readbackMode = mode;
    instance().discardTargets();
}

void setLinearTarget(bool enabled) {
    if (detail::linearTarget == enabled) return;
    detail::linearTarget = enabled;
    // The existing targets were built for the other layout.
    instance().discardTargets();
}

void shutdownVulkan() {
    closed = true;
    instance().stop();
}

} // namespace gtc::native
