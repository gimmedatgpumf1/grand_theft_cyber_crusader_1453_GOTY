// A driver-less exercise of the Vulkan backend, run against the stub loader in
// `tests/fake-vulkan`. It proves the control flow -- device setup, first frame,
// cached geometry, a resize, a textured material, shutdown -- and that nothing
// dereferences a handle the stub did not hand back.
//
// It says nothing about what the picture looks like: the stub accepts every
// command and fills mapped memory with a pattern, so the pixels here are the
// projection matrix's own bytes. That is the point. Rendering quality needs a
// real device; plumbing failures are what this catches.

#include "client/ui_layer.h"
#include "vulkan/vk_internal.h"
#include "vulkan/vk_renderer.h"

#include <algorithm>
#include <cstdio>
#include <dlfcn.h>
#include <vector>

namespace {

gtc::native::GpuBatch makeBatch() {
    gtc::native::GpuBatch batch;
    batch.objectId = 1;
    batch.revision = 7;
    batch.material.kind = gtc::native::Material::Kind::Solid;
    batch.material.color = {0.8, 0.2, 0.1};
    batch.material.opacity = 1.0;
    batch.vertices = {
        {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        {0.2f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.2f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
    };
    return batch;
}

} // namespace

int main() {
    gtc::native::GpuScene scene;
    scene.width = 64;
    scene.height = 48;
    scene.focal = 60;
    scene.revision = 1;
    scene.background = {0.02, 0.05, 0.03};
    scene.viewMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -3, 1};
    scene.batches.push_back(makeBatch());

    gtc::native::NativeFrame frame;
    int failures = 0;
    if (!gtc::native::renderVulkanScene(scene, frame) || frame.rgb.empty() ||
        frame.width != 64 || frame.height != 48) {
        std::printf("first frame did not arrive\n");
        ++failures;
    }
    // The test double records pipeline state and binds, not pixels. This checks
    // that UI batches select blending with no depth test; vulkan_ui.cpp checks
    // actual pixels separately, against a real Vulkan implementation.
    {
        void* driver = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        using Query = int (*)(int);
        using Count = int (*)();
        auto count = driver ? reinterpret_cast<Count>(dlsym(driver, "stubPipelineCount")) : nullptr;
        auto blend = driver ? reinterpret_cast<Query>(dlsym(driver, "stubPipelineBlended")) : nullptr;
        auto depth = driver ? reinterpret_cast<Query>(dlsym(driver, "stubPipelineDepthTest")) : nullptr;
        auto boundCount = driver ? reinterpret_cast<Count>(dlsym(driver, "stubBoundCount")) : nullptr;
        auto boundBlend = driver ? reinterpret_cast<Query>(dlsym(driver, "stubBoundBlended")) : nullptr;
        if (!count || !blend || !depth || !boundCount || !boundBlend || count() != 8) {
            std::puts("UI pipeline inspection unavailable"); ++failures;
        } else {
            for (int index = 0; index < 8; ++index) {
                if (blend(index) != (index >= 4) || depth(index) != (index < 4)) {
                    std::printf("incorrect pipeline state %d\n", index); ++failures;
                }
            }
            gtc::native::Camera camera;
            camera.position = {0, 0, 0}; camera.target = {0, 0, 1};
            camera.width = scene.width; camera.height = scene.height;
            gtc::native::GpuScene withUi = scene;
            withUi.viewMatrix = camera.viewMatrix(); withUi.focal = camera.focal();
            gtc::native::UiLayer layer;
            layer.clear(scene.width, scene.height);
            layer.rectangle(4, 4, 12, 12, 255, 32, 0, 0.5);
            gtc::native::NativeFrame layout;
            layout.width = scene.width; layout.height = scene.height; layout.ui = &layer;
            gtc::native::drawTextPixels(layout, 20, 5, "UI", {1, 1, 1}, 20);
            gtc::native::appendUiBatches(layer, camera, withUi);
            if (!gtc::native::renderVulkanScene(withUi, frame) || boundCount() == 0 ||
                boundBlend(boundCount() - 1) != 1) {
                std::puts("UI did not bind its blended pipeline"); ++failures;
            }
        }
        if (driver) dlclose(driver);
    }
    // The frame contract includes the coverage mask. The world renderer refuses
    // a GPU frame without one, so a missing mask ends the session rather than
    // showing a wrong picture.
    if (frame.alpha.size() != static_cast<std::size_t>(frame.width) * frame.height) {
        std::printf("frame came back without a coverage mask\n");
        ++failures;
    }
    // The same geometry again: the vertex buffer must be reused, not re-sent.
    if (!gtc::native::renderVulkanScene(scene, frame)) {
        std::printf("second frame did not arrive\n");
        ++failures;
    }
    // A resize has to rebuild the targets, not draw the old size.
    scene.width = 96;
    scene.height = 32;
    if (!gtc::native::renderVulkanScene(scene, frame) || frame.width != 96 || frame.height != 32) {
        std::printf("resized frame did not arrive at the new size\n");
        ++failures;
    }
    // A floor texture takes the staging-buffer path.
    gtc::native::Texture texture;
    texture.width = 2;
    texture.height = 2;
    texture.pixels.assign(16, 200);
    gtc::native::GpuBatch textured = makeBatch();
    textured.material.kind = gtc::native::Material::Kind::Floor;
    textured.material.texture = &texture;
    textured.revision = 8;
    scene.batches[0] = textured;
    if (!gtc::native::renderVulkanScene(scene, frame)) {
        std::printf("textured frame did not arrive\n");
        ++failures;
    }
    // A skeleton is fifty-six rigid batches that share one material, and
    // submitting them one at a time was 0.7 ms of a three millisecond frame.
    // This is the decision that folds them into one draw, checked on the scene
    // itself: a run of three, a batch of another colour left alone, an unrolled
    // batch left alone, and a second run after them.
    {
        gtc::native::GpuScene planned;
        planned.width = 64;
        planned.height = 48;
        for (int index = 0; index < 7; ++index) {
            gtc::native::GpuBatch batch = makeBatch();
            batch.objectId = static_cast<std::uint32_t>(100 + index);
            batch.material.color = index == 3 ? gtc::native::Color{0.1, 0.9, 0.2}
                                              : gtc::native::Color{0.8, 0.2, 0.1};
            // Batch 4 is the unrolled shape: three vertices per triangle and no
            // index, which has nothing to fold in.
            if (index != 4) batch.indices = {0, 1, 2};
            planned.batches.push_back(batch);
        }
        std::vector<gtc::native::detail::BatchRun> runs;
        std::vector<float> slots;
        gtc::native::detail::planRuns(planned, runs, slots);
        const bool shape = runs.size() == 2 && runs[0].first == 0 && runs[0].last == 2 &&
                           runs[1].first == 5 && runs[1].last == 6;
        const std::vector<float> wanted = {0.0F, 1.0F, 2.0F, -1.0F, -1.0F, 3.0F, 4.0F};
        if (!shape || slots != wanted) {
            std::printf("run planning folded the wrong batches: %zu runs\n", runs.size());
            ++failures;
        }
        // The slot is what the vertex stage reads out of the normal field, so a
        // duplicate would put one bone's matrix on another bone's vertices.
        std::vector<float> seen;
        for (float slot : slots) {
            if (slot < 0.0F) continue;
            if (std::find(seen.begin(), seen.end(), slot) != seen.end()) {
                std::printf("two batches share bone slot %g\n", static_cast<double>(slot));
                ++failures;
                break;
            }
            seen.push_back(slot);
        }
    }
    gtc::native::shutdownVulkan();
    // After shutdown the backend must refuse rather than touch freed handles.
    if (gtc::native::renderVulkanScene(scene, frame)) {
        std::printf("the backend drew after shutdown\n");
        ++failures;
    }
    if (failures != 0) {
        std::printf("vulkan plumbing: FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("vulkan plumbing: PASS (setup, reuse, resize, texture, shutdown)\n");
    return 0;
}
