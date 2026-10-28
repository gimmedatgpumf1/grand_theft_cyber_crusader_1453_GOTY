#pragma once

// The Vulkan backend's private shape: what the loader, the device setup and the
// frame loop share. The public seam is `vk_renderer.h`; nothing from here is
// used above it.

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "../vendor/vulkan/include/vulkan/vulkan.h"

#include "vk_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace gtc::native {
namespace detail {

// The frustum the OpenGL path has always used, kept identical so the two
// backends frame the same picture.
constexpr double kNearPlane = 0.035;
constexpr double kFarPlane = 70.0;

// What the vertex stage reads from set 0: the frame matrix, then the eye point
// the two-sided normal flip needs. Laid out to match the shader's block.
struct FrameUniform {
    float projection[16];
    float eye[4];
};
static_assert(sizeof(FrameUniform) == 80, "frame block must match the shader");

// What both stages read from the push constant block, in the order the shader
// declares: model matrix, colour, then the flag word.
struct PushConstants {
    float model[16];
    float colour[4];
    float flags[4];
};
static_assert(sizeof(PushConstants) == 96, "push block must match the shader");

inline double srgbEncode(double value) {
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

// The same Reinhard fit and transfer the fragment shader applies, so a cleared
// background and a drawn one are the same colour.
inline double displayValue(double linear) {
    const double value = std::max(0.0, linear);
    const double tone = std::clamp((value * (2.51 * value + 0.03)) /
        (value * (2.43 * value + 0.59) + 0.14), 0.0, 1.0);
    return srgbEncode(tone);
}

struct Matrix4 {
    // Row-major while it is being built, because that is how the frustum is
    // written down; column-major when it is handed to SPIR-V, which is how a
    // mat4 is stored in a block.
    double cell[4][4]{};

    static Matrix4 identity() {
        Matrix4 value;
        for (int i = 0; i < 4; ++i) value.cell[i][i] = 1.0;
        return value;
    }

    Matrix4 operator*(const Matrix4& other) const {
        Matrix4 result;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k) sum += cell[row][k] * other.cell[k][column];
                result.cell[row][column] = sum;
            }
        }
        return result;
    }

    void columns(float* out) const {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) out[column * 4 + row] = static_cast<float>(cell[row][column]);
        }
    }
};

inline Matrix4 fromColumnMajor(const std::array<double, 16>& values) {
    Matrix4 result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) result.cell[row][column] = values[column * 4 + row];
    }
    return result;
}

// The frustum the OpenGL path builds with glFrustum, but arranged for Vulkan's
// clip volume: z in [0, w] instead of [-w, w], and y pointing down the image.
inline Matrix4 projectionFor(const GpuScene& scene) {
    const double nearPlane = kNearPlane;
    const double horizontal = nearPlane * scene.width / (2.0 * std::max(1.0, scene.focal));
    const double vertical = nearPlane * scene.height / (2.0 * std::max(1.0, scene.focal));
    const double left = -horizontal, right = horizontal;
    const double bottom = -vertical, top = vertical;
    Matrix4 frustum;
    frustum.cell[0][0] = 2.0 * nearPlane / (right - left);
    frustum.cell[1][1] = 2.0 * nearPlane / (top - bottom);
    frustum.cell[2][0] = (right + left) / (right - left);
    frustum.cell[2][1] = (top + bottom) / (top - bottom);
    frustum.cell[2][2] = -(kFarPlane + nearPlane) / (kFarPlane - nearPlane);
    frustum.cell[2][3] = -2.0 * kFarPlane * nearPlane / (kFarPlane - nearPlane);
    frustum.cell[3][2] = -1.0;

    // Camera::viewMatrix() looks down positive z, so the view handed to the
    // shader carries a z flip, exactly as the GL path multiplies one in.
    Matrix4 flip = Matrix4::identity();
    flip.cell[2][2] = -1.0;
    Matrix4 clip = frustum * flip * fromColumnMajor(scene.viewMatrix);
    // Vulkan's clip space differs from OpenGL's in two ways, both of them a
    // half-space: depth moves to [0, w], and y runs down the image.
    for (int column = 0; column < 4; ++column) {
        const double depth = 0.5 * clip.cell[2][column] + 0.5 * clip.cell[3][column];
        clip.cell[2][column] = depth;
        clip.cell[1][column] = -clip.cell[1][column];
    }
    return clip;
}

struct TextureSlot {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
};

// How many frames the device may have in flight. One means "submit, then block
// until it is done", which is what this used to do: the GPU sat idle while the
// host copied a frame out, and the host sat idle while the GPU drew. Two lets
// each side work on a different frame at the same time.
constexpr int kFramesInFlight = 2;

struct Target {
    VkImage colour = VK_NULL_HANDLE;
    VkDeviceMemory colourMemory = VK_NULL_HANDLE;
    VkImageView colourView = VK_NULL_HANDLE;
    VkImage depth = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    // One readback buffer per frame in flight: the device writes frame N while
    // the host reads frame N-1, so they must not share memory.
    VkBuffer readback[kFramesInFlight] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory readbackMemory[kFramesInFlight] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    void* readbackMap[kFramesInFlight] = {nullptr, nullptr};
    // The mapped buffer can be write-combined memory, where a byte read costs a
    // bus transaction. The frame is copied out of it in one wide move and
    // converted from here instead.
    std::vector<unsigned char> staging[kFramesInFlight];
    bool readbackCached = false;
    int width = 0;
    int height = 0;
};

// Defined in vk_renderer.cpp; declared here because the frame loop asks it.
extern bool readbackEnabled;

// Stage timing, off unless asked for: `GTC_VK_PROFILE=1`.
bool profiling();

// When the frame has to be copied back to the host anyway, the colour image can
// be kept in linear layout: a tiled image has to be walked by the copy engine,
// while a linear one is copied as memory. Rendering into linear layout costs the
// rasteriser its swizzle, so this is a trade that has to be measured rather than
// assumed -- see the benchmark.
extern bool linearTarget;

// Which memory the readback buffer asks for. The GPU's copy out of the image is
// the expensive half of a readback, and where it lands decides its speed:
//   0 host cached (fast for the CPU read, may be slow for the device write)
//   1 device local + host visible (fastest for the device, plain RAM for us)
//   2 host visible only (write-combined: fastest for the device, worst for us)
extern int readbackMode;

class Renderer {
public:
    bool start();
    bool draw(const GpuScene& scene, NativeFrame& frame);
    void stop();
    // Throws away the offscreen targets so the next frame rebuilds them, which
    // is how a change of layout takes effect.
    // Waits for anything submitted and forgets which slot is pending, leaving
    // the targets in place: they are still valid, and rebuilding them is a
    // millisecond of upload that has no business inside a timed measurement.
    void drain() {
        if (device_ != VK_NULL_HANDLE) deviceWaitIdle(device_);
        pendingSlot_ = -1;
        for (int slot = 0; slot < kFramesInFlight; ++slot) fenceArmed_[slot] = false;
    }

    // Targets cannot be freed while a submitted frame still refers to them.
    // The stub schedules nothing, so only a real driver ever notices -- as a
    // device lost or a frame of garbage.
    void discardTargets() {
        if (device_ != VK_NULL_HANDLE) deviceWaitIdle(device_);
        pendingSlot_ = -1;
        for (int slot = 0; slot < kFramesInFlight; ++slot) fenceArmed_[slot] = false;
        destroyTargets();
    }

    // -- window surface, for the swapchain --------------------------------
    // Deliberately minimal: this asks the driver what it can do and reports it.
    // Nothing here draws, so a driver that answers "no" costs a log line rather
    // than a broken frame.
    // Built once the window has handed over its handle, before the device is
    // created -- the swapchain extension has to be named at device creation,
    // which is earlier than the first frame.
    bool prepareSurface();
    void setSurfaceTarget(void* handle, int kind) { wsiHandle_ = handle; wsiKind_ = kind; }
    // True when this frame was handed to the compositor instead of to the host.
    bool presenting() const { return presentWanted_ && swapReady_; }

private:
    bool openLoader();
    bool submitNow(const std::function<void(VkCommandBuffer)>& body);
    bool createInstance();
    bool instanceExtensionOffered(std::uint32_t& count, const char* wanted);
    bool chooseDevice();
    bool createDevice();
    bool createLayouts();
    bool createPipeline();
    bool createCommandPool();
    bool createSampler();
    bool createFrameResources();
    bool createRenderPass();
    bool createTargets(int width, int height);
    bool createTexture(const Texture& texture, TextureSlot& slot);
    TextureSlot& textureFor(const Material& material);
    bool createTransferTable();
    bool uploadVertices(const GpuScene& scene);
    void destroyTargets();
    // -- swapchain ---------------------------------------------------------
    bool createSwapchain(int width, int height);
    void destroySwapchain();
    // Barrier onto the acquired image, blit, barrier to present. Recorded into
    // the frame's command buffer; the picture never touches host memory.
    void recordPresentBlit(int imageIndex);
    bool acquirePresentImage(int slot, int& imageIndex);
    bool presentImage(int imageIndex);

    bool surfaceRequested() const { return wsiHandle_ != nullptr && wsiKind_ != 0; }

    static constexpr std::uint64_t recordedKeySeed() { return 1469598103934665603ull; }

    template <class Function>
    bool resolve(Function& slot, const char* name) {
        slot = reinterpret_cast<Function>(getInstanceProcAddr_(instance_, name));
        if (!slot) {
            std::fprintf(stderr, "[vk] missing entry point: %s\n", name);
            return false;
        }
        return true;
    }

    // -- surface and swapchain ---------------------------------------------
    // Presenting into a swapchain image removes the entire GPU-to-host path:
    // at 1568x929 the readback is 5.8 MB of PCIe traffic per frame, and the
    // window then reads those bytes again to put them on screen. A driver that
    // will not do it costs one line of log and the readback.
    bool presentWanted_ = false;
    bool surfaceUsable_ = false;
    bool swapReady_ = false;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapExtent_{0, 0};
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;
    // An acquire is announced on the frame slot that made it, a present on the
    // image being handed over: an image that comes back from acquire has had its
    // previous present complete, and a slot's acquire is consumed by that same
    // slot's submission before it is used again.
    std::vector<VkSemaphore> imageReady_;
    std::vector<VkSemaphore> drawDone_;
    // Whether this image has been presented at least once, which decides the
    // layout it is in when it is drawn into again.
    std::vector<char> swapImagePresented_;
    // Which frame slot last drew into each image, so a reused image waits for
    // the submission that wrote it instead of hoping it finished.
    std::vector<int> swapImageSlot_;
    int swapWidth_ = 0;
    int swapHeight_ = 0;


    // -- surface probe -----------------------------------------------------
    void* wsiHandle_ = nullptr;
    int wsiKind_ = 0;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    PFN_vkDestroySurfaceKHR destroySurface_ = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR surfaceSupport_ = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR surfaceCaps_ = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR surfaceFormats_ = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR surfaceModes_ = nullptr;

    // -- loader ------------------------------------------------------------
    void* library_ = nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;

    PFN_vkCreateInstance createInstanceFn = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceFeatures getPhysicalDeviceFeatures = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties getPhysicalDeviceFormatProperties = nullptr;
    PFN_vkCreateDevice createDeviceFn = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkGetDeviceQueue getDeviceQueue = nullptr;
    PFN_vkDeviceWaitIdle deviceWaitIdle = nullptr;
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements = nullptr;
    PFN_vkCreateImage createImage = nullptr;
    PFN_vkDestroyImage destroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements getImageMemoryRequirements = nullptr;
    PFN_vkCreateImageView createImageView = nullptr;
    PFN_vkDestroyImageView destroyImageView = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkBindBufferMemory bindBufferMemory = nullptr;
    PFN_vkBindImageMemory bindImageMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;
    PFN_vkCreateShaderModule createShaderModule = nullptr;
    PFN_vkDestroyShaderModule destroyShaderModule = nullptr;
    PFN_vkCreateRenderPass createRenderPassFn = nullptr;
    PFN_vkDestroyRenderPass destroyRenderPass = nullptr;
    PFN_vkCreateFramebuffer createFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer destroyFramebuffer = nullptr;
    PFN_vkCreatePipelineLayout createPipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout destroyPipelineLayout = nullptr;
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines = nullptr;
    PFN_vkDestroyPipeline destroyPipeline = nullptr;
    PFN_vkCreateDescriptorSetLayout createDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout destroyDescriptorSetLayout = nullptr;
    PFN_vkCreateDescriptorPool createDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool destroyDescriptorPool = nullptr;
    PFN_vkResetDescriptorPool resetDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets allocateDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets updateDescriptorSets = nullptr;
    PFN_vkCreateSampler createSamplerFn = nullptr;
    PFN_vkDestroySampler destroySampler = nullptr;
    PFN_vkCreateCommandPool createCommandPoolFn = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkCmdBeginRenderPass beginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass endRenderPass = nullptr;
    PFN_vkCmdBindPipeline bindPipeline = nullptr;
    PFN_vkCmdBindVertexBuffers bindVertexBuffers = nullptr;
    PFN_vkCmdBindDescriptorSets bindDescriptorSets = nullptr;
    PFN_vkCmdPushConstants pushConstants = nullptr;
    PFN_vkCmdDraw drawFn = nullptr;
    PFN_vkCmdCopyImageToBuffer copyImageToBuffer = nullptr;
    PFN_vkCmdCopyBufferToImage copyBufferToImage = nullptr;
    PFN_vkCmdCopyBuffer copyBufferFn = nullptr;
    PFN_vkCmdPipelineBarrier pipelineBarrier = nullptr;
    PFN_vkCmdSetViewport setViewport = nullptr;
    PFN_vkCmdSetScissor setScissor = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkQueueWaitIdle queueWaitIdle = nullptr;
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkResetFences resetFences = nullptr;
    PFN_vkCreateSemaphore createSemaphoreFn = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
    PFN_vkCmdBlitImage blitImage = nullptr;
    PFN_vkCmdCopyImage copyImage = nullptr;
    PFN_vkCreateSwapchainKHR createSwapchainFn = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapchainFn = nullptr;
    PFN_vkGetSwapchainImagesKHR getSwapchainImages = nullptr;
    PFN_vkAcquireNextImageKHR acquireNextImage = nullptr;
    PFN_vkQueuePresentKHR queuePresent = nullptr;

    // -- device ------------------------------------------------------------
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t family_ = 0;
    VkPhysicalDeviceMemoryProperties memory_{};
    bool memoryQueried_ = false;
    VkFormat colourFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    bool bgra_ = false;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    // The pipelined set: one command buffer and one fence per frame in flight.
    VkCommandBuffer commands_[kFramesInFlight] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkFence fences_[kFramesInFlight] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    // Uploads (textures, vertex staging) run on their own buffer and fence.
    VkCommandBuffer uploadCommand_ = VK_NULL_HANDLE;
    VkFence uploadFence_ = VK_NULL_HANDLE;
    bool fenceArmed_[kFramesInFlight] = {false, false};
    // A recorded frame, and what it was recorded from. Unchanged commands are
    // not recorded twice: at three hundred frames a second the scene sits still
    // long enough for that to matter.
    bool recordedValid_[kFramesInFlight] = {false, false};
    std::uint64_t recordedKey_[kFramesInFlight] = {0, 0};
    // The presented path draws into whichever image came back from acquire, so
    // it keeps one recorded frame per image.
    std::vector<VkCommandBuffer> presentCommands_;
    std::vector<std::uint64_t> presentKey_;
    std::vector<char> presentRecorded_;
    // Which slot holds a submitted frame nobody has collected yet.
    int pendingSlot_ = -1;
    std::uint64_t pendingFrame_ = 0;

    VkDescriptorSetLayout frameLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet frameSet_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkBuffer frameBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory frameMemory_ = VK_NULL_HANDLE;
    FrameUniform* frameMap_ = nullptr;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    void* vertexMap_ = nullptr;
    VkDeviceSize vertexCapacity_ = 0;
    bool vertexStaged_ = false;
    VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    void* stagingMap_ = nullptr;
    VkDeviceSize stagingCapacity_ = 0;
    std::uint64_t uploadedRevision_ = 0;
    std::uint64_t uploadedSignature_ = 0;
    std::size_t uploadedVertices_ = 0;
    bool uploadedValid_ = false;

    Target target_;
    std::map<const Texture*, TextureSlot> textures_;
    TextureSlot white_;
    TextureSlot transfer_;
    std::vector<std::uint32_t> firstVertex_;
    bool ready_ = false;
    int frames_ = 0;
};

} // namespace detail
} // namespace gtc::native
