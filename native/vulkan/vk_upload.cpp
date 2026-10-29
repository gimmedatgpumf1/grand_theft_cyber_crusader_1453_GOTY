// Geometry upload and reuse for the Vulkan renderer.
#include "vk_internal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>

namespace gtc::native::detail {

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
    info.size = bytes + bytes / 2;
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
    }
    array.capacity = info.size;
    return true;
}

// Fills an array from the scene: either every vertex's position and normal, or
// the texture coordinates of the batches that have a texture to sample.
bool Renderer::fillArray(DeviceArray& array, const GpuScene& scene, bool uvs, VkDeviceSize bytes) {
    const bool reallocate = !array.buffer || bytes > array.capacity;
    if (!reserveArray(array, bytes)) return false;
    const std::size_t first = reallocate ? 0 : dirtyBatch_;
    std::vector<std::uint32_t>& offsets = uvs ? firstUv_ : firstVertex_;
    offsets.assign(scene.batches.size(), 0);
    std::uint32_t running = 0, start = 0;
    for (std::size_t i = 0; i < scene.batches.size(); ++i) {
        if (i == first) start = running;
        if (uvs && !texturedMaterial(scene.batches[i].material)) continue;
        offsets[i] = running;
        running += static_cast<std::uint32_t>(scene.batches[i].vertices.size());
    }
    if (first >= scene.batches.size()) start = running;
    const VkDeviceSize stride = uvs ? sizeof(UvVertex) : sizeof(PackedVertex);
    const VkDeviceSize offset = static_cast<VkDeviceSize>(start) * stride;
    if (offset == bytes) return true;
    const auto write = [&](char* cursor) {
        for (std::size_t i = first; i < scene.batches.size(); ++i) {
            const GpuBatch& batch = scene.batches[i];
            if (uvs && !texturedMaterial(batch.material)) continue;
            for (std::size_t j = 0; j < batch.vertices.size(); ++j) {
                const GpuVertex& vertex = batch.vertices[j];
                const VkDeviceSize dest = static_cast<VkDeviceSize>(offsets[i] - start + j) * stride;
                if (uvs) {
                    UvVertex coordinate{vertex.u, vertex.v};
                    std::memcpy(cursor + dest, &coordinate, sizeof(coordinate));
                } else {
                    PackedVertex packed{vertex.x, vertex.y, vertex.z,
                        slots_[i] >= 0.0F ? slots_[i] : vertex.nx, vertex.ny, vertex.nz};
                    std::memcpy(cursor + dest, &packed, sizeof(packed));
                }
            }
        }
    };
    return uploadArray(array, bytes - offset, write, offset);
}

// Where an array actually lands: straight into the mapping when the memory is
// both device local and visible, otherwise through the reusable staging buffer.
// Shared by the vertices, the coordinates and the indices, because the three
// want the same thing.
bool Renderer::uploadArray(DeviceArray& array, VkDeviceSize bytes, const std::function<void(char*)>& write, VkDeviceSize offset) {
    if (array.map) {
        write(static_cast<char*>(array.map) + offset);
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
        region.dstOffset = offset;
        region.size = bytes;
        copyBufferFn(buffer, stagingBuffer_, array.buffer, 1, &region);
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = array.buffer;
        barrier.offset = offset;
        barrier.size = bytes;
        pipelineBarrier(buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                        0, 0, nullptr, 1, &barrier, 0, nullptr);
    });
}

bool Renderer::updateGeometry(const GpuScene& scene) {
    findRuns(scene);
    std::vector<GeometryStamp> next;
    next.reserve(scene.batches.size());
    for (std::size_t i = 0; i < scene.batches.size(); ++i) {
        const auto& batch = scene.batches[i];
        next.push_back({batch.revision, scene.revision,
                        batch.vertices.size(), batch.indices.size(), slots_[i], texturedMaterial(batch.material)});
    }
    dirtyBatch_ = 0;
    while (geometryScene_ == &scene && dirtyBatch_ < next.size() && dirtyBatch_ < geometryStamps_.size() &&
           next[dirtyBatch_] == geometryStamps_[dirtyBatch_]) ++dirtyBatch_;
    if (uploadedValid_ && dirtyBatch_ == next.size() && next.size() == geometryStamps_.size()) {
        // findRuns rebuilt the plan even though no geometry moved.
        for (BatchRun& run : runs_) {
            run.indices = firstIndex_[run.last] + indexCount_[run.last] - firstIndex_[run.first];
        }
        return true;
    }
    // All these arrays are shared; never write or destroy them while a frame uses them.
    if (pendingSlot_ >= 0 && waitForFences(device_, 1, &fences_[pendingSlot_], VK_TRUE, 5000000000ull) != VK_SUCCESS)
        return false;
    if (!uploadedValid_) dirtyBatch_ = 0;
    if (!uploadVertices(scene)) return false;
    geometryStamps_ = std::move(next);
    geometryScene_ = &scene;
    uploadedValid_ = true;
    return true;
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


} // namespace gtc::native::detail
