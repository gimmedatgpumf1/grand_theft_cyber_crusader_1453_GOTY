// A stand-in for a Vulkan driver, enough to run the renderer's control flow.
//
// THIS IS A TEST DOUBLE AND MUST NEVER BE REACHABLE FROM THE CLIENT. It accepts
// every command, invents handles, and points every mapped allocation at one
// static block, so a picture drawn with it is meaningless. It lives under
// `tests/` and is only ever put on LD_LIBRARY_PATH by `tests/run_tests.sh`, into
// its own directory, for the plumbing test in `tests/vulkan_plumbing.cpp`.
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"
#include <cstdint>
#include <cstring>
#include <cstdio>

// One block stands in for every allocation, so it has to be big enough for the
// largest thing the renderer maps -- the vertex buffer of a full skeleton room.
static unsigned char g_memory[256u << 20];
static long g_handle = 1;

static VkResult ok() { return VK_SUCCESS; }
static void nothing() {}

extern "C" {

static VkResult stubCreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance* out) {
    *out = VK_NULL_HANDLE;
    std::memcpy(out, &g_handle, sizeof(void*));
    return VK_SUCCESS;
}
static VkResult stubEnumDevices(VkInstance, std::uint32_t* count, VkPhysicalDevice* devices) {
    if (!devices) { *count = 1; return VK_SUCCESS; }
    *count = 1;
    std::memcpy(&devices[0], &g_handle, sizeof(void*));
    return VK_SUCCESS;
}
static void stubProps(VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
    std::memset(properties, 0, sizeof(*properties));
    properties->apiVersion = VK_API_VERSION_1_0;
    properties->limits.maxPushConstantsSize = 128;
    std::snprintf(properties->deviceName, sizeof(properties->deviceName), "fake vulkan");
}
static void stubFeatures(VkPhysicalDevice, VkPhysicalDeviceFeatures* features) { std::memset(features, 0, sizeof(*features)); }
static void stubQueues(VkPhysicalDevice, std::uint32_t* count, VkQueueFamilyProperties* properties) {
    if (!properties) { *count = 1; return; }
    *count = 1;
    std::memset(&properties[0], 0, sizeof(properties[0]));
    properties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT;
    properties[0].queueCount = 1;
}
static void stubMemory(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* memory) {
    std::memset(memory, 0, sizeof(*memory));
    memory->memoryTypeCount = 2;
    for (int index = 0; index < 2; ++index) {
        memory->memoryTypes[index].propertyFlags =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        memory->memoryTypes[index].heapIndex = 0;
    }
    memory->memoryHeapCount = 1;
    memory->memoryHeaps[0].size = sizeof(g_memory);
    memory->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
}
static void stubFormat(VkPhysicalDevice, VkFormat, VkFormatProperties* properties) {
    properties->linearTilingFeatures = 0;
    properties->optimalTilingFeatures = 0xFFFFFFFFu;
    properties->bufferFeatures = 0;
}
static void stubBufferRequirements(VkDevice, VkBuffer, VkMemoryRequirements* requirements) {
    requirements->size = 1u << 22;
    requirements->alignment = 4;
    requirements->memoryTypeBits = 3;
}
static void stubImageRequirements(VkDevice, VkImage, VkMemoryRequirements* requirements) {
    requirements->size = 1u << 22;
    requirements->alignment = 4;
    requirements->memoryTypeBits = 3;
}
static VkResult stubAllocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* memory) {
    std::memcpy(memory, &g_handle, sizeof(void*));
    return VK_SUCCESS;
}
static VkResult stubMap(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void** data) {
    // A gradient, so the readback path has something recognisable to convert.
    for (std::size_t index = 0; index < sizeof(g_memory); index += 4) {
        g_memory[index + 0] = static_cast<unsigned char>((index >> 2) & 0xFF);
        g_memory[index + 1] = 96;
        g_memory[index + 2] = static_cast<unsigned char>(((index >> 2) >> 8) & 0xFF);
        g_memory[index + 3] = 255;
    }
    *data = g_memory;
    return VK_SUCCESS;
}
static void stubGetQueue(VkDevice, std::uint32_t, std::uint32_t, VkQueue* queue) { std::memcpy(queue, &g_handle, sizeof(void*)); }
static VkResult stubAllocateCommandBuffers(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer* buffers) {
    std::memcpy(&buffers[0], &g_handle, sizeof(void*));
    return VK_SUCCESS;
}
static VkResult stubAllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* sets) {
    for (std::uint32_t index = 0; index < info->descriptorSetCount; ++index) {
        std::memcpy(&sets[index], &g_handle, sizeof(void*));
    }
    return VK_SUCCESS;
}

static VkResult createBufferStub(VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createImageStub(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createImageViewStub(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createShaderStub(VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createRenderPassStub(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createFramebufferStub(VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createLayoutStub(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createPipelineStub(VkDevice, VkPipelineCache, std::uint32_t count, const VkGraphicsPipelineCreateInfo*,
                                   const VkAllocationCallbacks*, VkPipeline* out) {
    for (std::uint32_t index = 0; index < count; ++index) std::memcpy(&out[index], &g_handle, sizeof(void*));
    return VK_SUCCESS;
}
static VkResult createSetLayoutStub(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createPoolStub(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createSamplerStub(VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createCommandPoolStub(VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createFenceStub(VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}
static VkResult createDeviceStub(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice* out) {
    std::memcpy(out, &g_handle, sizeof(void*)); return VK_SUCCESS;
}

static void* lookup(const char* name) {
    if (!std::strcmp(name, "vkCreateInstance")) return (void*)&stubCreateInstance;
    if (!std::strcmp(name, "vkEnumeratePhysicalDevices")) return (void*)&stubEnumDevices;
    if (!std::strcmp(name, "vkGetPhysicalDeviceProperties")) return (void*)&stubProps;
    if (!std::strcmp(name, "vkGetPhysicalDeviceFeatures")) return (void*)&stubFeatures;
    if (!std::strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties")) return (void*)&stubQueues;
    if (!std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties")) return (void*)&stubMemory;
    if (!std::strcmp(name, "vkGetPhysicalDeviceFormatProperties")) return (void*)&stubFormat;
    if (!std::strcmp(name, "vkGetBufferMemoryRequirements")) return (void*)&stubBufferRequirements;
    if (!std::strcmp(name, "vkGetImageMemoryRequirements")) return (void*)&stubImageRequirements;
    if (!std::strcmp(name, "vkAllocateMemory")) return (void*)&stubAllocateMemory;
    if (!std::strcmp(name, "vkMapMemory")) return (void*)&stubMap;
    if (!std::strcmp(name, "vkGetDeviceQueue")) return (void*)&stubGetQueue;
    if (!std::strcmp(name, "vkAllocateCommandBuffers")) return (void*)&stubAllocateCommandBuffers;
    if (!std::strcmp(name, "vkAllocateDescriptorSets")) return (void*)&stubAllocateDescriptorSets;
    if (!std::strcmp(name, "vkCreateBuffer")) return (void*)&createBufferStub;
    if (!std::strcmp(name, "vkCreateImage")) return (void*)&createImageStub;
    if (!std::strcmp(name, "vkCreateImageView")) return (void*)&createImageViewStub;
    if (!std::strcmp(name, "vkCreateShaderModule")) return (void*)&createShaderStub;
    if (!std::strcmp(name, "vkCreateRenderPass")) return (void*)&createRenderPassStub;
    if (!std::strcmp(name, "vkCreateFramebuffer")) return (void*)&createFramebufferStub;
    if (!std::strcmp(name, "vkCreatePipelineLayout")) return (void*)&createLayoutStub;
    if (!std::strcmp(name, "vkCreateGraphicsPipelines")) return (void*)&createPipelineStub;
    if (!std::strcmp(name, "vkCreateDescriptorSetLayout")) return (void*)&createSetLayoutStub;
    if (!std::strcmp(name, "vkCreateDescriptorPool")) return (void*)&createPoolStub;
    if (!std::strcmp(name, "vkCreateSampler")) return (void*)&createSamplerStub;
    if (!std::strcmp(name, "vkCreateCommandPool")) return (void*)&createCommandPoolStub;
    if (!std::strcmp(name, "vkCreateFence")) return (void*)&createFenceStub;
    if (!std::strcmp(name, "vkCreateDevice")) return (void*)&createDeviceStub;
    if (!std::strncmp(name, "vkCmd", 5)) return (void*)&nothing;
    if (!std::strncmp(name, "vkDestroy", 9)) return (void*)&nothing;
    if (!std::strcmp(name, "vkUpdateDescriptorSets")) return (void*)&nothing;
    if (!std::strcmp(name, "vkFreeMemory")) return (void*)&nothing;
    if (!std::strcmp(name, "vkUnmapMemory")) return (void*)&nothing;
    return (void*)&ok;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char* name) {
    return (PFN_vkVoidFunction)lookup(name);
}

} // extern "C"
