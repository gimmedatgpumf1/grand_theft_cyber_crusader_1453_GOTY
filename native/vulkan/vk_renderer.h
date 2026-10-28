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

} // namespace gtc::native
