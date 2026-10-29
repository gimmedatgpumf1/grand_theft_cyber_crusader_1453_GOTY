// Offscreen Vulkan integration test. Unlike the fake-driver plumbing test,
// this executes the actual shaders and checks their rendered pixels. It can
// run on a hardware ICD or a software Vulkan implementation such as SwiftShader.
#include "client/ui_layer.h"
#include "client/crosshair.h"
#include "client/cheat_menu.h"
#include "client/mode_bar.h"
#include "client/fps_counter.h"
#include "client/status_panel.h"
#include "client/target_prompt.h"
#include "vulkan/vk_renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace gtc::native;

static bool checkSize(int width, int height, int fontSize) {
    Camera camera;
    camera.position = {1.25, 2.5, -3};
    camera.target = {0.5, 1.8, 1};
    camera.width = width;
    camera.height = height;
    GpuScene scene;
    scene.width = width;
    scene.height = height;
    scene.focal = camera.focal();
    scene.viewMatrix = camera.viewMatrix();
    scene.background = {0, 0, 0};

    const std::size_t rasterizationsBefore = glyphRasterizationCount();
    UiLayer layer;
    layer.clear(width, height);
    layer.rectangle(10, 10, 30, 20, 255, 0, 0, 1);
    layer.rectangle(50, 10, 30, 20, 0, 255, 0, 0.5);
    NativeFrame layout;
    layout.width = width;
    layout.height = height;
    layout.ui = &layer;
    drawTextPixels(layout, 90, 30, "Arena 1453", colorHex("#ffffff"), fontSize);
    if (layer.quads().size() < 5) { std::puts("FAIL: no font quads"); return false; }
    appendUiBatches(layer, camera, scene);
    if (glyphRasterizationCount() != rasterizationsBefore) {
        std::puts("FAIL: GPU UI layout rasterized glyphs on the CPU"); return false;
    }

    NativeFrame frame;
    frame.wantPixels = true;
    // The backend can collect the preceding frame. Repeat after every resize
    // so the readback is definitely from the current scene.
    for (int i = 0; i < 3; ++i) if (!renderVulkanScene(scene, frame)) return false;
    if (glyphRasterizationCount() != rasterizationsBefore) {
        std::puts("FAIL: Vulkan drawing rasterized glyphs on the CPU"); return false;
    }
    if (frame.rgb.size() != static_cast<std::size_t>(width) * height * 3) return false;
    const auto pixel = [&](int x, int y, int channel) {
        return frame.rgb[(static_cast<std::size_t>(y) * width + x) * 3 + channel];
    };
    if (pixel(20, 15, 0) < 245 || pixel(20, 15, 1) > 10 ||
        pixel(60, 15, 1) < 175 || pixel(60, 15, 1) > 200 || pixel(5, 5, 0) > 10 ||
        pixel(10, 9, 0) > 10 || pixel(40, 15, 0) > 10) {
        std::puts("FAIL: rectangle placement, colour or alpha"); return false;
    }
    NativeFrame reference;
    reference.width = width;
    reference.height = height;
    reference.rgb.assign(static_cast<std::size_t>(width) * height * 3, 0);
    compositeUi(reference, layer);
    int ink = 0, missing = 0, outside = 0, antialias = 0;
    for (int y = 30; y < std::min(height, 30 + fontSize + 4); ++y) {
        for (int x = 90; x < std::min(width, 90 + fontSize * 8); ++x) {
            const int ref = reference.rgb[(static_cast<std::size_t>(y) * width + x) * 3];
            const int actual = pixel(x, y, 0);
            if (ref > 230) { ++ink; if (actual < 200) ++missing; }
            if (ref == 0 && actual > 30) ++outside;
            if (actual > 20 && actual < 235) ++antialias;
        }
    }
    std::printf("Vulkan UI %dx%d: %zu quads, text ink %d missing %d outside %d AA %d\n",
                width, height, layer.quads().size(), ink, missing, outside, antialias);
    return ink > 50 && missing < 8 && outside < 8 && antialias > 10;
}

static bool checkGameUi(int width, int height) {
    Camera camera;
    camera.position = {3, 2, -1}; camera.target = {0, 1, 4};
    camera.width = width; camera.height = height;
    GpuScene scene;
    scene.width = width; scene.height = height;
    scene.focal = camera.focal(); scene.viewMatrix = camera.viewMatrix();
    scene.background = {0, 0, 0};
    const std::size_t rasterizationsBefore = glyphRasterizationCount();
    UiLayer layer;
    layer.clear(width, height);
    NativeFrame layout;
    layout.width = width; layout.height = height; layout.ui = &layer;
    ViewState view;
    view.width = width; view.height = height; view.mode = ViewMode::ThirdPerson;
    drawCrosshair(layout, view);
    drawTargetPrompt(layout, layoutTargetPrompt(width, height, height / 2), "Toilet");
    drawCheatMenu(layout, 62, 30, 4);
    drawModeBar(layout, view.mode, 2);
    const auto bar = layoutModeBar(width, height);
    drawNeedsPanel(layout, layoutNeedsPanel(width, height, bar.top, bar.width), NeedsStatus{72, 56, 100});
    drawFpsCounter(layout, 299.5);
    appendUiBatches(layer, camera, scene);
    if (glyphRasterizationCount() != rasterizationsBefore) {
        std::puts("FAIL: game UI rasterized glyphs on the CPU"); return false;
    }
    NativeFrame frame;
    frame.wantPixels = true;
    for (int i = 0; i < 3; ++i) if (!renderVulkanScene(scene, frame)) return false;
    NativeFrame reference;
    reference.width = width; reference.height = height;
    reference.rgb.assign(static_cast<std::size_t>(width) * height * 3, 0);
    compositeUi(reference, layer);
    int ink = 0, missing = 0, outside = 0;
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
        const auto maxChannel = [pixel](const NativeFrame& f) {
            return std::max({f.rgb[pixel * 3], f.rgb[pixel * 3 + 1], f.rgb[pixel * 3 + 2]});
        };
        const int expected = maxChannel(reference), actual = maxChannel(frame);
        if (expected > 100) { ++ink; if (actual < 60) ++missing; }
        if (expected == 0 && actual > 60) ++outside;
    }
    std::printf("Vulkan game UI %dx%d: %zu quads, visible reference pixels %d missing %d outside %d\n",
                width, height, layer.quads().size(), ink, missing, outside);
    return layer.quads().size() > 200 && ink > 3000 && missing < ink / 20 && outside < 150;
}

int main() {
    if (!vulkanAvailable()) { std::puts("FAIL: no Vulkan device/ICD"); return 1; }
    const bool first = checkSize(320, 240, 24);
    const bool second = checkSize(640, 480, 42); // resize and switch text size
    const bool third = checkSize(320, 240, 24);  // switch back to the first size
    const bool game = checkGameUi(640, 480);
    const bool game1080 = checkGameUi(1920, 1080);
    shutdownVulkan();
    if (!first || !second || !third || !game || !game1080) { std::puts("Vulkan UI: FAIL"); return 1; }
    std::puts("Vulkan UI: PASS (offscreen device-rendered pixels)");
    return 0;
}
