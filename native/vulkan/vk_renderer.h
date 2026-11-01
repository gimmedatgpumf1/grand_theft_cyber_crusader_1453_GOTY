#pragma once

#include "../client/raster.h"

namespace gtc::native {

// The Vulkan path is a peer of the OpenGL one, behind the same seam: the
// rasterizer fills a GpuScene, somebody draws it, and a NativeFrame comes back.
// Nothing above this header knows which backend drew the frame.
bool vulkanAvailable();
bool renderVulkanScene(const GpuScene& scene, NativeFrame& frame);
void shutdownVulkan();

// Whether a drawn frame is copied back to host memory. On by default, because
// the client needs pixels it can blit and draw a HUD over. Turning it off makes
// the number comparable with a backend that only submits and never waits.
void setFrameReadback(bool enabled);

// Keeps the colour image in linear layout so copying it back is a memory copy
// rather than a walk over tiled memory. Only worth it when the frame is read
// back; measured by the skull benchmark.
void setLinearTarget(bool enabled);

// Where the readback buffer lives: 0 host cached, 1 device local + host
// visible, 2 host visible only. Measured by the skull benchmark; 0 is the
// default.
void setReadbackMemory(int mode);

// Tells the backend which window it may eventually present to, before the device
// is created: surface extensions have to be requested at instance creation. With
// no window attached, everything stays on the readback path.
struct XlibSurfaceTarget { void* display; unsigned long window; };
// kind 1: Win32 HWND, kind 2: XlibSurfaceTarget (kept alive by the window).
void setVulkanSurfaceTarget(void* windowHandle, int kind);

// Waits for any submitted frame to finish, so the next measurement starts from
// an idle device instead of inheriting the previous one's work. Only the
// benchmark needs this: the client always runs frames back to back.
void drainVulkan();

} // namespace gtc::native
