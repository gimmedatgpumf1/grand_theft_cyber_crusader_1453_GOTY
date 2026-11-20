// Device setup for the Vulkan backend: loader, instance, physical device,
// device, and the fixed objects that never change size. Kept apart from the
// frame loop so that "can this machine draw at all" is one file to read.
//
// Every step can fail, and every failure is reported rather than assumed: a
// machine without a Vulkan driver gets `false` from start() and the client
// falls back to the CPU path instead of dying.

#include "../client/diagnostics.h"
#include "vk_internal.h"

#if defined(_WIN32)
#include <windows.h>
// The vendored headers carry the platform-neutral surface types but not the
// Win32 one, so it is spelled out here rather than pulling in a second copy of
// the whole loader interface.
typedef struct VkWin32SurfaceCreateInfoKHR {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    HINSTANCE hinstance;
    HWND hwnd;
} VkWin32SurfaceCreateInfoKHR;
typedef VkResult(VKAPI_PTR* PFN_vkCreateWin32SurfaceKHR)(VkInstance, const VkWin32SurfaceCreateInfoKHR*,
                                                        const VkAllocationCallbacks*, VkSurfaceKHR*);
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdio>
#include <cstring>

namespace gtc::native {
namespace detail {
namespace {

// The loader is a different file with a different name on each platform, and
// neither is linked: it is opened at run time so a machine without Vulkan can
// still build and run this client.
#if defined(_WIN32)
constexpr const char* kLoaderName = "vulkan-1.dll";
#else
constexpr const char* kLoaderName = "libvulkan.so.1";
#endif

void* openLibrary(const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(name));
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

void* librarySymbol(void* library, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

void closeLibrary(void* library) {
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

} // namespace

bool Renderer::openLoader() {
    library_ = openLibrary(kLoaderName);
#if !defined(_WIN32)
    // Some distributions ship only the unversioned soname.
    if (!library_) library_ = openLibrary("libvulkan.so");
#endif
    if (!library_) return false;
    getInstanceProcAddr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        librarySymbol(library_, "vkGetInstanceProcAddr"));
    return getInstanceProcAddr_ != nullptr;
}

bool Renderer::createInstance() {
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "gtc-native";
    application.applicationVersion = 1;
    application.pEngineName = "gtc-native";
    application.engineVersion = 1;
    application.apiVersion = VK_API_VERSION_1_0;

    // The surface extensions have to be asked for when the instance is created,
    // before anything has been drawn, so the window hands its handle over first.
    // Presentation is required for interactive frames; if its extensions are
    // absent, refuse startup rather than silently falling back to readback.
    const char* extensions[2] = {nullptr, nullptr};
    std::uint32_t extensionCount = 0;
    if (wsiHandle_ != nullptr && wsiKind_ != 0) {
        extensions[extensionCount++] = "VK_KHR_surface";
#if defined(_WIN32)
        extensions[extensionCount++] = "VK_KHR_win32_surface";
#else
        extensions[extensionCount++] = "VK_KHR_xlib_surface";
#endif
        std::uint32_t offered = 0;
        if (!instanceExtensionOffered(offered, nullptr)) {
            std::fprintf(stderr, "[vk] required window surface extensions are unavailable\n");
            return false;
        }
    }
    VkInstanceCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &application;
    info.enabledExtensionCount = extensionCount;
    info.ppEnabledExtensionNames = extensionCount ? extensions : nullptr;
    VkResult result = createInstanceFn(&info, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[vk] vkCreateInstance failed (%d)\n", static_cast<int>(result));
        return false;
    }
    return true;
}

// Whether the loader offers the two surface extensions, asked before the
// instance exists because that is when they have to be named.
bool Renderer::instanceExtensionOffered(std::uint32_t& count, const char* wanted) {
    const auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        getInstanceProcAddr_(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
    if (enumerate == nullptr) return false;
    std::uint32_t total = 0;
    if (enumerate(nullptr, &total, nullptr) != VK_SUCCESS || total == 0) return false;
    std::vector<VkExtensionProperties> properties(total);
    if (enumerate(nullptr, &total, properties.data()) != VK_SUCCESS) return false;
    count = total;
    if (wanted == nullptr) {
        // Callers pass null to mean "are the surface ones there": check both.
        const auto has = [&](const char* name) {
            for (const VkExtensionProperties& property : properties) {
                if (std::strcmp(property.extensionName, name) == 0) return true;
            }
            return false;
        };
#if defined(_WIN32)
        return has("VK_KHR_surface") && has("VK_KHR_win32_surface");
#else
        return has("VK_KHR_surface") && has("VK_KHR_xlib_surface");
#endif
    }
    for (const VkExtensionProperties& property : properties) {
        if (std::strcmp(property.extensionName, wanted) == 0) return true;
    }
    return false;
}

bool Renderer::chooseDevice() {
    std::uint32_t count = 0;
    if (enumeratePhysicalDevices(instance_, &count, nullptr) != VK_SUCCESS || count == 0) return false;
    std::vector<VkPhysicalDevice> devices(count);
    if (enumeratePhysicalDevices(instance_, &count, devices.data()) != VK_SUCCESS) return false;

    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        getPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_0) continue;
        if (properties.limits.maxPushConstantsSize < sizeof(PushConstants)) continue;
        std::uint32_t families = 0;
        getPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        if (families == 0) continue;
        std::vector<VkQueueFamilyProperties> queues(families);
        getPhysicalDeviceQueueFamilyProperties(candidate, &families, queues.data());
        for (std::uint32_t index = 0; index < families; ++index) {
            constexpr VkQueueFlags required=VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT;
            if ((queues[index].queueFlags & required) != required) continue;
            physical_ = candidate;
            family_ = index;
            break;
        }
        if (physical_ != VK_NULL_HANDLE) break;
    }
    if (physical_ == VK_NULL_HANDLE) return false;

    // A colour format this device can render into and copy out of. Two things
    // are being chosen here, and both are worth the line they cost:
    //
    // * sRGB, so the transfer curve is applied by the device as it writes a
    //   pixel. The fragment stage used to spend three dependent texture lookups
    //   per pixel in a table of the same curve; the hardware does it for nothing
    //   and in the format the memory is already in.
    // * BGRA first, because that is the byte order the window takes. The
    //   readback then needs no channel swap, and the swapchain handover is a
    //   copy of identical bytes instead of a blit that converts them.
    const VkFormat candidates[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
                                   VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    const VkFormatFeatureFlags wanted = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    colourFormat_ = VK_FORMAT_UNDEFINED;
    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        getPhysicalDeviceFormatProperties(physical_, format, &properties);
        if ((properties.optimalTilingFeatures & wanted) == wanted) {
            colourFormat_ = format;
            break;
        }
    }
    if (!isColourFormat(colourFormat_)) return false;
    bgra_ = isBgraFormat(colourFormat_);
    tracef("vk: colour format %s, %s readback", colourFormat_ == VK_FORMAT_B8G8R8A8_SRGB ? "B8G8R8A8_SRGB"
          : colourFormat_ == VK_FORMAT_R8G8B8A8_SRGB ? "R8G8B8A8_SRGB"
          : colourFormat_ == VK_FORMAT_B8G8R8A8_UNORM ? "B8G8R8A8_UNORM" : "R8G8B8A8_UNORM",
          bgra_ ? "no-swap" : "swap");

    // Depth is never stored and never leaves the pass, so D16 halves the depth
    // footprint at no cost but precision. A 32-bit depth is 5.8 MB per frame to
    // touch on an iGPU that shares its memory with the CPU, so the narrower
    // format is worth having -- as an option, until somebody has looked at the
    // joints of a skeleton up close and said it does not z-fight.
    const bool narrowDepth = std::getenv("GTC_VK_DEPTH16") != nullptr;
    const VkFormat depths[] = {narrowDepth ? VK_FORMAT_D16_UNORM : VK_FORMAT_D32_SFLOAT,
                               narrowDepth ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_D16_UNORM};
    for (VkFormat format : depths) {
        VkFormatProperties properties{};
        getPhysicalDeviceFormatProperties(physical_, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            depthFormat_ = format;
            break;
        }
    }
    VkFormatProperties depthProperties{};
    getPhysicalDeviceFormatProperties(physical_, depthFormat_, &depthProperties);
    if ((depthProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) return false;
    return true;
}

bool Renderer::createDevice() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = family_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    VkDeviceCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    // The swapchain extension is named only when the window's surface was
    // accepted, which happens before this call: a device without it draws into
    // an image this process owns, as it always has.
    const char* extensions[1] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    info.enabledExtensionCount = surfaceUsable_ ? 1u : 0u;
    info.ppEnabledExtensionNames = surfaceUsable_ ? extensions : nullptr;
    if (createDeviceFn(physical_, &info, nullptr, &device_) != VK_SUCCESS) {
        device_ = VK_NULL_HANDLE;
        return false;
    }
    getDeviceQueue(device_, family_, 0, &queue_);
    return queue_ != VK_NULL_HANDLE;
}

bool Renderer::createLayouts() {
    VkDescriptorSetLayoutBinding frameBinding{};
    frameBinding.binding = 0;
    frameBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBinding.descriptorCount = 1;
    frameBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 1 is the sRGB transfer table: fixed for the life of the renderer,
    // in the frame set so every material set does not have to carry a copy.
    VkDescriptorSetLayoutBinding lutBinding{};
    lutBinding.binding = 1;
    lutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    lutBinding.descriptorCount = 1;
    lutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 2 is the bone array: one matrix per rigid batch, so a whole body
    // is a single draw instead of one per bone. It lives in the frame block's
    // buffer, past the end of it, and is only read by the vertex stage.
    VkDescriptorSetLayoutBinding boneBinding{};
    boneBinding.binding = 2;
    boneBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneBinding.descriptorCount = 1;
    boneBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    const VkDescriptorSetLayoutBinding frameBindings[3] = {frameBinding, lutBinding, boneBinding};
    VkDescriptorSetLayoutCreateInfo frameInfo{};

    frameInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frameInfo.bindingCount = 3;
    frameInfo.pBindings = frameBindings;
    if (createDescriptorSetLayout(device_, &frameInfo, nullptr, &frameLayout_) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding textureBinding{};
    textureBinding.binding = 0;
    textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureBinding.descriptorCount = 1;
    textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo textureInfo{};

    textureInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    textureInfo.bindingCount = 1;
    textureInfo.pBindings = &textureBinding;
    if (createDescriptorSetLayout(device_, &textureInfo, nullptr, &textureLayout_) != VK_SUCCESS) return false;

    VkDescriptorPoolSize sizes[3]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = 16;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = 257;
    sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[2].descriptorCount = 16;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 272;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = sizes;
    if (createDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo setInfo{};

    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = descriptorPool_;
    VkDescriptorSetLayout layouts[kFramesInFlight];
    for (auto& layout : layouts) layout = frameLayout_;
    setInfo.descriptorSetCount = kFramesInFlight;
    setInfo.pSetLayouts = layouts;
    return allocateDescriptorSets(device_, &setInfo, frameSets_) == VK_SUCCESS;
}


bool Renderer::createCommandPool() {
    VkCommandPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = family_;
    if (createCommandPoolFn(device_, &info, nullptr, &commandPool_) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = commandPool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = kFramesInFlight + 1;
    VkCommandBuffer all[kFramesInFlight + 1] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    if (allocateCommandBuffers(device_, &allocation, all) != VK_SUCCESS) return false;
    for (int slot = 0; slot < kFramesInFlight; ++slot) commands_[slot] = all[slot];
    uploadCommand_ = all[kFramesInFlight];
    command_ = commands_[0];
    // One fence per cell of the pipeline. They start signalled and are reset
    // only once the work that used them has been collected.
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int slot = 0; slot < kFramesInFlight; ++slot) {
        if (createFence(device_, &fence, nullptr, &fences_[slot]) != VK_SUCCESS) return false;
    }
    if (createFence(device_, &fence, nullptr, &uploadFence_) != VK_SUCCESS) return false;
    fence_ = fences_[0];
    return true;
}

bool Renderer::createSampler() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxAnisotropy = 1.0f;
    info.maxLod = 0.0f;
    info.minLod = 0.0f;
    return createSamplerFn(device_, &info, nullptr, &sampler_) == VK_SUCCESS;
}

bool Renderer::createFrameResources() {
    for (int slot = 0; slot < kFramesInFlight; ++slot) {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = kSlotOffset + kSlotBlock;
        info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (createBuffer(device_, &info, nullptr, &frameBuffers_[slot]) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements{};
        getBufferMemoryRequirements(device_, frameBuffers_[slot], &requirements);
        VkPhysicalDeviceMemoryProperties memory{};
        getPhysicalDeviceMemoryProperties(physical_, &memory);
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        std::uint32_t type = memory.memoryTypeCount;
        for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
            if ((requirements.memoryTypeBits & (1u << index)) &&
                (memory.memoryTypes[index].propertyFlags & wanted) == wanted) { type = index; break; }
        }
        if (type == memory.memoryTypeCount) return false;
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        if (allocateMemory(device_, &allocation, nullptr, &frameMemories_[slot]) != VK_SUCCESS) return false;
        if (bindBufferMemory(device_, frameBuffers_[slot], frameMemories_[slot], 0) != VK_SUCCESS) return false;
        if (mapMemory(device_, frameMemories_[slot], 0, VK_WHOLE_SIZE, 0,
                      reinterpret_cast<void**>(&frameMaps_[slot])) != VK_SUCCESS) return false;
        VkDescriptorBufferInfo buffer{frameBuffers_[slot], 0, sizeof(FrameUniform)};
        VkDescriptorBufferInfo bones{frameBuffers_[slot], kSlotOffset, kSlotBlock};
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = frameSets_[slot];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &buffer;
        writes[1] = writes[0];
        writes[1].dstBinding = 2;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &bones;
        updateDescriptorSets(device_, 2, writes, 0, nullptr);
    }
    return true;
}

bool Renderer::start() {
    if (ready_) return true;
    trace("vk: opening the loader");
    if (!openLoader()) {
        std::fprintf(stderr, "[vk] no Vulkan loader on this machine\n");
        trace("vk: no loader");
        return false;
    }
    if (!resolve(createInstanceFn, "vkCreateInstance")) return false;
    trace("vk: creating the instance");
    if (!createInstance()) return false;
    trace("vk: instance created");
    // Everything after the instance is resolved through the instance, which is
    // how a runtime that only ships a loader still hands over a full device.
    if (!resolve(destroyInstance, "vkDestroyInstance") ||
        !resolve(enumeratePhysicalDevices, "vkEnumeratePhysicalDevices") ||
        !resolve(getPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties") ||
        !resolve(getPhysicalDeviceFeatures, "vkGetPhysicalDeviceFeatures") ||
        !resolve(getPhysicalDeviceQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties") ||
        !resolve(getPhysicalDeviceMemoryProperties, "vkGetPhysicalDeviceMemoryProperties") ||
        !resolve(getPhysicalDeviceFormatProperties, "vkGetPhysicalDeviceFormatProperties") ||
        !resolve(createDeviceFn, "vkCreateDevice") ||
        !resolve(destroyDevice, "vkDestroyDevice") ||
        !resolve(getDeviceQueue, "vkGetDeviceQueue") ||
        !resolve(deviceWaitIdle, "vkDeviceWaitIdle") ||
        !resolve(createBuffer, "vkCreateBuffer") ||
        !resolve(destroyBuffer, "vkDestroyBuffer") ||
        !resolve(getBufferMemoryRequirements, "vkGetBufferMemoryRequirements") ||
        !resolve(createImage, "vkCreateImage") ||
        !resolve(destroyImage, "vkDestroyImage") ||
        !resolve(getImageMemoryRequirements, "vkGetImageMemoryRequirements") ||
        !resolve(createImageView, "vkCreateImageView") ||
        !resolve(destroyImageView, "vkDestroyImageView") ||
        !resolve(allocateMemory, "vkAllocateMemory") ||
        !resolve(freeMemory, "vkFreeMemory") ||
        !resolve(bindBufferMemory, "vkBindBufferMemory") ||
        !resolve(bindImageMemory, "vkBindImageMemory") ||
        !resolve(mapMemory, "vkMapMemory") ||
        !resolve(unmapMemory, "vkUnmapMemory") ||
        !resolve(createShaderModule, "vkCreateShaderModule") ||
        !resolve(destroyShaderModule, "vkDestroyShaderModule") ||
        !resolve(createRenderPassFn, "vkCreateRenderPass") ||
        !resolve(destroyRenderPass, "vkDestroyRenderPass") ||
        !resolve(createFramebuffer, "vkCreateFramebuffer") ||
        !resolve(destroyFramebuffer, "vkDestroyFramebuffer") ||
        !resolve(createPipelineLayout, "vkCreatePipelineLayout") ||
        !resolve(destroyPipelineLayout, "vkDestroyPipelineLayout") ||
        !resolve(createGraphicsPipelines, "vkCreateGraphicsPipelines") ||
        !resolve(destroyPipeline, "vkDestroyPipeline") ||
        !resolve(createDescriptorSetLayout, "vkCreateDescriptorSetLayout") ||
        !resolve(destroyDescriptorSetLayout, "vkDestroyDescriptorSetLayout") ||
        !resolve(createDescriptorPool, "vkCreateDescriptorPool") ||
        !resolve(destroyDescriptorPool, "vkDestroyDescriptorPool") ||
        !resolve(resetDescriptorPool, "vkResetDescriptorPool") ||
        !resolve(allocateDescriptorSets, "vkAllocateDescriptorSets") ||
        !resolve(updateDescriptorSets, "vkUpdateDescriptorSets") ||
        !resolve(createSamplerFn, "vkCreateSampler") ||
        !resolve(destroySampler, "vkDestroySampler") ||
        !resolve(createCommandPoolFn, "vkCreateCommandPool") ||
        !resolve(destroyCommandPool, "vkDestroyCommandPool") ||
        !resolve(allocateCommandBuffers, "vkAllocateCommandBuffers") ||
        !resolve(resetCommandBuffer, "vkResetCommandBuffer") ||
        !resolve(beginCommandBuffer, "vkBeginCommandBuffer") ||
        !resolve(endCommandBuffer, "vkEndCommandBuffer") ||
        !resolve(beginRenderPass, "vkCmdBeginRenderPass") ||
        !resolve(endRenderPass, "vkCmdEndRenderPass") ||
        !resolve(bindPipeline, "vkCmdBindPipeline") ||
        !resolve(bindVertexBuffers, "vkCmdBindVertexBuffers") ||
        !resolve(bindDescriptorSets, "vkCmdBindDescriptorSets") ||
        !resolve(pushConstants, "vkCmdPushConstants") ||
        !resolve(drawFn, "vkCmdDraw") ||
        !resolve(drawIndexedFn, "vkCmdDrawIndexed") ||
        !resolve(bindIndexBuffer, "vkCmdBindIndexBuffer") ||
        !resolve(copyImageToBuffer, "vkCmdCopyImageToBuffer") ||
        !resolve(copyBufferToImage, "vkCmdCopyBufferToImage") ||
        !resolve(copyBufferFn, "vkCmdCopyBuffer") ||
        !resolve(pipelineBarrier, "vkCmdPipelineBarrier") ||
        !resolve(setViewport, "vkCmdSetViewport") ||
        !resolve(setScissor, "vkCmdSetScissor") ||
        !resolve(queueSubmit, "vkQueueSubmit") ||
        !resolve(queueWaitIdle, "vkQueueWaitIdle") ||
        !resolve(createFence, "vkCreateFence") ||
        !resolve(destroyFence, "vkDestroyFence") ||
        !resolve(waitForFences, "vkWaitForFences") ||
        !resolve(resetFences, "vkResetFences")) {
        stop();
        return false;
    }
    trace("vk: choosing a device");
    if (!chooseDevice()) {
        trace("vk: no usable device");
        stop();
        return false;
    }
    // Before the device exists: the swapchain extension has to be named at
    // device creation, and a surface the driver will not present to is better
    // known now than after the first frame.
    if (presentWanted_ || wsiHandle_ != nullptr) prepareSurface();
    if (!createDevice()) {
        trace("vk: no usable device");
        stop();
        return false;
    }
    trace("vk: device created");
    if (!createLayouts()) { trace("vk: layouts failed"); stop(); return false; }
    if (!createRenderPass()) { trace("vk: render pass failed"); stop(); return false; }
    if (!createPipeline()) { trace("vk: pipeline failed"); stop(); return false; }
    if (!createCommandPool()) { trace("vk: command pool failed"); stop(); return false; }
    if (!createSampler()) { trace("vk: sampler failed"); stop(); return false; }
    if (!createTransferTable()) { trace("vk: transfer table failed"); stop(); return false; }
    if (!createFrameResources()) { trace("vk: frame resources failed"); stop(); return false; }
    trace("vk: ready");
    ready_ = true;
    return true;
}

void Renderer::stop() {
    destroySwapchain();
    if (device_ != VK_NULL_HANDLE) {
        deviceWaitIdle(device_);
        for (DeviceArray* array : {&vertex_, &uv_, &index_}) {
            if (array->buffer) destroyBuffer(device_, array->buffer, nullptr);
            if (array->memory) freeMemory(device_, array->memory, nullptr);
            *array = DeviceArray{};
        }
        if (stagingBuffer_) destroyBuffer(device_, stagingBuffer_, nullptr);
        if (stagingMemory_) freeMemory(device_, stagingMemory_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        stagingMemory_ = VK_NULL_HANDLE;
        stagingMap_ = nullptr;
        stagingCapacity_ = 0;
        for (auto& entry : textures_) {
            if (entry.second.view) destroyImageView(device_, entry.second.view, nullptr);
            if (entry.second.image) destroyImage(device_, entry.second.image, nullptr);
            if (entry.second.memory) freeMemory(device_, entry.second.memory, nullptr);
        }
        textures_.clear();
        if (white_.view) destroyImageView(device_, white_.view, nullptr);
        if (white_.image) destroyImage(device_, white_.image, nullptr);
        if (white_.memory) freeMemory(device_, white_.memory, nullptr);
        white_ = TextureSlot{};
        if (interfaceAtlas_.view) destroyImageView(device_, interfaceAtlas_.view, nullptr);
        if (interfaceAtlas_.image) destroyImage(device_, interfaceAtlas_.image, nullptr);
        if (interfaceAtlas_.memory) freeMemory(device_, interfaceAtlas_.memory, nullptr);
        interfaceAtlas_ = TextureSlot{};
        interfaceAtlasReady_ = false;
        if (transfer_.view) destroyImageView(device_, transfer_.view, nullptr);
        if (transfer_.image) destroyImage(device_, transfer_.image, nullptr);
        if (transfer_.memory) freeMemory(device_, transfer_.memory, nullptr);
        transfer_ = TextureSlot{};
        destroyTargets();
        for (int slot = 0; slot < kFramesInFlight; ++slot) {
            if (frameBuffers_[slot]) destroyBuffer(device_, frameBuffers_[slot], nullptr);
            if (frameMemories_[slot]) freeMemory(device_, frameMemories_[slot], nullptr);
            frameBuffers_[slot] = VK_NULL_HANDLE;
            frameMemories_[slot] = VK_NULL_HANDLE;
            frameMaps_[slot] = nullptr;
        }
        frameMap_ = nullptr;
        boneMap_ = nullptr;
        if (pipeline_) destroyPipeline(device_, pipeline_, nullptr);
        if (pipelineCovered_) destroyPipeline(device_, pipelineCovered_, nullptr);
        if (pipelineUv_) destroyPipeline(device_, pipelineUv_, nullptr);
        if (pipelineUvCovered_) destroyPipeline(device_, pipelineUvCovered_, nullptr);
        if (pipelineSkin_) destroyPipeline(device_, pipelineSkin_, nullptr);
        if (pipelineSkinCovered_) destroyPipeline(device_, pipelineSkinCovered_, nullptr);
        if (pipelineTranslucent_) destroyPipeline(device_, pipelineTranslucent_, nullptr);
        if (pipelineTranslucentCovered_) destroyPipeline(device_, pipelineTranslucentCovered_, nullptr);
        if (pipelineInterface_) destroyPipeline(device_, pipelineInterface_, nullptr);
        if (pipelineInterfaceCovered_) destroyPipeline(device_, pipelineInterfaceCovered_, nullptr);
        if (pipelineInterfaceUv_) destroyPipeline(device_, pipelineInterfaceUv_, nullptr);
        if (pipelineInterfaceUvCovered_) destroyPipeline(device_, pipelineInterfaceUvCovered_, nullptr);
        if (pipelineLayout_) destroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (renderPass_) destroyRenderPass(device_, renderPass_, nullptr);
        if (renderPassCovered_) destroyRenderPass(device_, renderPassCovered_, nullptr);
        // The pool owns every set it handed out, so destroying the pool is
        // enough; the layouts are separate objects and have to go as well.
        if (descriptorPool_) destroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (frameLayout_) destroyDescriptorSetLayout(device_, frameLayout_, nullptr);
        if (textureLayout_) destroyDescriptorSetLayout(device_, textureLayout_, nullptr);
        if (sampler_) destroySampler(device_, sampler_, nullptr);
        if (commandPool_) destroyCommandPool(device_, commandPool_, nullptr);
        if (uploadFence_) destroyFence(device_, uploadFence_, nullptr);
        uploadFence_ = VK_NULL_HANDLE;
        uploadCommand_ = VK_NULL_HANDLE;
        for (int slot = 0; slot < kFramesInFlight; ++slot) {
            if (fences_[slot]) destroyFence(device_, fences_[slot], nullptr);
            fences_[slot] = VK_NULL_HANDLE;
            fenceArmed_[slot] = false;
            commands_[slot] = VK_NULL_HANDLE;
        }
        fence_ = VK_NULL_HANDLE;
        pendingSlot_ = -1;
        if (device_ != VK_NULL_HANDLE) destroyDevice(device_, nullptr);
    }
    descriptorPool_ = VK_NULL_HANDLE;
    frameLayout_ = VK_NULL_HANDLE;
    textureLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    pipelineCovered_ = VK_NULL_HANDLE;
    pipelineUv_ = VK_NULL_HANDLE;
    pipelineUvCovered_ = VK_NULL_HANDLE;
    pipelineSkin_ = VK_NULL_HANDLE;
    pipelineSkinCovered_ = VK_NULL_HANDLE;
    pipelineTranslucent_ = VK_NULL_HANDLE;
    pipelineTranslucentCovered_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    renderPassCovered_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    command_ = VK_NULL_HANDLE;
    fence_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    // The surface is an instance child: it has to go before the instance does.
    if (surface_ != VK_NULL_HANDLE && destroySurface_) {
        destroySurface_(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE && destroyInstance) destroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    if (library_) closeLibrary(library_);
    library_ = nullptr;
    getInstanceProcAddr_ = nullptr;
    ready_ = false;
    geometryStamps_.clear();
    nextGeometryStamps_.clear();
    geometryScene_ = nullptr;
    uploadedValid_ = false;
    frames_ = 0;
}

} // namespace detail
} // namespace gtc::native
