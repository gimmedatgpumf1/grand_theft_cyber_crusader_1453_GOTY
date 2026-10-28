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
#include "shaders/spirv_shaders.h"

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

// A generated module gets one last look before it reaches the driver: the magic
// number, the word count of every instruction, and the declared bound. If the
// generator ever regresses, this refuses the module at startup instead of
// handing a driver something it will reject with a validation error nobody in
// this process can see.
bool moduleSane(const std::uint32_t* words, std::size_t count) {
    if (count < 5 || words[0] != 0x07230203u || words[3] == 0) return false;
    std::size_t offset = 5;
    while (offset < count) {
        const std::uint32_t length = words[offset] >> 16;
        if (length == 0 || offset + length > count) return false;
        offset += length;
    }
    return offset == count;
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
    // Only names the loader actually offers are requested: asking for one this
    // machine does not have would fail instance creation and cost the whole
    // backend, when the only thing at stake is whether the picture can skip
    // host memory.
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
            extensions[1] = nullptr;
            extensionCount = 0;
            wsiHandle_ = nullptr;
            wsiKind_ = 0;
        }
    }
    VkInstanceCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &application;
    info.enabledExtensionCount = extensionCount;
    info.ppEnabledExtensionNames = extensionCount ? extensions : nullptr;
    VkResult result = createInstanceFn(&info, nullptr, &instance_);
    if (result != VK_SUCCESS && extensionCount > 0) {
        // Give the surface up rather than the device.
        std::fprintf(stderr, "[vk] instance rejected the surface extensions (%d), continuing without them\n",
                     static_cast<int>(result));
        wsiHandle_ = nullptr;
        wsiKind_ = 0;
        info.enabledExtensionCount = 0;
        info.ppEnabledExtensionNames = nullptr;
        result = createInstanceFn(&info, nullptr, &instance_);
    }
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
            if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) continue;
            physical_ = candidate;
            family_ = index;
            break;
        }
        if (physical_ != VK_NULL_HANDLE) break;
    }
    if (physical_ == VK_NULL_HANDLE) return false;

    // A colour format this device can render into and copy out of, preferring
    // RGBA so the readback needs no channel swap.
    const VkFormat candidates[] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM};
    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        getPhysicalDeviceFormatProperties(physical_, format, &properties);
        const VkFormatFeatureFlags wanted = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        if ((properties.optimalTilingFeatures & wanted) == wanted) {
            colourFormat_ = format;
            bgra_ = format == VK_FORMAT_B8G8R8A8_UNORM;
            break;
        }
    }
    if (colourFormat_ != VK_FORMAT_R8G8B8A8_UNORM && colourFormat_ != VK_FORMAT_B8G8R8A8_UNORM) return false;

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

    const VkDescriptorSetLayoutBinding frameBindings[2] = {frameBinding, lutBinding};
    VkDescriptorSetLayoutCreateInfo frameInfo{};

    frameInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frameInfo.bindingCount = 2;
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

    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = 16;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = 257;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 272;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    if (createDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo setInfo{};

    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = descriptorPool_;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &frameLayout_;
    return allocateDescriptorSets(device_, &setInfo, &frameSet_) == VK_SUCCESS;
}

bool Renderer::createPipeline() {
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    if (!moduleSane(vk::spirv::skeleton_vert,
                    sizeof(vk::spirv::skeleton_vert) / sizeof(vk::spirv::skeleton_vert[0])) ||
        !moduleSane(vk::spirv::skeleton_frag,
                    sizeof(vk::spirv::skeleton_frag) / sizeof(vk::spirv::skeleton_frag[0]))) {
        std::fprintf(stderr, "[vk] generated shader module is malformed\n");
        return false;
    }
    VkShaderModuleCreateInfo vertexInfo{};
    vertexInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertexInfo.codeSize = sizeof(vk::spirv::skeleton_vert);
    vertexInfo.pCode = vk::spirv::skeleton_vert;
    VkShaderModuleCreateInfo fragmentInfo{};
    fragmentInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragmentInfo.codeSize = sizeof(vk::spirv::skeleton_frag);
    fragmentInfo.pCode = vk::spirv::skeleton_frag;
    if (createShaderModule(device_, &vertexInfo, nullptr, &vertexModule) != VK_SUCCESS) return false;
    if (createShaderModule(device_, &fragmentInfo, nullptr, &fragmentModule) != VK_SUCCESS) {
        destroyShaderModule(device_, vertexModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    // One interleaved vertex: position, normal, texture coordinate -- the same
    // GpuVertex the OpenGL path uploads.
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(GpuVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(GpuVertex, x);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(GpuVertex, nx);
    attributes[2].location = 2;
    attributes[2].binding = 0;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = offsetof(GpuVertex, u);

    VkPipelineVertexInputStateCreateInfo vertexInput{};

    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo assembly{};

    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport{};

    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};

    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    // Neither side is culled: the CPU path draws whatever order the triangles
    // arrive in, and the shading is two-sided, so a back face is still a face.
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};

    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};

    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_FALSE;
    blend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    range.offset = 0;
    range.size = sizeof(PushConstants);

    const VkDescriptorSetLayout layouts[2] = {frameLayout_, textureLayout_};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = layouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    if (createPipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        destroyShaderModule(device_, vertexModule, nullptr);
        destroyShaderModule(device_, fragmentModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = pipelineLayout_;
    info.renderPass = renderPass_;
    info.subpass = 0;
    const VkResult result = createGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_);
    destroyShaderModule(device_, vertexModule, nullptr);
    destroyShaderModule(device_, fragmentModule, nullptr);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[vk] pipeline creation failed (%d)\n", static_cast<int>(result));
        return false;
    }
    return true;
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
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = sizeof(FrameUniform);
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (createBuffer(device_, &info, nullptr, &frameBuffer_) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    getBufferMemoryRequirements(device_, frameBuffer_, &requirements);
    VkPhysicalDeviceMemoryProperties memory{};
    getPhysicalDeviceMemoryProperties(physical_, &memory);
    std::uint32_t type = 0;
    const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    bool found = false;
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        if ((requirements.memoryTypeBits & (1u << index)) == 0) continue;
        if ((memory.memoryTypes[index].propertyFlags & wanted) != wanted) continue;
        type = index;
        found = true;
        break;
    }
    if (!found) return false;
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    if (allocateMemory(device_, &allocation, nullptr, &frameMemory_) != VK_SUCCESS) return false;
    if (bindBufferMemory(device_, frameBuffer_, frameMemory_, 0) != VK_SUCCESS) return false;
    if (mapMemory(device_, frameMemory_, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&frameMap_)) != VK_SUCCESS) {
        return false;
    }
    VkDescriptorBufferInfo buffer{};
    buffer.buffer = frameBuffer_;
    buffer.offset = 0;
    buffer.range = sizeof(FrameUniform);
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = frameSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &buffer;
    updateDescriptorSets(device_, 1, &write, 0, nullptr);
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
    if (surfaceUsable_) std::printf("[vk] surface: usable, frames go to the window without a readback\n");
    return true;
}

void Renderer::stop() {
    destroySwapchain();
    if (device_ != VK_NULL_HANDLE) {
        deviceWaitIdle(device_);
        if (vertexBuffer_) destroyBuffer(device_, vertexBuffer_, nullptr);
        if (stagingBuffer_) destroyBuffer(device_, stagingBuffer_, nullptr);
        if (stagingMemory_) freeMemory(device_, stagingMemory_, nullptr);
        stagingBuffer_ = VK_NULL_HANDLE;
        stagingMemory_ = VK_NULL_HANDLE;
        stagingMap_ = nullptr;
        stagingCapacity_ = 0;
        if (vertexMemory_) freeMemory(device_, vertexMemory_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
        vertexMemory_ = VK_NULL_HANDLE;
        vertexMap_ = nullptr;
        vertexCapacity_ = 0;
        for (auto& entry : textures_) {
            if (entry.second.view) destroyImageView(device_, entry.second.view, nullptr);
            if (entry.second.image) destroyImage(device_, entry.second.image, nullptr);
            if (entry.second.memory) freeMemory(device_, entry.second.memory, nullptr);
        }
        textures_.clear();
        if (transfer_.view) destroyImageView(device_, transfer_.view, nullptr);
        if (transfer_.image) destroyImage(device_, transfer_.image, nullptr);
        if (transfer_.memory) freeMemory(device_, transfer_.memory, nullptr);
        transfer_ = TextureSlot{};
        destroyTargets();
        if (frameBuffer_) destroyBuffer(device_, frameBuffer_, nullptr);
        if (frameMemory_) freeMemory(device_, frameMemory_, nullptr);
        frameBuffer_ = VK_NULL_HANDLE;
        frameMemory_ = VK_NULL_HANDLE;
        frameMap_ = nullptr;
        if (pipeline_) destroyPipeline(device_, pipeline_, nullptr);
        if (pipelineLayout_) destroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (renderPass_) destroyRenderPass(device_, renderPass_, nullptr);
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
    pipelineLayout_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
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
}

} // namespace detail
} // namespace gtc::native
