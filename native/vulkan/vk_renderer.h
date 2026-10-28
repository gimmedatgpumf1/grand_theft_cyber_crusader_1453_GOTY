#pragma once

#include "../client/raster.h"

namespace gtc::native {

// The Vulkan path is a peer of the OpenGL one, behind the same seam: the
// rasterizer fills a GpuScene, somebody draws it, and a NativeFrame comes back.
// Nothing above this header knows which backend drew the frame.
bool vulkanAvailable();
bool renderVulkanScene(const GpuScene& scene, NativeFrame& frame);
void shutdownVulkan();

} // namespace gtc::native
