// Offscreen Vulkan integration test for the test room's real resize path.
// This executes the renderer on an ICD; it does not exercise a window swapchain.
#include "client/skeleton_room.h"
#include "client/room_freecam.h"
#include "vulkan/vk_renderer.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

using namespace gtc::native;

static bool bothVisible = true;

static bool renderAndCheck(const GpuScene& scene, NativeFrame& frame) {
    if (!renderVulkanScene(scene, frame)) return false;
    if (scene.width == 1 || scene.height == 1) return true;
    const std::size_t bytes = static_cast<std::size_t>(scene.width) * scene.height * 4;
    const unsigned char* image = frame.direct ? frame.direct : frame.bgra.data();
    std::vector<unsigned char> withBones(image, image + bytes);
    // Remove one body at a time and compare device pixels. This establishes
    // that each skeleton contributes visible pixels, not merely that the room
    // background or the other skeleton is visible.
    for (std::uint32_t base : {8001u, 9001u}) {
        GpuScene background = scene;
        background.revision += base;
        background.batches.erase(std::remove_if(background.batches.begin(), background.batches.end(),
            [base](const GpuBatch& batch) { return batch.objectId >= base && batch.objectId < base + 999; }),
            background.batches.end());
        NativeFrame empty;
        empty.width = scene.width; empty.height = scene.height;
        empty.wantPixels = true; empty.wantsBgra = true;
        if (!renderVulkanScene(background, empty)) return false;
        const unsigned char* plain = empty.direct ? empty.direct : empty.bgra.data();
        int changed = 0;
        for (std::size_t at = 0; at < bytes; at += 4) {
            int difference = 0;
            for (int channel = 0; channel < 3; ++channel)
                difference += std::abs(static_cast<int>(withBones[at + channel]) - plain[at + channel]);
            if (difference > 24) ++changed;
        }
        if (changed < 10) {
            std::printf("Vulkan room resize: skeleton %u missing at %dx%d (%d pixels)\n",
                        base, scene.width, scene.height, changed);
            bothVisible = false;
        }
    }
    return true;
}

int main() {
    if (!vulkanAvailable()) { std::puts("Vulkan room resize: FAIL (no ICD)"); return 1; }
    SkeletonRoom room;
    std::string error;
    if (!room.load(".", error)) { std::printf("room load: %s\n", error.c_str()); return 1; }
    setGpuRenderer(&renderVulkanScene);
    ViewState view;
    view.gpu = true;
    NativeFrame frame;
    frame.wantPixels = true; // Explicit test readback, never the interactive path.
    int step = 0;
    bool ok = true;
    const std::pair<int, int> sizes[] = {{320, 240}, {800, 600}, {1, 1},
                                         {640, 480}, {400, 600}, {960, 540}, {320, 240}};
    // First use the unmodified backend in consecutive frames, then verify each
    // body contributes actual device pixels at every visible size.
    for (int pass = 0; pass < 2 && ok; ++pass) {
      if (pass == 1) setGpuRenderer(&renderAndCheck);
      for (const auto& size : sizes) {
        view.width = size.first; view.height = size.second;
        // Some sizes change within the same animation tick, others across ticks.
        room.render(frame, view, step % 7 < 4 ? 1.75 : 1.90);
        const std::size_t bytes = static_cast<std::size_t>(view.width) * view.height * 4;
        if (frame.width != view.width || frame.height != view.height ||
            (!frame.direct && frame.bgra.size() < bytes)) { ok = false; break; }
        if (!bothVisible) { ok = false; break; }
        ++step;
      }
    }
    // Exercise movement through the same room scene and actual Vulkan shader
    // path, without rebuilding geometry or touching the gameplay view.
    if (ok) {
        setGpuRenderer(&renderVulkanScene);
        RoomFreecam camera;
        view.width = 640; view.height = 480;
        room.render(frame, view, 2.5, &camera);
        const std::size_t bytes = static_cast<std::size_t>(view.width) * view.height * 4;
        const unsigned char* before = frame.direct ? frame.direct : frame.bgra.data();
        std::vector<unsigned char> first(before, before + bytes);
        camera.handle(WindowEvent{WindowEvent::Type::KeyDown, 'w'}, true);
        camera.move(0.8);
        WindowEvent look{WindowEvent::Type::MouseMove}; look.dx = 45; look.dy = -25;
        camera.handle(look, true);
        room.render(frame, view, 2.5, &camera);
        const unsigned char* after = frame.direct ? frame.direct : frame.bgra.data();
        int changed = 0;
        for (std::size_t i = 0; i < bytes; i += 4) {
            int difference = 0;
            for (int c = 0; c < 3; ++c)
                difference += std::abs(static_cast<int>(first[i + c]) - after[i + c]);
            if (difference > 24) ++changed;
        }
        ok = changed > 1000;
        std::printf("Vulkan room freecam: %s (%d changed pixels)\n", ok ? "PASS" : "FAIL", changed);
    }
    setGpuRenderer(nullptr);
    shutdownVulkan();
    std::printf("Vulkan room resize: %s (%d offscreen frames)\n", ok ? "PASS" : "FAIL", step);
    return ok ? 0 : 1;
}
