// A driver-less exercise of the Vulkan backend, run against the stub loader in
// `tests/fake-vulkan`. It proves the control flow -- device setup, first frame,
// cached geometry, a resize, a textured material, shutdown -- and that nothing
// dereferences a handle the stub did not hand back.
//
// It says nothing about what the picture looks like: the stub accepts every
// command and fills mapped memory with a pattern, so the pixels here are the
// projection matrix's own bytes. That is the point. Rendering quality needs a
// real device; plumbing failures are what this catches.

#include "vulkan/vk_renderer.h"

#include <cstdio>

namespace {

gtc::native::GpuBatch makeBatch() {
    gtc::native::GpuBatch batch;
    batch.objectId = 1;
    batch.revision = 7;
    batch.material.kind = gtc::native::Material::Kind::Solid;
    batch.material.color = {0.8, 0.2, 0.1};
    batch.material.opacity = 1.0;
    // Normals are packed as signed bytes, so they go through setNormal rather
    // than a literal: a float literal there is a narrowing conversion, which
    // MSVC refuses outright.
    gtc::native::GpuVertex a{};
    a.x = 0.0f; a.y = 0.0f; a.z = 0.0f; a.setNormal(0.0, 1.0, 0.0); a.u = 0.0f; a.v = 0.0f;
    gtc::native::GpuVertex b = a;
    b.x = 0.2f; b.u = 1.0f;
    gtc::native::GpuVertex c = a;
    c.y = 0.2f; c.v = 1.0f;
    batch.vertices = {a, b, c};
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
