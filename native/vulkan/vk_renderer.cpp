// The frame loop for the Vulkan backend: targets, textures, vertex upload, one
// recorded pass, one readback. Kept intentional and synchronous -- the frame is
// finished before this function returns, because the result is a NativeFrame
// the rest of the client expects to own.

#include "../client/diagnostics.h"
#include "../client/threadpool.h"
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
// Tiled by default, and it is not close: rendering into linear memory costs the
// rasteriser its swizzle, so the GPU half of a frame measured 2.07 ms against
// 0.47 ms tiled. The copy out of a linear image is cheaper, but only by 0.4 ms,
// which is a bad trade for 1.6 ms of extra rendering. GTC_VK_LINEAR=1 asks for
// the other layout anyway.
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

// Makes sure one device-side array can hold `bytes`, growing it when it cannot.
// The memory type is chosen on the way: device-local, and host-visible too when
// the device offers such a type, which is the case everywhere this has run.
bool Renderer::reserveArray(DeviceArray& array, VkDeviceSize bytes) {
    if (!memoryQueried_) {
        getPhysicalDeviceMemoryProperties(physical_, &memory_);
        memoryQueried_ = true;
    }
    const VkPhysicalDeviceMemoryProperties& memory = memory_;
    const auto pickType = [&](const VkMemoryRequirements& requirements, VkMemoryPropertyFlags wanted) {
        for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
            if ((requirements.memoryTypeBits & (1u << index)) == 0) continue;
            if ((memory.memoryTypes[index].propertyFlags & wanted) != wanted) continue;
            return index;
        }
        return memory.memoryTypeCount;
    };
    if (array.buffer != VK_NULL_HANDLE && bytes <= array.capacity) return true;
    if (array.buffer) destroyBuffer(device_, array.buffer, nullptr);
    if (array.memory) freeMemory(device_, array.memory, nullptr);
    array = DeviceArray{};
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = bytes;
    // Transfer destination so a device-local buffer can be filled through the
    // staging buffer where there is no memory type that is both.
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (createBuffer(device_, &info, nullptr, &array.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    getBufferMemoryRequirements(device_, array.buffer, &requirements);
    // Host-visible memory that is *not* device local is often write-combined
    // system memory, where the GPU's read of a few megabytes drags -- which is
    // exactly what made a single skull cost milliseconds.
    const VkMemoryPropertyFlags ideal = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    std::uint32_t type = pickType(requirements, ideal);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    if (type != memory.memoryTypeCount) {
        allocation.memoryTypeIndex = type;
        if (allocateMemory(device_, &allocation, nullptr, &array.memory) != VK_SUCCESS) return false;
        if (bindBufferMemory(device_, array.buffer, array.memory, 0) != VK_SUCCESS) return false;
        if (mapMemory(device_, array.memory, 0, VK_WHOLE_SIZE, 0, &array.map) != VK_SUCCESS) return false;
    } else {
        type = pickType(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type == memory.memoryTypeCount) type = pickType(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (type == memory.memoryTypeCount) return false;
        allocation.memoryTypeIndex = type;
        if (allocateMemory(device_, &allocation, nullptr, &array.memory) != VK_SUCCESS) return false;
        if (bindBufferMemory(device_, array.buffer, array.memory, 0) != VK_SUCCESS) return false;
        array.staged = true;
    }
    array.capacity = bytes;
    return true;
}

// Fills an array from the scene: either every vertex's position and normal, or
// the texture coordinates of the batches that have a texture to sample.
bool Renderer::fillArray(DeviceArray& array, const GpuScene& scene, bool uvs, VkDeviceSize bytes) {
    if (!reserveArray(array, bytes)) return false;
    const auto write = [&](char* cursor) {
        std::uint32_t running = 0;
        // One array per call: the offsets of the array being written are the only
        // ones this pass may touch, or the second pass would blank the first.
        std::vector<std::uint32_t>& offsets = uvs ? firstUv_ : firstVertex_;
        offsets.assign(scene.batches.size(), 0);
        for (std::size_t index = 0; index < scene.batches.size(); ++index) {
            const GpuBatch& batch = scene.batches[index];
            const std::vector<GpuVertex>& vertices = batch.vertices;
            // Only the batches that sample an image have a coordinate, so only
            // they take up room in the coordinate array -- and only they move the
            // offset on.
            const bool keep = !uvs || texturedMaterial(batch.material);
            if (!keep) continue;
            offsets[index] = running;
            for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex) {
                if (uvs) {
                    UvVertex coordinate{vertices[vertex].u, vertices[vertex].v};
                    std::memcpy(cursor + static_cast<VkDeviceSize>(running + vertex) * sizeof(UvVertex),
                                &coordinate, sizeof(UvVertex));
                } else {
                    PackedVertex packed;
                    packed.x = vertices[vertex].x;
                    packed.y = vertices[vertex].y;
                    packed.z = vertices[vertex].z;
                    // A batch that goes into a run trades its normal for the slot
                    // its matrix sits at: an indexed batch has no vertex normal,
                    // and the slot has to reach the vertex stage somehow.
                    const float slot = index < slots_.size() ? slots_[index] : -1.0F;
                    packed.nx = slot >= 0.0F ? slot : vertices[vertex].nx;
                    packed.ny = vertices[vertex].ny;
                    packed.nz = vertices[vertex].nz;
                    std::memcpy(cursor + static_cast<VkDeviceSize>(running + vertex) * sizeof(PackedVertex),
                                &packed, sizeof(PackedVertex));
                }
            }
            running += static_cast<std::uint32_t>(vertices.size());
        }
        if (!uvs) uploadedVertices_ = running;
    };
    return uploadArray(array, bytes, write);
}

// Where an array actually lands: straight into the mapping when the memory is
// both device local and visible, otherwise through the reusable staging buffer.
// Shared by the vertices, the coordinates and the indices, because the three
// want the same thing.
bool Renderer::uploadArray(DeviceArray& array, VkDeviceSize bytes, const std::function<void(char*)>& write) {
    if (array.map) {
        write(static_cast<char*>(array.map));
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
        if (!memoryQueried_) {
            getPhysicalDeviceMemoryProperties(physical_, &memory_);
            memoryQueried_ = true;
        }
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        std::uint32_t type = memory_.memoryTypeCount;
        for (std::uint32_t index = 0; index < memory_.memoryTypeCount; ++index) {
            if ((requirements.memoryTypeBits & (1u << index)) == 0) continue;
            if ((memory_.memoryTypes[index].propertyFlags & wanted) != wanted) continue;
            type = index;
            break;
        }
        if (type == memory_.memoryTypeCount) return false;
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        if (allocateMemory(device_, &allocation, nullptr, &stagingMemory_) != VK_SUCCESS) return false;
        if (bindBufferMemory(device_, stagingBuffer_, stagingMemory_, 0) != VK_SUCCESS) return false;
        if (mapMemory(device_, stagingMemory_, 0, VK_WHOLE_SIZE, 0, &stagingMap_) != VK_SUCCESS) return false;
        stagingCapacity_ = bytes;
    }
    write(static_cast<char*>(stagingMap_));
    return submitNow([&](VkCommandBuffer buffer) {
        VkBufferCopy region{};
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = bytes;
        copyBufferFn(buffer, stagingBuffer_, array.buffer, 1, &region);
    });
}

bool Renderer::uploadVertices(const GpuScene& scene) {
    std::size_t total = 0, coordinates = 0;
    for (const GpuBatch& batch : scene.batches) {
        total += batch.vertices.size();
        if (texturedMaterial(batch.material)) coordinates += batch.vertices.size();
    }
    if (total == 0) {
        firstVertex_.assign(scene.batches.size(), 0);
        firstUv_.assign(scene.batches.size(), 0);
        runs_.clear();
        slots_.clear();
        return true;
    }
    // Which batches share a draw, before the upload: a batch in a run writes its
    // slot where its normal would have gone.
    findRuns(scene);
    if (!fillArray(vertex_, scene, false, static_cast<VkDeviceSize>(total) * sizeof(PackedVertex))) return false;
    // A batch with no texture samples nothing, so it is never bound to this
    // array and never has a coordinate fetched for it.
    if (coordinates > 0 &&
        !fillArray(uv_, scene, true, static_cast<VkDeviceSize>(coordinates) * sizeof(UvVertex))) {
        return false;
    }
    // The draw loop indexes both arrays by these offsets. A wrong one samples
    // another batch's pixels, which would look like a texture bug rather than an
    // offset bug, so they are checked once per upload instead of diagnosed later.
    std::size_t positions = 0, coordinatesUsed = 0;
    for (std::size_t index = 0; index < scene.batches.size(); ++index) {
        const GpuBatch& batch = scene.batches[index];
        if (firstVertex_[index] != positions) {
            std::fprintf(stderr, "[vk] vertex offset %u is not where batch %zu starts\n", firstVertex_[index],
                         index);
            return false;
        }
        positions += batch.vertices.size();
        if (!texturedMaterial(batch.material)) continue;
        if (firstUv_[index] != coordinatesUsed) {
            std::fprintf(stderr, "[vk] texture coordinate offset %u is not where batch %zu starts\n",
                         firstUv_[index], index);
            return false;
        }
        coordinatesUsed += batch.vertices.size();
    }
    // Indices, if any batch is indexed. They are written after the vertex
    // offsets above, because every index is shifted by one.
    std::size_t indices = 0;
    for (const GpuBatch& batch : scene.batches) indices += batch.indices.size();
    if (indices > 0) {
        if (!fillIndices(scene, static_cast<VkDeviceSize>(indices) * sizeof(std::uint32_t))) return false;
    } else {
        firstIndex_.assign(scene.batches.size(), 0);
        indexCount_.assign(scene.batches.size(), 0);
    }
    // A run's indices are contiguous in the shared array, because they are
    // written batch by batch in order.
    for (BatchRun& run : runs_) {
        run.indices = firstIndex_[run.last] + indexCount_[run.last] - firstIndex_[run.first];
    }
    if (positions != total || coordinatesUsed != coordinates) {
        std::fprintf(stderr, "[vk] upload counted %zu of %zu vertices, %zu of %zu coordinates\n", positions, total,
                     coordinatesUsed, coordinates);
        return false;
    }
    return true;
}

bool Renderer::draw(const GpuScene& scene, NativeFrame& frame) {
    if (!ready_) return false;
    // The first frames are written down step by step: if the process dies in a
    // driver, the last line names the call that was in flight.
    const bool traced = frames_ < 3;
    // Stated every frame: a caller reuses one frame object, and a frame that was
    // presented is not a frame that still has its pixels to show.
    frame.presented = false;
    const bool profile = profiling();
    const double setupStart = profile ? milliseconds() : 0.0;
    if (traced) tracef("vk: frame %d begin %dx%d", frames_, scene.width, scene.height);
    int width = std::max(1, scene.width);
    int height = std::max(1, scene.height);
    // Every pixel is about to be covered by the scene, so the frame is not
    // cleared first.
    const bool covered = scene.covered && renderPassCovered_ != VK_NULL_HANDLE &&
        pipelineCovered_ != VK_NULL_HANDLE;
    const int slot = static_cast<int>(frames_ % kFramesInFlight);
    // A frame that is going to the window is rendered at the size of the
    // swapchain image, so the handover is one to one. A resize may require one retry.
    int presentTarget = -1;
    if (presentWanted_ && surfaceUsable_ && !frame.wantPixels) {
        for (int attempt = 0; attempt < 2 && presentTarget < 0; ++attempt) {
            if (createSwapchain(width, height) && acquirePresentImage(slot, presentTarget)) {
                width = static_cast<int>(swapExtent_.width);
                height = static_cast<int>(swapExtent_.height);
            }
        }
    }
    if (wsiHandle_ && !frame.wantPixels && presentTarget < 0) {
        // An out-of-date swapchain is transient (e.g. while resizing). Skip
        // this frame, rather than reading it back or terminating the game.
        if (surfaceUsable_ && !swapReady_) { frame.presented = true; return true; }
        trace("vk: unable to acquire a presentation image");
        return false;
    }
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
        // The indices are geometry too: a batch that keeps its vertices and
        // changes its triangles -- a bone whose skinning changed -- has to
        // travel again, or the GPU keeps drawing the old faces.
        signature = (signature ^ batch.indices.size()) * 1099511628211ull;
        totalVertices += batch.vertices.size();
    }
    if (!uploadedValid_ || signature != uploadedSignature_) {
        if (pendingSlot_ >= 0 && waitForFences(device_, 1, &fences_[pendingSlot_], VK_TRUE, 5000000000ull) != VK_SUCCESS) return false;
        if (traced) tracef("vk: frame %d upload %zu batches", frames_, scene.batches.size());
        if (!uploadVertices(scene)) { tracef("vk: frame %d upload failed", frames_); return false; }
        uploadedSignature_ = signature;
        uploadedValid_ = true;
    } else if (totalVertices == 0) {
        firstVertex_.assign(scene.batches.size(), 0);
        firstUv_.assign(scene.batches.size(), 0);
    }
    // The offsets are indexed by batch below, so they are made to fit the batch
    // list that is actually being recorded rather than the one that was
    // uploaded.
    if (firstVertex_.size() != scene.batches.size() || firstUv_.size() != scene.batches.size()) {
        firstVertex_.assign(scene.batches.size(), 0);
        firstUv_.assign(scene.batches.size(), 0);
        std::uint32_t running = 0, coordinates = 0;
        for (std::size_t index = 0; index < scene.batches.size(); ++index) {
            firstVertex_[index] = running;
            firstUv_[index] = coordinates;
            running += static_cast<std::uint32_t>(scene.batches[index].vertices.size());
            if (texturedMaterial(scene.batches[index].material)) {
                coordinates += static_cast<std::uint32_t>(scene.batches[index].vertices.size());
            }
        }
    }

    // Textures are uploaded before recording, because each upload submits its
    // own command buffer and commands cannot be nested.
    std::vector<VkDescriptorSet> textureSets(scene.batches.size(), VK_NULL_HANDLE);
    bool interfaceLogged = false;
    for (std::size_t index = 0; index < scene.batches.size(); ++index) {
        if (scene.batches[index].vertices.empty()) continue;
        if (scene.batches[index].interface) {
            if (!interfaceLogged && traced) { trace("vk: drawing interface geometry on device"); interfaceLogged = true; }
            if (texturedMaterial(scene.batches[index].material)) {
                if (!ensureInterfaceAtlas()) return false;
                textureSets[index] = interfaceAtlas_.set;
                continue;
            }
        }
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
    command_ = commands_[slot];
    if (resetCommandBuffer(command_, 0) != VK_SUCCESS) { tracef("vk: frame %d reset failed", frames_); return false; }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    // One-time: recorded for this submission and thrown away after it. Reusing
    // a recorded buffer instead was tried and removed -- the driver's submission
    // cost with this flag went from 0.74 ms to 3.14 ms without it, three times
    // more than recording the commands was ever worth.
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
    // The attachment is an sRGB format, so a clear value is a linear colour the
    // device encodes on its way into memory: only the tone map belongs here.
    clears[0].color.float32[0] = static_cast<float>(toneValue(scene.background.r));
    clears[0].color.float32[1] = static_cast<float>(toneValue(scene.background.g));
    clears[0].color.float32[2] = static_cast<float>(toneValue(scene.background.b));
    clears[0].color.float32[3] = 1.0f;
    clears[1].depthStencil.depth = 1.0f;
    clears[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo pass{};

    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = covered ? renderPassCovered_ : renderPass_;
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

    bindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &frameSet_, 0, nullptr);
    const VkDeviceSize offset = 0;
    if (vertex_.buffer != VK_NULL_HANDLE) {
        bindVertexBuffers(command_, 0, 1, &vertex_.buffer, &offset);
    }
    // Bound once for the frame: an index names a vertex offset in the shared
    // stream, so every batch's indices live in this one array.
    if (index_.buffer != VK_NULL_HANDLE && !firstIndex_.empty()) {
        bindIndexBuffer(command_, index_.buffer, 0, VK_INDEX_TYPE_UINT32);
    }
    VkPipeline boundPipeline = VK_NULL_HANDLE;

    // A room's bones are 56 batches that mostly share one material, so the same
    // texture set gets bound over and over. Binding it again changes nothing and
    // costs a validation-and-write per draw.
    VkDescriptorSet boundTexture = VK_NULL_HANDLE;
    fillBones(scene);
    for (std::size_t index = 0; index < scene.batches.size(); ++index) {
        const GpuBatch& batch = scene.batches[index];
        if (batch.vertices.empty()) continue;
        const bool textured = texturedMaterial(batch.material);
        if (batch.interface) {
            const VkDeviceSize base = static_cast<VkDeviceSize>(firstVertex_[index]) * sizeof(PackedVertex);
            bindVertexBuffers(command_, 0, 1, &vertex_.buffer, &base);
        }
        const VkDescriptorSet textureSet = textureSets[index] != VK_NULL_HANDLE ? textureSets[index] : white_.set;
        if (textured) {
            // The coordinates of this batch, and only this batch: a body that
            // samples nothing has eight bytes fewer per vertex to fetch.
            const VkDeviceSize uvOffset = static_cast<VkDeviceSize>(firstUv_[index]) * sizeof(UvVertex);
            bindVertexBuffers(command_, 1, 1, &uv_.buffer, &uvOffset);
        }
        const VkPipeline wanted = batch.interface
            ? (textured ? (covered ? pipelineInterfaceUvCovered_ : pipelineInterfaceUv_) : (covered ? pipelineInterfaceCovered_ : pipelineInterface_))
            : (textured ? (covered ? pipelineUvCovered_ : pipelineUv_) : (covered ? pipelineCovered_ : pipeline_));
        if (wanted != boundPipeline) {
            bindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted);
            boundPipeline = wanted;
        }
        if (textureSet != boundTexture) {
            bindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 1, 1, &textureSet, 0,
                               nullptr);
            boundTexture = textureSet;
        }
        PushConstants push{};
        for (int word = 0; word < 16; ++word) push.model[word] = static_cast<float>(batch.modelMatrix[word]);
        push.colour[0] = static_cast<float>(batch.material.color.r);
        push.colour[1] = static_cast<float>(batch.material.color.g);
        push.colour[2] = static_cast<float>(batch.material.color.b);
        push.colour[3] = static_cast<float>(batch.material.opacity);
        push.flags[0] = textured ? 1.0f : 0.0f;
        push.flags[1] = (batch.material.constantLighting || batch.material.unlit) ? 1.0f : 0.0f;
        // A bone matrix is a rotation and a translation: the normal it produces
        // is already unit length, so the vertex stage does not have to pay for a
        // square root over every vertex in the skeleton. Checked here, once per
        // batch, from the matrix that is being sent anyway.
        const double* basis = batch.modelMatrix.data();
        const auto unitLength = [](double x, double y, double z) {
            const double squared = x * x + y * y + z * z;
            return squared > 0.998 && squared < 1.002;
        };
        const bool rigid = unitLength(basis[0], basis[1], basis[2]) && unitLength(basis[4], basis[5], basis[6]) &&
                           unitLength(basis[8], basis[9], basis[10]);
        push.flags[2] = rigid ? 1.0f : 0.0f;
        // A batch that does not shade and samples no texture has one colour for
        // every pixel it covers: tone map it once here, and the fragment stage
        // writes the answer instead of rebuilding it. The floor, the walls and
        // the ceiling are drawn this way and they are most of the screen.
        const bool oneColour = push.flags[1] > 0.5f && push.flags[0] < 0.5f;
        if (oneColour) {
            push.colour[0] = static_cast<float>(toneValue(batch.material.color.r));
            push.colour[1] = static_cast<float>(toneValue(batch.material.color.g));
            push.colour[2] = static_cast<float>(toneValue(batch.material.color.b));
        }
        push.flags[3] = oneColour ? 1.0f : 0.0f;
        // An indexed batch carries positions only: its normal belongs to the
        // triangle, not to the vertex, so the fragment stage derives it.
        const bool indexed = !batch.indices.empty();
        push.extra[0] = indexed ? 1.0f : 0.0f;
        // A run of rigid batches goes out as one call: the vertex stage reads
        // each vertex's matrix out of the bone array, so fifty-six draws become
        // one. Every batch in the run is pushed with the same colour, which is
        // why the run was formed.
        const auto run = index < slots_.size() && slots_[index] >= 0.0F
                             ? std::find_if(runs_.begin(), runs_.end(),
                                            [&](const BatchRun& candidate) { return candidate.first == index; })
                             : runs_.end();
        push.extra[1] = run != runs_.end() ? 1.0f : 0.0f;
        pushConstants(command_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(PushConstants), &push);
        if (run != runs_.end()) {
            if (run->indices > 0) drawIndexedFn(command_, run->indices, 1, firstIndex_[index], 0, 0);
            index = run->last;
        } else if (indexed) {
            const std::uint32_t count = index < indexCount_.size() ? indexCount_[index] : 0;
            if (count == 0) continue;
            drawIndexedFn(command_, count, 1, firstIndex_[index], 0, 0);
        } else {
            drawFn(command_, static_cast<std::uint32_t>(batch.vertices.size()), 1,
                   batch.interface ? 0 : firstVertex_[index], 0);
        }
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
    if (presentTarget >= 0) {
        // The picture is handed to the compositor from device memory: nothing
        // is copied out, and the host never sees a pixel.
        recordPresentBlit(presentTarget);
    } else if (!skipCopy()) {
        copyImageToBuffer(command_, target_.colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target_.readback[slot], 1,
                          &region);
    }

    // Nothing else to order here: the fence below is what makes the copy
    // visible to the host read, and HOST is not a stage a barrier may name.
    if (traced) tracef("vk: frame %d recorded", frames_);
    if (endCommandBuffer(command_) != VK_SUCCESS) { tracef("vk: frame %d end failed", frames_); return false; }
    const double submittedStart = profile ? milliseconds() : 0.0;
    if (profile && frames_ < 5) {
        std::printf("    [vk] record (barriers, pass, %zu draws): %.2f ms\n",
                    scene.batches.size(), submittedStart - recordedStart);
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (presentTarget >= 0) {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &imageReady_[slot];
        submit.pWaitDstStageMask = &waitStage;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &drawDone_[presentTarget];
    }
    resetFences(device_, 1, &fences_[slot]);
    fenceArmed_[slot] = true;
    if (traced) tracef("vk: frame %d submitting", frames_);
    if (queueSubmit(queue_, 1, &submit, fences_[slot]) != VK_SUCCESS) {
        tracef("vk: frame %d submit failed", frames_);
        return false;
    }
    if (presentTarget >= 0) presentImage(presentTarget);

    // Collect the frame that was submitted before this one. Waiting for *this*
    // frame would leave the device idle while the host copies the picture out;
    // waiting for the previous one lets the two overlap, which is the whole
    // point of keeping more than one in flight.
    int collect = frame.wantPixels ? slot : (pendingSlot_ >= 0 ? pendingSlot_ : slot);
    // On a presented frame there is nothing to collect and nothing to read, so
    // the very first one does not wait for itself: that is host time spent
    // watching the GPU it does not need to spend.
    if (presentTarget >= 0 && pendingSlot_ < 0) collect = -1;
    if (collect >= 0 && waitForFences(device_, 1, &fences_[collect], VK_TRUE, 5000000000ull) != VK_SUCCESS) {
        tracef("vk: frame %d wait failed", frames_);
        return false;
    }
    if (collect >= 0) fenceArmed_[collect] = false;
    pendingSlot_ = slot;
    const double gpuDone = profile ? milliseconds() : 0.0;
    if (profile && frames_ < 5) {
        std::printf("    [vk] submit + wait for the device: %.2f ms\n", gpuDone - submittedStart);
    }
    if (traced) tracef("vk: frame %d presented", frames_);
    ++frames_;

    if (presentTarget >= 0) {
        // Handed over on the device: there is nothing in host memory to hand on,
        // and the window must not blit it a second time.
        frame.width = width;
        frame.height = height;
        frame.rgb.clear();
        frame.bgra.clear();
        frame.alpha.clear();
        frame.direct = nullptr;
        frame.presented = true;
        return true;
    }
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (skipCopy()) return true;
    const double readbackStart = profiling() && frames_ < 6 ? milliseconds() : 0.0;
    frame.width = width;
    frame.height = height;
    // When the caller has said it wants the picture in window order, the frame
    // is handed over as BGRA and rgb stays empty: the presenter can then put it
    // on screen without touching a pixel, and a caller that does want to draw
    // over it asks for rgb and gets it converted once, lazily.
    const bool wantBgra = frame.wantsBgra;
    if (wantBgra) {
        if (target_.readbackCached && bgra_) frame.bgra.clear();
        else frame.bgra.resize(count * 4);
        frame.rgb.clear();
    } else {
        frame.direct = nullptr;
        // Every pixel below is overwritten, so the buffers are only grown, never
        // cleared: zero-filling 5.8 MB per frame is real time at 60 Hz.
        frame.rgb.resize(count * 3);
        frame.bgra.clear();
    }
    // The coverage mask is part of the frame contract, not an extra: the caller
    // checks it before it shows anything, and every pixel this pass returns was
    // drawn by it, so it is fully covered. A frame handed straight to the window
    // has nobody to ask, so filling 1.45 MB of mask for it is wasted bandwidth.
    if (!wantBgra) {
        frame.alpha.resize(count);
        std::memset(frame.alpha.data(), 255, count);
    } else {
        frame.alpha.clear();
    }
    // One wide move out of the mapped range, then convert from ordinary
    // memory: a memcpy of 5.8 MB is a handful of vector loads per cache line,
    // where the byte-by-byte read that used to be here was one transaction per
    // channel per pixel on a write-combined mapping.
    const std::size_t bytes = count * 4;
    const int readSlot = collect;
    // Cached host memory reads at full speed, so the frame is converted straight
    // out of the mapping; anything else (write-combined, device-local) is copied
    // out in one wide move first and converted from ordinary memory.
    const unsigned char* mapped = static_cast<const unsigned char*>(target_.readbackMap[readSlot]);
    if (!target_.readbackCached) {
        if (target_.staging[readSlot].size() < bytes) target_.staging[readSlot].resize(bytes);
        std::memcpy(target_.staging[readSlot].data(), mapped, bytes);
        mapped = target_.staging[readSlot].data();
    }
    // The mapped readback is already in host memory and already in window
    // order, so the frame can point straight at it: no copy at all. Only a
    // caller that must own the pixels -- a screenshot taken after the next
    // frame has been drawn -- asks for rgb and pays for the conversion then.
    frame.direct = (wantBgra && target_.readbackCached && bgra_) ? mapped : nullptr;
    // One 32-bit load per pixel and shifts, rather than four byte loads, and the
    // band is split across cores: 1.45 M pixels of channel shuffling is exactly
    // the sort of work that is embarrassingly parallel, and doing it on one
    // thread was leaving three cores idle every frame.
    const std::uint32_t* texels = reinterpret_cast<const std::uint32_t*>(mapped);
    const int redShift = bgra_ ? 16 : 0;
    const int blueShift = bgra_ ? 0 : 16;
    const auto convert = [&](std::size_t begin, std::size_t end) {
        if (wantBgra && frame.direct) return;  // nothing to do: the frame points at it
        if (wantBgra) {
            // The device already stores the bytes in the order the window wants
            // when its colour format is BGRA: nothing to do but move them.
            unsigned char* out = frame.bgra.data() + begin * 4;
            if (bgra_) {
                std::memcpy(out, texels + begin, (end - begin) * 4);
                return;
            }
            for (std::size_t index = begin; index < end; ++index, out += 4) {
                const std::uint32_t texel = texels[index];
                out[0] = static_cast<unsigned char>((texel >> 16) & 0xFFu);
                out[1] = static_cast<unsigned char>((texel >> 8) & 0xFFu);
                out[2] = static_cast<unsigned char>(texel & 0xFFu);
                out[3] = 255;
            }
            return;
        }
        // A byte per channel per pixel, written straight out: this loop is over
        // a million pixels on every frame the overlay is drawn, so its shape is
        // the difference between a millisecond and a small fraction of one. The
        // four-pixels-in-twelve-bytes-out version of this was slower.
        unsigned char* out = frame.rgb.data() + begin * 3;
        for (std::size_t index = begin; index < end; ++index, out += 3) {
            const std::uint32_t texel = texels[index];
            out[0] = static_cast<unsigned char>((texel >> redShift) & 0xFFu);
            out[1] = static_cast<unsigned char>((texel >> 8) & 0xFFu);
            out[2] = static_cast<unsigned char>((texel >> blueShift) & 0xFFu);
        }
    };
    // Nothing to convert when the frame was handed over pointing straight at the
    // mapping. Waking the pool to do nothing would cost more than the work it
    // skips, and this is the path the test room takes every frame.
    if (!(wantBgra && frame.direct)) {
        FramePool& pool = framePool();
        const std::size_t chunks = std::min<std::size_t>(pool.size(), std::max<std::size_t>(1, count / 131072));
        pool.ranges(chunks, count, convert);
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
    // This frame is read, not shown: the check below is about the pixels. A
    // presented frame never has any, and asking for one through the swapchain
    // was how the whole backend failed its own test and fell back to OpenGL.
    frame.wantPixels = true;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!renderer.draw(scene, frame)) {
            std::fprintf(stderr, "[vk] self test frame %d failed\n", attempt);
            tracef("vk: self test frame %d failed", attempt);
            return false;
        }
        const std::size_t pixels = static_cast<std::size_t>(frame.width) * frame.height;
        if (frame.width != 32 || frame.height != 24 || frame.rgb.size() < pixels * 3 ||
            frame.alpha.size() < pixels) {
            std::fprintf(stderr, "[vk] self test broke the frame contract (%dx%d, rgb %zu, alpha %zu)\n",
                         frame.width, frame.height, frame.rgb.size(), frame.alpha.size());
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
            // The backend that draws this machine's frames failed its own first
            // frame. That is the fault to look at, so nothing else runs.
            std::fflush(stdout);
            std::fprintf(stderr, "[vk] self test failed; the Vulkan backend is broken, not the machine\n");
            std::fflush(stderr);
            std::abort();
        }
        tracef("vk: available=%d", available ? 1 : 0);
    }
    return available;
}

bool renderVulkanScene(const GpuScene& scene, NativeFrame& frame) {
    if (!vulkanAvailable()) return false;
    if (instance().draw(scene, frame)) return true;
    // No rebuild-and-retry. A frame that will not draw is the fault; rebuilding
    // the device over it is how the fault goes unread for another week.
    std::fflush(stdout);
    std::fprintf(stderr, "[vk] frame failed\n");
    std::fflush(stderr);
    std::abort();
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

void setVulkanSurfaceTarget(void* windowHandle, int kind) {
    detail::Renderer& renderer = instance();
    renderer.setSurfaceTarget(windowHandle, kind);
}

void drainVulkan() {
    detail::Renderer& renderer = instance();
    renderer.drain();  // a no-op when the device was never created
}

void shutdownVulkan() {
    closed = true;
    instance().stop();
}

} // namespace gtc::native
