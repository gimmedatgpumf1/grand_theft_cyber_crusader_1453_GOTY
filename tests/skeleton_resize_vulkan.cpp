// Offscreen Vulkan integration test for the test room's real resize path.
// This executes the renderer on an ICD; it does not exercise a window swapchain.
#include "client/skeleton_room.h"
#include "client/room_freecam.h"
#include "vulkan/vk_renderer.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

using namespace gtc::native;

static bool bothVisible = true;
static bool dotsVisible = false;
static bool solidsVisible = false;
static bool heelsVisible = false;

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

// Compare actual Vulkan pixels with each body's 24 dot spheres removed.
static bool renderAndCheckDots(const GpuScene& scene, NativeFrame& frame) {
    if (!renderVulkanScene(scene, frame)) return false;
    const std::size_t bytes = static_cast<std::size_t>(scene.width) * scene.height * 4;
    const unsigned char* pixels = frame.direct ? frame.direct : frame.bgra.data();
    const std::vector<unsigned char> withDots(pixels, pixels + bytes);
    for (std::uint32_t id : {8001u, 9001u}) {
        GpuScene noDots = scene;
        noDots.revision += id + 20000;
        bool removed = false;
        for (auto& batch : noDots.batches) {
            if (batch.objectId == id && batch.indices.size() == 378 * 36 + 24 * 60) {
                batch.indices.resize(batch.indices.size() - 24 * 60);
                removed = true;
            }
        }
        if (!removed) return false;
        NativeFrame plain;
        plain.width = scene.width; plain.height = scene.height;
        plain.wantPixels = true; plain.wantsBgra = true;
        if (!renderVulkanScene(noDots, plain)) return false;
        const unsigned char* other = plain.direct ? plain.direct : plain.bgra.data();
        int changed = 0;
        for (std::size_t at = 0; at < bytes; at += 4) {
            int difference = 0;
            for (int c = 0; c < 3; ++c) difference += std::abs(int(withDots[at+c]) - int(other[at+c]));
            if (difference > 24) ++changed;
        }
        std::printf("Vulkan vertebra dots %u: %d visible pixels\n", id, changed);
        if (changed < 5) return false;
    }
    dotsVisible = true;
    return true;
}

// Compare real Vulkan pixels with each closed prism removed. Also remove just
// its filled caps: this checks filled triangles, not merely an outline.
static bool renderAndCheckSolids(const GpuScene& scene, NativeFrame& frame) {
    if (!renderVulkanScene(scene, frame)) return false;
    const std::size_t bytes = static_cast<std::size_t>(scene.width) * scene.height * 4;
    const unsigned char* pixels = frame.direct ? frame.direct : frame.bgra.data();
    const std::vector<unsigned char> original(pixels, pixels + bytes);
    int sequence = 0;
    for (std::uint32_t id : {8001u, 9001u}) {
        const char* path = id == 8001 ? "tools/skeleton-source/skeleton-female.lines" : "tools/skeleton-source/skeleton-male.lines";
        std::ifstream input(path);
        struct Shape { std::string name; std::size_t offset, caps, indices; bool foot; };
        std::vector<Shape> shapes;
        std::size_t totalIndices = 0, totalVertices = 0;
        std::string row;
        while (std::getline(input, row)) if (row.rfind("M ", 0) == 0 || row.rfind("F ", 0) == 0) {
            std::istringstream fields(row);
            char kind; std::string name; fields >> kind >> name;
            if (kind == 'F') {
                shapes.push_back({name, totalIndices, 0, 48, true});
                totalIndices += 48; totalVertices += 10;
            } else {
                double thickness; int vertices, faces, edges;
                fields >> thickness >> vertices >> faces >> edges;
                if (!fields) return false;
                Shape shape{name, totalIndices, static_cast<std::size_t>(faces)*6,
                            static_cast<std::size_t>(faces+edges)*6, false};
                totalIndices += shape.indices; totalVertices += 2*vertices;
                shapes.push_back(shape);
            }
        }
        if (shapes.size() != 8) return false;
        for (const Shape& shape : shapes) {
            for (bool onlyCaps : {false, true}) {
                if (onlyCaps && shape.foot) continue;
                GpuScene without = scene;
                without.revision += 40000 + sequence++;
                bool removed = false;
                for (auto& batch : without.batches) {
                    if (batch.objectId != id || batch.indices.size() != totalIndices || batch.vertices.size() != totalVertices) continue;
                    const auto begin = batch.indices.begin() + shape.offset;
                    batch.indices.erase(begin, begin + (onlyCaps ? shape.caps : shape.indices));
                    removed = true;
                }
                if (!removed) return false;
                NativeFrame plain;
                plain.width = scene.width; plain.height = scene.height;
                plain.wantPixels = true; plain.wantsBgra = true;
                if (!renderVulkanScene(without, plain)) return false;
                const unsigned char* other = plain.direct ? plain.direct : plain.bgra.data();
                int changed = 0;
                for (std::size_t at = 0; at < bytes; at += 4) {
                    int difference = 0;
                    for (int c = 0; c < 3; ++c) difference += std::abs(int(original[at+c]) - int(other[at+c]));
                    if (difference > 24) ++changed;
                }
                std::printf("Vulkan %s %u %s: %d visible pixels\n", onlyCaps ? "filled faces" : "solid",
                            id, shape.name.c_str(), changed);
                if (changed < 1) return false;
            }
        }
    }
    solidsVisible = true;
    return true;
}

// Compare the real device image with the triangles incident to the heel-end
// corners of each foot removed; the geometry test separately checks dimensions.
static bool renderAndCheckHeels(const GpuScene& scene, NativeFrame& frame) {
    if (!renderVulkanScene(scene, frame)) return false;
    const std::size_t bytes = static_cast<std::size_t>(scene.width) * scene.height * 4;
    const unsigned char* pixels = frame.direct ? frame.direct : frame.bgra.data();
    const std::vector<unsigned char> original(pixels, pixels + bytes);
    int revision = 0;
    for (std::uint32_t id : {8001u, 9001u}) {
        const char* path = id == 8001 ? "tools/skeleton-source/skeleton-female.lines" : "tools/skeleton-source/skeleton-male.lines";
        for (char side : {'L', 'R'}) {
            std::ifstream input(path);
            const std::string group = std::string("FootBase_") + side;
            std::size_t vertexOffset = 0, indexOffset = 0, totalVertices = 0, totalIndices = 0;
            bool found = false;
            std::string row;
            while (std::getline(input, row)) {
                if (row.rfind("F ", 0) != 0 && row.rfind("M ", 0) != 0) continue;
                std::istringstream fields(row);
                char kind; std::string name; fields >> kind >> name;
                if (kind == 'F') {
                    if (name == group) { vertexOffset = totalVertices; indexOffset = totalIndices; found = true; }
                    totalVertices += 10; totalIndices += 48;
                } else {
                    double thickness; std::size_t n, f, e; fields >> thickness >> n >> f >> e;
                    if (!fields) return false;
                    totalVertices += n*2; totalIndices += (f+e)*6;
                }
            }
            if (!found) return false;
            GpuScene without = scene;
            without.revision += 60000 + revision++;
            bool removed = false;
            for (auto& batch : without.batches) {
                if (batch.objectId != id || batch.vertices.size() != totalVertices || batch.indices.size() != totalIndices) continue;
                std::vector<std::uint32_t> kept;
                for (std::size_t i = 0; i < batch.indices.size(); i += 3) {
                    bool isHeel = false;
                    if (i >= indexOffset && i < indexOffset + 48) {
                        for (int corner = 0; corner < 3; ++corner) {
                            auto index = batch.indices[i+corner];
                            if (index >= vertexOffset+4 && index < vertexOffset+6) isHeel = true;
                        }
                    }
                    if (isHeel) removed = true;
                    else kept.insert(kept.end(), batch.indices.begin()+i, batch.indices.begin()+i+3);
                }
                batch.indices.swap(kept);
            }
            if (!removed) return false;
            NativeFrame plain;
            plain.width = scene.width; plain.height = scene.height;
            plain.wantPixels = true; plain.wantsBgra = true;
            if (!renderVulkanScene(without, plain)) return false;
            const unsigned char* other = plain.direct ? plain.direct : plain.bgra.data();
            int changed = 0;
            for (std::size_t at = 0; at < bytes; at += 4) {
                int difference = 0;
                for (int c = 0; c < 3; ++c) difference += std::abs(int(original[at+c]) - int(other[at+c]));
                if (difference > 24) ++changed;
            }
            std::printf("Vulkan heel-end geometry %u %c: %d visible pixels\n", id, side, changed);
            if (changed < 1) return false;
        }
    }
    heelsVisible = true;
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
    if (ok) {
        view.width = 640; view.height = 480;
        setGpuRenderer(&renderVulkanScene);
        room.render(frame, view, 2.5);
        const std::size_t bytes = static_cast<std::size_t>(view.width) * view.height * 4;
        const unsigned char* before = frame.direct ? frame.direct : frame.bgra.data();
        std::vector<unsigned char> mesh(before, before + bytes);
        room.toggleLineView();
        room.render(frame, view, 2.5);
        const unsigned char* after = frame.direct ? frame.direct : frame.bgra.data();
        int changed = 0;
        for (std::size_t i = 0; i < bytes; i += 4) {
            int difference = 0;
            for (int c = 0; c < 3; ++c)
                difference += std::abs(static_cast<int>(mesh[i+c]) - after[i+c]);
            if (difference > 24) ++changed;
        }
        setGpuRenderer(&renderAndCheck);
        room.render(frame, view, 2.5);
        ok = changed > 1000 && bothVisible;
        for (const auto size : {std::pair<int,int>{800,600}, {480,360}, {640,480}}) {
            view.width = size.first; view.height = size.second;
            room.render(frame, view, 2.5);
            ok = ok && bothVisible;
        }
        view.width = 960; view.height = 720;
        setGpuRenderer(&renderAndCheckDots);
        room.render(frame, view, 2.5);
        ok = ok && dotsVisible;
        setGpuRenderer(&renderAndCheckSolids);
        room.render(frame, view, 2.5);
        ok = ok && solidsVisible;
        setGpuRenderer(&renderAndCheckHeels);
        room.render(frame, view, 2.5);
        ok = ok && heelsVisible;
        view.width = 640; view.height = 480;
        room.toggleLineView();
        setGpuRenderer(&renderVulkanScene);
        room.render(frame, view, 2.5);
        after = frame.direct ? frame.direct : frame.bgra.data();
        int restoreDiff = 0;
        for (std::size_t i = 0; i < bytes; i += 4) {
            int difference = 0;
            for (int c = 0; c < 3; ++c)
                difference += std::abs(static_cast<int>(mesh[i+c]) - after[i+c]);
            if (difference > 24) ++restoreDiff;
        }
        ok = ok && restoreDiff < 10;
        std::printf("Vulkan line view: %s (%d changed pixels; both bodies; mesh restored, %d different pixels)\n",
                    ok ? "PASS" : "FAIL", changed, restoreDiff);
    }
    setGpuRenderer(nullptr);
    shutdownVulkan();
    std::printf("Vulkan room resize: %s (%d offscreen frames)\n", ok ? "PASS" : "FAIL", step);
    return ok ? 0 : 1;
}
