#pragma once
// Compare incremental uploads to forced full uploads on an actual Vulkan ICD.
#include "vulkan/vk_internal.h"

static bool checkIncrementalGeometry() {
    gtc::native::detail::Renderer renderer;
    if (!renderer.start()) return false;
    Camera camera;
    camera.position = {0, 0, 0}; camera.target = {0, 0, 1};
    camera.width = 320; camera.height = 240;
    GpuScene scene;
    scene.width = 320; scene.height = 240; scene.focal = camera.focal();
    scene.viewMatrix = camera.viewMatrix(); scene.background = {0, 0, 0};
    scene.revision = 1;
    UiLayer fixed;
    fixed.clear(320, 240);
    fixed.rectangle(5, 10, 55, 40, 255, 0, 0, 1);
    fixed.rectangle(65, 10, 55, 40, 0, 255, 0, 1);
    appendUiBatches(fixed, camera, scene);
    for (auto& batch : scene.batches) batch.revision = 0;
    // Exercise the retained global index stream as well as the vertex stream.
    scene.batches[0].isUi = false;
    scene.batches[1].isUi = false;
    scene.batches[0].indices = {0, 1, 2, 3, 4, 5};
    const std::size_t stable = scene.batches.size();
    NativeFrame incremental, full;
    incremental.wantPixels = full.wantPixels = true;
    bool ok = true;
    for (int step = 0; step < 8 && ok; ++step) {
        scene.batches.resize(stable);
        if (step == 4) {
            // Same-size static edit: scene revision must invalidate it.
            for (auto& vertex : scene.batches[0].vertices) vertex.x += 0.1f;
            ++scene.revision;
        }
        if (step != 5) {
            UiLayer dynamic;
            dynamic.clear(320, 240);
            dynamic.rectangle(20 + step * 5, 80, 35 + step * 3, 30, 20, 80, 255, 0.7);
            NativeFrame layout;
            layout.width = 320; layout.height = 240; layout.ui = &dynamic;
            drawTextPixels(layout, 75, 135, step % 2 ? "moving UI" : "water", {1, 1, 1}, 24);
            appendUiBatches(dynamic, camera, scene);
            scene.batches[stable].isUi = false;
            scene.batches[stable].indices = {0, 1, 2, 3, 4, 5};
        }
        if (!renderer.draw(scene, incremental)) { ok = false; break; }
        if (step == 0) {
            const auto pixel = [&](int x, int y, int channel) {
                return incremental.rgb[(static_cast<std::size_t>(y) * 320 + x) * 3 + channel];
            };
            if (pixel(10, 15, 0) < 200 || pixel(70, 15, 1) < 200 || pixel(25, 85, 2) < 100) {
                std::puts("FAIL: incremental test geometry was not visible"); ok = false; break;
            }
        }
        const std::size_t expected = (step == 0 || step == 4) ? 0 : stable;
        if (renderer.stableGeometryBatches() != expected) {
            std::printf("FAIL: retained prefix at step %d was %zu, expected %zu\n",
                        step, renderer.stableGeometryBatches(), expected);
            ok = false; break;
        }
        ++scene.revision; // force the same geometry through a full upload
        if (!renderer.draw(scene, full) || renderer.stableGeometryBatches() != 0 || incremental.rgb != full.rgb) {
            std::printf("FAIL: incremental/full pixels differ at step %d\n", step);
            ok = false;
        }
    }
    // Re-planning an unchanged coalesced skeleton must retain its index count.
    if (ok) {
        GpuScene bones = scene;
        bones.batches.resize(stable);
        for (auto& batch : bones.batches) {
            batch.isUi = false;
            batch.material.color = {1, 1, 1};
            batch.material.unlit = false;
            batch.material.constantLighting = false;
            batch.indices = {0, 1, 2, 3, 4, 5};
            batch.revision = 0;
        }
        ++bones.revision;
        if (!renderer.draw(bones, incremental) || !renderer.draw(bones, full) || incremental.rgb != full.rgb) {
            std::puts("FAIL: unchanged coalesced geometry disappeared"); ok = false;
        } else {
            const auto lit = std::count_if(incremental.rgb.begin(), incremental.rgb.end(),
                                           [](unsigned char channel) { return channel > 20; });
            if (lit < 100) { std::puts("FAIL: coalesced geometry was not visible"); ok = false; }
        }
    }
    renderer.stop();
    std::printf("Vulkan geometry: %s (8 incremental/full frame comparisons)\n", ok ? "PASS" : "FAIL");
    return ok;
}
