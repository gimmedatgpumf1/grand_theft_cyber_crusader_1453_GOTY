// The window's swapchain -- the one path that gets a finished picture onto the
// screen without ever putting it in host memory.
//
// Everything else in this backend renders into an image the process owns and
// then copies that image out to a mapped buffer: at 1568x929 that is 5.8 MB of
// PCIe traffic per frame, which the window then reads again to blit to the
// device context. Two full passes over the picture, every frame, on a machine
// whose GPU finished drawing it milliseconds earlier. Here the frame is blitted,
// on the device, straight into the image the compositor is about to show.
//
// Every step is checked and every failure is fatal and named. A window this
// backend cannot present to is a broken window, and a broken window that keeps
// running is one nobody looks at.

#include "../client/diagnostics.h"
#include "vk_internal.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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
#else
// ABI of VK_KHR_xlib_surface, without linking Xlib or requiring X11 headers.
struct VkXlibSurfaceCreateInfoKHR {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* dpy;
    unsigned long window;
};
using PFN_vkCreateXlibSurfaceKHR = VkResult(VKAPI_PTR*)(VkInstance, const VkXlibSurfaceCreateInfoKHR*,
                                                        const VkAllocationCallbacks*, VkSurfaceKHR*);
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gtc::native {
namespace detail {
namespace {

// There is no quiet way out of here. A window this backend cannot present to is
// a broken window, not a slower one: say what broke and die, so the thing that
// broke is the only thing on the screen.
[[noreturn]] void fatalSwapchain(const char* reason) {
    std::fflush(stdout);
    std::fprintf(stderr, "[vk] swapchain: %s\n", reason);
    std::fflush(stderr);
    std::abort();
}

// VK_KHR_swapchain has to be named when the device is created, before any of
// this file's frame-time work can happen.
bool swapchainExtensionOffered(PFN_vkGetInstanceProcAddr getProc, VkInstance instance, VkPhysicalDevice physical) {
    const auto enumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        getProc(instance, "vkEnumerateDeviceExtensionProperties"));
    if (enumerate == nullptr) return false;
    std::uint32_t count = 0;
    if (enumerate(physical, nullptr, &count, nullptr) != VK_SUCCESS || count == 0) return false;
    std::vector<VkExtensionProperties> offered(count);
    if (enumerate(physical, nullptr, &count, offered.data()) != VK_SUCCESS) return false;
    for (const VkExtensionProperties& property : offered) {
        if (std::strcmp(property.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) return true;
    }
    return false;
}

} // namespace

// The surface, asked for before the device exists. This is the same conversation
// the old probe had with the driver -- can this window present, in what format,
// at what size -- except the answers are kept instead of printed and dropped.
bool Renderer::prepareSurface() {
    if (!surfaceRequested()) return false;
    if (!resolve(destroySurface_, "vkDestroySurfaceKHR") ||
        !resolve(surfaceSupport_, "vkGetPhysicalDeviceSurfaceSupportKHR") ||
        !resolve(surfaceCaps_, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") ||
        !resolve(surfaceFormats_, "vkGetPhysicalDeviceSurfaceFormatsKHR") ||
        !resolve(surfaceModes_, "vkGetPhysicalDeviceSurfacePresentModesKHR")) {
        fatalSwapchain("the surface entry points are missing from this driver");
    }
    if (!swapchainExtensionOffered(getInstanceProcAddr_, instance_, physical_)) {
        fatalSwapchain("VK_KHR_swapchain is not offered by this device");
    }
    VkResult created = VK_ERROR_EXTENSION_NOT_PRESENT;
#if defined(_WIN32)
    const auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        getInstanceProcAddr_(instance_, "vkCreateWin32SurfaceKHR"));
    if (create == nullptr) {
        fatalSwapchain("vkCreateWin32SurfaceKHR is missing");
    }
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = GetModuleHandle(nullptr);
    info.hwnd = static_cast<HWND>(wsiHandle_);
    created = create(instance_, &info, nullptr, &surface_);
#else
    const auto create = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
        getInstanceProcAddr_(instance_, "vkCreateXlibSurfaceKHR"));
    if (!create || wsiKind_ != 2) fatalSwapchain("Xlib surface entry point or target missing");
    const auto* target = static_cast<const XlibSurfaceTarget*>(wsiHandle_);
    VkXlibSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    info.dpy = target->display;
    info.window = target->window;
    created = create(instance_, &info, nullptr, &surface_);
#endif
    if (created != VK_SUCCESS || surface_ == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[vk] surface: surface creation failed (%d)\n", static_cast<int>(created));
        fatalSwapchain("the window would not give up a surface");
    }
    VkBool32 supported = VK_FALSE;
    if (surfaceSupport_(physical_, family_, surface_, &supported) != VK_SUCCESS || !supported) {
        fatalSwapchain("this queue family cannot present to this window");
    }
    surfaceUsable_ = true;
    presentWanted_ = true;
    return true;
}

// One swapchain at the size the window currently is. Format and present mode are
// chosen here because both decide the frame time: an exact format match makes the
// handover a copy instead of a conversion, and a present mode that is not FIFO is
// the difference between the refresh rate and the frame rate.
bool Renderer::createSwapchain(int width, int height) {
    if (!surfaceUsable_ || !presentWanted_) return false;
    if (!resolve(createSwapchainFn, "vkCreateSwapchainKHR") ||
        !resolve(destroySwapchainFn, "vkDestroySwapchainKHR") ||
        !resolve(getSwapchainImages, "vkGetSwapchainImagesKHR") ||
        !resolve(acquireNextImage, "vkAcquireNextImageKHR") ||
        !resolve(queuePresent, "vkQueuePresentKHR") ||
        !resolve(createSemaphoreFn, "vkCreateSemaphore") ||
        !resolve(destroySemaphore, "vkDestroySemaphore") ||
        !resolve(blitImage, "vkCmdBlitImage") || !resolve(copyImage, "vkCmdCopyImage")) {
        fatalSwapchain("the swapchain entry points are missing from this driver");
    }
    VkSurfaceCapabilitiesKHR caps{};
    if (surfaceCaps_(physical_, surface_, &caps) != VK_SUCCESS) {
        fatalSwapchain("the surface has no capabilities");
    }
    VkExtent2D extent{};
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        extent = caps.currentExtent;
    } else {
        extent.width = std::min(std::max(static_cast<std::uint32_t>(width), caps.minImageExtent.width),
                                caps.maxImageExtent.width);
        extent.height = std::min(std::max(static_cast<std::uint32_t>(height), caps.minImageExtent.height),
                                 caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return false;
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        fatalSwapchain("this surface cannot take a transfer");
    }
    if (swapReady_ && extent.width == swapExtent_.width && extent.height == swapExtent_.height) return true;
    destroySwapchain();

    // The renderer's own format first: when the surface agrees, the handover is
    // vkCmdCopyImage, which is a straight move in device memory.
    VkFormat format = colourFormat_;
    VkColorSpaceKHR space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    std::uint32_t formatCount = 0;
    if (surfaceFormats_(physical_, surface_, &formatCount, nullptr) == VK_SUCCESS && formatCount > 0) {
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        surfaceFormats_(physical_, surface_, &formatCount, formats.data());
        // The renderer's format first, then anything that puts the same bytes
        // in the same order: a copy of identical bytes is a move, and the only
        // thing a blit does that a copy does not is convert -- which is the one
        // thing this handover must not do, because the pixels are already in the
        // window's colour space.
        const auto pick = [&](bool exact, bool sameBytes, bool bgra, bool anyColour) {
            for (const VkSurfaceFormatKHR& candidate : formats) {
                const bool match = exact ? candidate.format == colourFormat_
                    : sameBytes ? sameChannelBytes(candidate.format, colourFormat_)
                    : bgra ? isBgraFormat(candidate.format)
                    : anyColour ? isColourFormat(candidate.format) : true;
                if (!match) continue;
                format = candidate.format;
                space = candidate.colorSpace;
                return true;
            }
            return false;
        };
        pick(true, false, false, false) || pick(false, true, false, false) || pick(false, false, true, false) ||
            pick(false, false, false, true);
    }
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    std::uint32_t modeCount = 0;
    if (surfaceModes_(physical_, surface_, &modeCount, nullptr) == VK_SUCCESS && modeCount > 0) {
        std::vector<VkPresentModeKHR> modes(modeCount);
        surfaceModes_(physical_, surface_, &modeCount, modes.data());
        bool immediate = false, mailbox = false;
        for (VkPresentModeKHR candidate : modes) {
            if (candidate == VK_PRESENT_MODE_IMMEDIATE_KHR) immediate = true;
            if (candidate == VK_PRESENT_MODE_MAILBOX_KHR) mailbox = true;
        }
        mode = immediate ? VK_PRESENT_MODE_IMMEDIATE_KHR
             : mailbox ? VK_PRESENT_MODE_MAILBOX_KHR
             : VK_PRESENT_MODE_FIFO_KHR;
    }
    std::uint32_t images = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && images > caps.maxImageCount) images = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = images;
    info.imageFormat = format;
    info.imageColorSpace = space;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    // Nothing renders into these: they are the destination of one copy per
    // frame, and the compositor does the rest.
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0
        ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = VK_NULL_HANDLE;
    if (createSwapchainFn(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) {
        swapchain_ = VK_NULL_HANDLE;
        fatalSwapchain("vkCreateSwapchainKHR failed");
    }
    std::uint32_t count = 0;
    if (getSwapchainImages(device_, swapchain_, &count, nullptr) != VK_SUCCESS || count == 0) {
        fatalSwapchain("vkGetSwapchainImagesKHR returned nothing");
    }
    swapImages_.resize(count, VK_NULL_HANDLE);
    if (getSwapchainImages(device_, swapchain_, &count, swapImages_.data()) != VK_SUCCESS) {
        fatalSwapchain("vkGetSwapchainImagesKHR failed");
    }
    swapImages_.resize(count);
    swapViews_.assign(count, VK_NULL_HANDLE);
    imageReady_.assign(kFramesInFlight, VK_NULL_HANDLE);
    drawDone_.assign(count, VK_NULL_HANDLE);
    swapImageSlot_.assign(count, -1);
    swapImagePresented_.assign(count, 0);
    for (VkSemaphore& semaphore : imageReady_) {
        VkSemaphoreCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (createSemaphoreFn(device_, &info, nullptr, &semaphore) != VK_SUCCESS) {
            fatalSwapchain("vkCreateSemaphore failed");
        }
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = swapImages_[index];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        if (createImageView(device_, &view, nullptr, &swapViews_[index]) != VK_SUCCESS) {
            fatalSwapchain("vkCreateImageView failed for a swapchain image");
        }
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (createSemaphoreFn(device_, &semaphore, nullptr, &drawDone_[index]) != VK_SUCCESS) {
            fatalSwapchain("vkCreateSemaphore failed for a swapchain image");
        }
    }
    swapFormat_ = format;
    swapExtent_ = extent;
    swapWidth_ = static_cast<int>(extent.width);
    swapHeight_ = static_cast<int>(extent.height);
    swapReady_ = true;
    tracef("vk: swapchain %ux%u, %u images, %s", extent.width, extent.height, count,
           mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate"
           : mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "fifo");
    std::printf("[vk] swapchain: %ux%u, %u images, %s, %s handover\n", extent.width, extent.height, count,
                mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate"
                : mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "fifo",
                swapFormat_ == colourFormat_ || sameChannelBytes(swapFormat_, colourFormat_) ? "copy" : "blit");
    return true;
}

void Renderer::destroySwapchain() {
    if (device_ != VK_NULL_HANDLE && swapchain_ != VK_NULL_HANDLE) deviceWaitIdle(device_);
    for (VkSemaphore& semaphore : drawDone_) {
        if (semaphore && destroySemaphore) destroySemaphore(device_, semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
    }
    for (VkSemaphore& semaphore : imageReady_) {
        if (semaphore && destroySemaphore) destroySemaphore(device_, semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
    }
    for (VkImageView& view : swapViews_) {
        if (view && destroyImageView) destroyImageView(device_, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (swapchain_ && destroySwapchainFn) destroySwapchainFn(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    swapImages_.clear();
    swapViews_.clear();
    imageReady_.clear();
    drawDone_.clear();
    swapImageSlot_.clear();
    swapImagePresented_.clear();
    swapReady_ = false;
    swapWidth_ = swapHeight_ = 0;
}

// Hands back the next image to draw into. An image that has been presented
// before is only reused once the submission that last wrote it has completed,
// which is what the per-slot fences are for.
bool Renderer::acquirePresentImage(int slot, int& imageIndex) {
    imageIndex = -1;
    if (!swapReady_ || acquireNextImage == nullptr) return false;
    std::uint32_t index = 0;
    // No timeout: an image is either coming or the window is gone.
    const VkResult result = acquireNextImage(device_, swapchain_, UINT64_MAX, imageReady_[slot], VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // A resize. Not a failure: the next frame makes a swapchain that fits.
        swapReady_ = false;
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "[vk] swapchain: vkAcquireNextImageKHR failed (%d)\n", static_cast<int>(result));
        fatalSwapchain("the swapchain would not give up an image");
    }
    if (index >= swapImageSlot_.size()) return false;
    const int owner = swapImageSlot_[index];
    if (owner >= 0 && owner < kFramesInFlight && fenceArmed_[owner]) {
        waitForFences(device_, 1, &fences_[owner], VK_TRUE, 5000000000ull);
        fenceArmed_[owner] = false;
        if (pendingSlot_ == owner) pendingSlot_ = -1;
    }
    swapImageSlot_[index] = slot;
    imageIndex = static_cast<int>(index);
    return true;
}

// The handover: the picture is already in TRANSFER_SRC on the colour image, so
// this is one barrier, one copy or blit, and one barrier to present.
void Renderer::recordPresentBlit(int imageIndex) {
    if (imageIndex < 0 || static_cast<std::size_t>(imageIndex) >= swapImages_.size()) return;
    // An image that comes back from acquire is in PRESENT_SRC; the first use of
    // a fresh image is in UNDEFINED. Both are stated rather than assumed.
    const bool presented = swapImagePresented_[static_cast<std::size_t>(imageIndex)] != 0;
    VkImageMemoryBarrier acquire{};
    acquire.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    acquire.oldLayout = presented ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    acquire.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    acquire.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    acquire.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    acquire.image = swapImages_[imageIndex];
    acquire.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    acquire.subresourceRange.levelCount = 1;
    acquire.subresourceRange.layerCount = 1;
    acquire.srcAccessMask = 0;
    acquire.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pipelineBarrier(command_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                    0, nullptr, 1, &acquire);

    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent = {swapExtent_.width, swapExtent_.height, 1};
    // Identical bytes, or the same channels with the transfer curve applied on
    // one side only: both are a move between compatible formats, and a move does
    // not touch a value. Anything else is a real conversion and is blitted.
    if (swapFormat_ == colourFormat_ || sameChannelBytes(swapFormat_, colourFormat_)) {
        copyImage(command_, target_.colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapImages_[imageIndex],
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    } else {
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        blit.srcOffsets[1] = {static_cast<std::int32_t>(swapExtent_.width),
                              static_cast<std::int32_t>(swapExtent_.height), 1};
        blit.dstOffsets[1] = blit.srcOffsets[1];
        blitImage(command_, target_.colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapImages_[imageIndex],
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    }

    VkImageMemoryBarrier release{};
    release.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    release.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    release.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    release.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    release.image = swapImages_[imageIndex];
    release.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    release.subresourceRange.levelCount = 1;
    release.subresourceRange.layerCount = 1;
    release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    release.dstAccessMask = 0;
    pipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                    0, nullptr, 1, &release);
}

bool Renderer::presentImage(int imageIndex) {
    if (!swapReady_ || imageIndex < 0 || static_cast<std::size_t>(imageIndex) >= drawDone_.size()) return false;
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &drawDone_[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    const std::uint32_t index = static_cast<std::uint32_t>(imageIndex);
    present.pImageIndices = &index;
    const VkResult result = queuePresent(queue_, &present);
    swapImagePresented_[static_cast<std::size_t>(imageIndex)] = 1;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // A resize: the next frame makes a swapchain that matches the window.
        swapReady_ = false;
        swapWidth_ = swapHeight_ = 0;
        return false;
    }
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[vk] swapchain: vkQueuePresentKHR failed (%d)\n", static_cast<int>(result));
        fatalSwapchain("presenting failed");
    }
    return true;
}

} // namespace detail
} // namespace gtc::native
