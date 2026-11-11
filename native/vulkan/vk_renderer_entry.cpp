// The Vulkan backend's front door: the one renderer this process owns, the first
// frame it must survive before anyone trusts it, and the switches a test or a
// screenshot run turns on and off. The frame loop itself is in vk_renderer.cpp.

#include "../client/diagnostics.h"
#include "vk_internal.h"

#include <cstdio>
#include <cstdlib>

namespace gtc::native {

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

}  // namespace gtc::native
