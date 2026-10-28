// The measurements, away from the argument parsing that starts them.
//
// Every number the client prints about how fast it draws is written here, next to
// the scene it draws, so a benchmark cannot quietly measure something the game
// does not do -- which is exactly what a frame copied back to the host is.

#include "client/benchmark.h"

#include "client/opengl_shared.h"
#include "client/skeleton_body.h"
#include "client/skeleton_room.h"
#include "client/threadpool.h"
#include "client/window.h"
#include "vulkan/vk_renderer.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

namespace gtc::native {



// The asset root is discovered, not demanded. Someone who runs the client from
// build/bin, or double-clicks it, has a working directory that is not the
// repository, and being told "cannot open skeleton" for that is a needle in the
// wrong haystack. The candidates are tried in order and the first one that
// actually contains the skeleton is used; if none does, the original value is
// kept so the error names the path that was asked for.

void requireProfiling() {
    // The benchmark is the diagnostic; it should not need a second environment
    // variable to say which stage it is timing.
#ifdef _WIN32
    if (std::getenv("GTC_VK_PROFILE") == nullptr) _putenv_s("GTC_VK_PROFILE", "1");
#else
    setenv("GTC_VK_PROFILE", "1", 1);
#endif
}


double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}


void reportLine(const char* label, double milliseconds) {
    std::cout << "  " << std::left << std::setw(30) << label << std::right << std::fixed
              << std::setprecision(2) << std::setw(8) << milliseconds << " ms/frame"
              << std::setprecision(1) << std::setw(9) << (milliseconds > 0 ? 1000.0 / milliseconds : 0.0)
              << " fps\n";
}

namespace {

struct SkullScene {
    gtc::native::GpuScene scene;
    gtc::native::Camera camera;
    bool ready = false;
};



// A mean hides the frames that actually hurt: one 7 ms frame and nineteen 3 ms
// frames average to 3.2, which is a number nobody can act on. The median is what
// the frame rate feels like; the worst frame is what a user sees as a hitch.
void reportSpread(const char* label, std::vector<double> samples) {
    if (samples.empty()) return;
    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    const double worst = samples.back();
    std::cout << "  " << std::left << std::setw(30) << label << std::right << std::fixed
              << std::setprecision(2) << std::setw(8) << median << " ms median"
              << std::setprecision(2) << std::setw(10) << worst << " ms worst\n";
}


// Builds the one-skull scene the benchmark draws: a skull alone, the camera on
// it, everything else absent so both backends see exactly the same triangles.
bool buildSkull(const std::string& root, const gtc::native::ViewState& view, SkullScene& out,
                std::string& error) {
    gtc::native::SkeletonModel model;
    if (!model.load(root + "/tools/skeleton-source/skeleton-male.obj", error)) return false;
    const int skull = model.bone("skull");
    if (skull < 0) {
        error = "no skull bone in the skeleton";
        return false;
    }
    model.poseIdle(0.75, 0.0);
    out.camera.width = std::max(1, view.width);
    out.camera.height = std::max(1, view.height);
    out.camera.fovDegrees = 50;
    // Eye height on the skull, a metre and a half away: the skull fills the
    // frame, so the measurement is rasterization and not scene traversal.
    out.camera.position = {0.0, 1.62, -1.35};
    out.camera.target = {0.0, 1.60, 0.0};
    gtc::native::Rasterizer rasterizer(out.camera.width, out.camera.height, gtc::native::colorHex("#0d1210"),
                                      out.camera.focal(), true, gtc::native::GpuScene{});
    rasterizer.setCamera(out.camera);
    model.draw(rasterizer, out.camera, {0, 0, 0}, 0.0, skull);
    out.scene = rasterizer.takeGpuScene();
    out.scene.width = out.camera.width;
    out.scene.height = out.camera.height;
    out.scene.focal = out.camera.focal();
    out.scene.viewMatrix = out.camera.viewMatrix();
    out.scene.revision = 1;
    for (gtc::native::GpuBatch& batch : out.scene.batches) batch.revision = 1;
    out.ready = true;
    return true;
}




// The same idea one size up: the whole body, every bone, the camera far enough
// back to frame all of it. The skull stopped being interesting the moment its
// cost was known; this is the geometry a room actually draws, four times over.
bool buildBody(const std::string& root, const gtc::native::ViewState& view, SkullScene& out,
               std::string& error) {
    gtc::native::SkeletonModel model;
    if (!model.load(root + "/tools/skeleton-source/skeleton-male.obj", error)) return false;
    model.poseIdle(0.75, 0.0);
    out.camera.width = std::max(1, view.width);
    out.camera.height = std::max(1, view.height);
    out.camera.fovDegrees = 50;
    // Two and a half metres back at eye height: the body fills the middle of
    // the frame, the way it stands in the test room.
    out.camera.position = {0.0, 1.0, -2.6};
    out.camera.target = {0.0, 0.88, 0.0};
    gtc::native::Rasterizer rasterizer(out.camera.width, out.camera.height, gtc::native::colorHex("#0d1210"),
                                      out.camera.focal(), true, gtc::native::GpuScene{});
    rasterizer.setCamera(out.camera);
    model.draw(rasterizer, out.camera, {0, 0, 0}, 0.0);
    out.scene = rasterizer.takeGpuScene();
    out.scene.width = out.camera.width;
    out.scene.height = out.camera.height;
    out.scene.focal = out.camera.focal();
    out.scene.viewMatrix = out.camera.viewMatrix();
    out.scene.revision = 1;
    for (gtc::native::GpuBatch& batch : out.scene.batches) batch.revision = 1;
    out.ready = true;
    return true;
}


// The same scene with every triangle collapsed to a single point. The vertex
// stage still runs over every vertex and every draw is still submitted; the
// rasteriser then has nothing to fill. What is left is the cost of moving the
// geometry, which is the half of the frame the fragment shader hides.
void collapseTriangles(gtc::native::GpuScene& scene) {
    for (gtc::native::GpuBatch& batch : scene.batches) {
        for (gtc::native::GpuVertex& vertex : batch.vertices) {
            vertex.x = vertex.y = vertex.z = 0.0F;
        }
    }
}

} // namespace


// The measurement King asked for: one skull, drawn by each backend, plus the
// CPU geometry cost that no backend can remove.
int runSkullBenchmark(const std::string& root, gtc::native::ViewState view, const std::string& renderer,
                      int iterations) {
    requireProfiling();
    std::string error;
    SkullScene skull;
    if (!buildSkull(root, view, skull, error)) {
        std::cerr << "Skull benchmark: " << error << '\n';
        return 1;
    }
    std::size_t vertices = 0, triangles = 0;
    for (const gtc::native::GpuBatch& batch : skull.scene.batches) {
        vertices += batch.vertices.size();
        triangles += batch.vertices.size() / 3;
    }
    std::cout << "single skull: " << triangles << " triangles, " << vertices << " vertices, "
              << skull.camera.width << 'x' << skull.camera.height << '\n';
    std::cout << "threads for frame conversions: " << gtc::native::frameThreads() << '\n';

    // What the window does with a finished frame: reorder it for the device
    // context. Measured because it is per frame, every frame, at this size.
    {
        gtc::native::NativeFrame probe;
        probe.width = skull.camera.width;
        probe.height = skull.camera.height;
        probe.rgb.assign(static_cast<std::size_t>(probe.width) * probe.height * 3, 96);
        std::vector<unsigned char> bgra;
        gtc::native::rgbToBgra(probe, bgra);
        const double start = nowMs();
        for (int iteration = 0; iteration < iterations; ++iteration) gtc::native::rgbToBgra(probe, bgra);
        reportLine("present reorder (threaded)", (nowMs() - start) / iterations);
        if (bgra.size() != static_cast<std::size_t>(probe.width) * probe.height * 4 || bgra[3] != 255) {
            std::cerr << "rgbToBgra produced the wrong shape\n";
            return 1;
        }
    }

    // Build cost: posing the bone and emitting its triangles into a batch.
    // Every animated character in the world pays this, every frame it moves.
    {
        gtc::native::SkeletonModel model;
        std::string poseError;
        if (!model.load(root + "/tools/skeleton-source/skeleton-male.obj", poseError)) return 1;
        const int bone = model.bone("skull");
        model.poseIdle(0.75, 0.0);
        const double start = nowMs();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            gtc::native::Rasterizer rasterizer(skull.camera.width, skull.camera.height,
                                              gtc::native::colorHex("#0d1210"), skull.camera.focal(), true,
                                              gtc::native::GpuScene{});
            rasterizer.setCamera(skull.camera);
            model.draw(rasterizer, skull.camera, {0, 0, 0}, 0.0, bone);
            gtc::native::GpuScene built = rasterizer.takeGpuScene();
            if (built.batches.empty()) { std::cerr << "skull build produced nothing\n"; return 1; }
        }
        reportLine("skull geometry (cpu)", (nowMs() - start) / iterations);
    }

    // A whole body, the way the world builds one for every character it draws:
    // this is the number that decides the frame time in the street, because it
    // is paid per character per frame on top of the backend's own cost.
    {
        gtc::native::SkeletonModel model;
        if (!model.load(root + "/tools/skeleton-source/skeleton-male.obj", error)) return 1;
        model.poseIdle(0.75, 0.0);
        std::size_t bodyTriangles = 0;
        const double start = nowMs();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            gtc::native::Rasterizer rasterizer(skull.camera.width, skull.camera.height,
                                              gtc::native::colorHex("#0d1210"), skull.camera.focal(), true,
                                              gtc::native::GpuScene{});
            rasterizer.setCamera(skull.camera);
            model.draw(rasterizer, skull.camera, {0, 0, 0}, 0.0);
            gtc::native::GpuScene built = rasterizer.takeGpuScene();
            bodyTriangles = 0;
            for (const gtc::native::GpuBatch& batch : built.batches) bodyTriangles += batch.vertices.size() / 3;
        }
        reportLine("whole skeleton geometry", (nowMs() - start) / iterations);
        std::cout << "  (" << bodyTriangles << " triangles per body)\n";

        // The same body the way the room draws it: geometry emitted once, then a
        // pose costs one matrix per bone. This, not the line above, is what the
        // test room pays when the pose updates.
        gtc::native::GpuScene rest;
        std::vector<std::size_t> boneBatches;
        {
            gtc::native::Rasterizer rasterizer(skull.camera.width, skull.camera.height,
                                              gtc::native::colorHex("#0d1210"), skull.camera.focal(), true);
            rasterizer.setCamera(skull.camera);
            model.buildRestGeometry(rasterizer, skull.camera, 9001);
            rest = rasterizer.takeGpuScene();
            for (std::size_t index = 0; index < rest.batches.size(); ++index) boneBatches.push_back(index);
        }
        const double poseStart = nowMs();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            const std::vector<std::array<double, 16>> matrices =
                model.boneMatrices({0.0, 0.0, 0.0}, 3.14159265);
            for (std::size_t index = 0; index < boneBatches.size(); ++index) {
                rest.batches[boneBatches[index]].modelMatrix = matrices[index % matrices.size()];
            }
        }
        reportLine("whole skeleton pose (matrices)", (nowMs() - poseStart) / iterations);
    }

    if (renderer == "cpu" || renderer == "all") {
        // The software rasterizer: builds and draws in the same pass.
        // The model is loaded once, outside the loop: loading an OBJ is not
        // rasterization, and timing the two together hides what the rasterizer
        // costs.
        gtc::native::SkeletonModel model;
        if (!model.load(root + "/tools/skeleton-source/skeleton-male.obj", error)) return 1;
        const int bone = model.bone("skull");
        gtc::native::NativeFrame frame;
        {
            gtc::native::Rasterizer warm(skull.camera.width, skull.camera.height, gtc::native::colorHex("#0d1210"),
                                         skull.camera.focal(), false);
            warm.setCamera(skull.camera);
            model.poseIdle(0.75, 0.0);
            model.draw(warm, skull.camera, {0, 0, 0}, 0.0, bone);
            frame = warm.finish();
            if (frame.rgb.empty()) { std::cerr << "cpu skull frame came back empty\n"; return 1; }
        }
        double submitMs = 0.0, finishMs = 0.0;
        const double start = nowMs();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            gtc::native::Rasterizer rasterizer(skull.camera.width, skull.camera.height,
                                              gtc::native::colorHex("#0d1210"), skull.camera.focal(), false);
            rasterizer.setCamera(skull.camera);
            const double submitStart = nowMs();
            model.poseIdle(0.75, 0.0);
            model.draw(rasterizer, skull.camera, {0, 0, 0}, 0.0, bone);
            submitMs += nowMs() - submitStart;
            const double finishStart = nowMs();
            frame = rasterizer.finish();
            finishMs += nowMs() - finishStart;
            if (frame.rgb.empty()) { std::cerr << "cpu skull frame came back empty\n"; return 1; }
        }
        reportLine("skull cpu raster", (nowMs() - start) / iterations);
        reportLine("  of which submit", submitMs / iterations);
        reportLine("  of which draw", finishMs / iterations);
    }

    // The GPU backends draw a prebuilt scene, which is what a cache hit looks
    // like: their number is the backend's own, with geometry held constant.
    if (renderer == "vulkan" || renderer == "all") {
        // The window is opened before the renderer is asked anything, because
        // opening it is what hands over the window to present to, and that
        // question is answered once, when the instance is created. A benchmark
        // that opened it later would measure the readback path and call it the
        // game's frame.
        gtc::native::NativeWindow benchWindow;
        std::string benchWindowError;
        const bool windowed = benchWindow.open("GTC benchmark", benchWindowError);
        if (!gtc::native::vulkanAvailable()) {
            std::cout << "  vulkan                    unavailable on this machine\n";
        } else {
            gtc::native::NativeFrame frame;
            if (!windowed) {
                std::cout << "  window frame              unavailable: " << benchWindowError << '\n';
            } else {
                benchWindow.resize(skull.camera.width, skull.camera.height);
                int windowWidth = 0, windowHeight = 0;
                benchWindow.size(windowWidth, windowHeight);
                const auto measureWindow = [&](const char* label, const gtc::native::GpuScene& scene,
                                               const char* what) {
                    for (int warmUp = 0; warmUp < 24; ++warmUp) {
                        if (!benchWindow.renderScene(scene, frame)) return;
                        benchWindow.present(frame);
                    }
                    std::vector<double> samples;
                    samples.reserve(static_cast<std::size_t>(iterations));
                    for (int iteration = 0; iteration < iterations; ++iteration) {
                        const double start = nowMs();
                        if (!benchWindow.renderScene(scene, frame)) return;
                        benchWindow.present(frame);
                        samples.push_back(nowMs() - start);
                    }
                    std::sort(samples.begin(), samples.end());
                    // The median, not the mean: the window path has the odd frame
                    // where the compositor gets in the way, and one of those in
                    // twenty is not what the frame rate feels like.
                    std::cout << "  " << what << " at " << windowWidth << 'x' << windowHeight << '\n';
                    reportLine(label, samples[samples.size() / 2]);
                };
                measureWindow("skull vulkan (window frame)", skull.scene, "skull");
                SkullScene windowBody;
                if (buildBody(root, view, windowBody, error)) {
                    measureWindow("skeleton vulkan (window frame)", windowBody.scene, "whole skeleton");
                }
            }
            // An iGPU measured from cold idles at its lowest clock, and the
            // first block would then blame the renderer for the governor. Spin
            // the device until frames stop getting faster rather than for a
            // fixed count: a fixed count was still 60% off at the end of the run.
            {
                double previous = 0.0;
                int used = 0;
                for (int warmUp = 0; warmUp < 400; ++warmUp) {
                    const double frameStart = nowMs();
                    gtc::native::renderVulkanScene(skull.scene, frame);
                    const double spent = nowMs() - frameStart;
                    ++used;
                    if (used > 8 && spent > previous * 0.97) break;
                    previous = spent;
                }
                gtc::native::drainVulkan();
            }
            // The offscreen blocks want the pixels: with a surface open, a frame
            // that does not ask for them is presented to the window instead of
            // being copied back, and then there is nothing to measure here.
            frame.wantPixels = true;
            gtc::native::renderVulkanScene(skull.scene, frame);
            gtc::native::drainVulkan();
            std::vector<double> samples;
            samples.reserve(static_cast<std::size_t>(iterations));
            double total = 0.0;
            for (int iteration = 0; iteration < iterations; ++iteration) {
                const double frameStart = nowMs();
                if (!gtc::native::renderVulkanScene(skull.scene, frame)) {
                    std::cerr << "vulkan skull draw failed\n";
                    return 1;
                }
                const double spent = nowMs() - frameStart;
                samples.push_back(spent);
                total += spent;
            }
            reportLine("skull vulkan (full frame)", total / iterations);
            reportSpread("  of which median", samples);
            // The same frame with no triangles in it: the clears, the copies and
            // the submission, and nothing drawn. Whatever this costs is paid
            // before any geometry is looked at, and the difference from the line
            // above is what the drawing itself costs.
            {
                gtc::native::GpuScene empty = skull.scene;
                empty.batches.clear();
                for (int warmUp = 0; warmUp < 8; ++warmUp) gtc::native::renderVulkanScene(empty, frame);
                gtc::native::drainVulkan();
                const double emptyStart = nowMs();
                for (int iteration = 0; iteration < iterations; ++iteration) {
                    if (!gtc::native::renderVulkanScene(empty, frame)) {
                        std::cerr << "vulkan empty frame failed\n";
                        return 1;
                    }
                }
                reportLine("skull vulkan (no draws)", (nowMs() - emptyStart) / iterations);
            }
            // The whole body: four times the skull's geometry, drawn the same
            // way. Measured here rather than inferred, because per-triangle cost
            // does not stay linear once the fragments start overlapping.
            {
                SkullScene body;
                if (!buildBody(root, view, body, error)) {
                    std::cerr << "Whole skeleton: " << error << '\n';
                    return 1;
                }
                std::size_t bodyTriangles = 0, bodyVertices = 0;
                for (const gtc::native::GpuBatch& batch : body.scene.batches) {
                    bodyTriangles += batch.vertices.size() / 3;
                    bodyVertices += batch.vertices.size();
                }
                std::cout << "whole skeleton: " << bodyTriangles << " triangles, " << bodyVertices
                          << " vertices\n";
                for (int warmUp = 0; warmUp < 8; ++warmUp) gtc::native::renderVulkanScene(body.scene, frame);
                gtc::native::drainVulkan();
                std::vector<double> samples;
                samples.reserve(static_cast<std::size_t>(iterations));
                double total = 0.0;
                for (int iteration = 0; iteration < iterations; ++iteration) {
                    const double frameStart = nowMs();
                    if (!gtc::native::renderVulkanScene(body.scene, frame)) {
                        std::cerr << "vulkan whole skeleton draw failed\n";
                        return 1;
                    }
                    const double spent = nowMs() - frameStart;
                    samples.push_back(spent);
                    total += spent;
                }
                reportLine("skeleton vulkan (full frame)", total / iterations);
                reportSpread("  of which median", samples);
                // Every vertex, no pixels: the vertex stage and the submission
                // without the fragment work, so the two halves can be told apart
                // instead of argued about.
                gtc::native::GpuScene empty = body.scene;
                collapseTriangles(empty);
                for (int warmUp = 0; warmUp < 8; ++warmUp) gtc::native::renderVulkanScene(empty, frame);
                gtc::native::drainVulkan();
                const double vertexStart = nowMs();
                for (int iteration = 0; iteration < iterations; ++iteration) {
                    if (!gtc::native::renderVulkanScene(empty, frame)) {
                        std::cerr << "vulkan vertex-only frame failed\n";
                        return 1;
                    }
                }
                reportLine("skeleton vulkan (vertices only)", (nowMs() - vertexStart) / iterations);
            }
            // The same draw with the picture left on the device: commands are
            // submitted and the device is waited for, but nothing is copied
            // back. This is the only figure that can be compared with a backend
            // whose "frame time" is submission alone.
            gtc::native::setFrameReadback(false);
            gtc::native::drainVulkan();
            gtc::native::renderVulkanScene(skull.scene, frame);
            gtc::native::drainVulkan();
            const double submitStart = nowMs();
            for (int iteration = 0; iteration < iterations; ++iteration) {
                gtc::native::renderVulkanScene(skull.scene, frame);
            }
            reportLine("skull vulkan (submit only)", (nowMs() - submitStart) / iterations);
            // The same submission with the colour image tiled. Rendering into
            // linear memory costs the rasteriser its swizzle, so the GPU half of
            // the frame is timed in both layouts rather than assumed equal.
            gtc::native::setLinearTarget(false);
            gtc::native::drainVulkan();
            gtc::native::renderVulkanScene(skull.scene, frame);
            gtc::native::drainVulkan();
            const double tiledStart = nowMs();
            for (int iteration = 0; iteration < iterations; ++iteration) {
                gtc::native::renderVulkanScene(skull.scene, frame);
            }
            reportLine("skull vulkan (submit only, tiled)", (nowMs() - tiledStart) / iterations);
            // The whole skeleton with nothing copied back, which is the shape of
            // a frame that goes to the window: submission and the device's own
            // work, and no host traffic at all.
            {
                SkullScene body;
                if (buildBody(root, view, body, error)) {
                    for (int warmUp = 0; warmUp < 8; ++warmUp) gtc::native::renderVulkanScene(body.scene, frame);
                    gtc::native::drainVulkan();
                    const double bodySubmitStart = nowMs();
                    for (int iteration = 0; iteration < iterations; ++iteration) {
                        gtc::native::renderVulkanScene(body.scene, frame);
                    }
                    reportLine("skeleton vulkan (submit only)", (nowMs() - bodySubmitStart) / iterations);
                }
            }
            gtc::native::setLinearTarget(true);
            gtc::native::setFrameReadback(true);
            // Same frame, colour image kept in linear layout: the copy back is
            // then a memory copy instead of a walk over tiled memory. Costs the
            // rasteriser its swizzle, so both parts are timed rather than assumed.
            gtc::native::setLinearTarget(true);
            gtc::native::drainVulkan();
            gtc::native::renderVulkanScene(skull.scene, frame);
            gtc::native::drainVulkan();
            const double linearStart = nowMs();
            for (int iteration = 0; iteration < iterations; ++iteration) {
                gtc::native::renderVulkanScene(skull.scene, frame);
            }
            reportLine("skull vulkan (linear image)", (nowMs() - linearStart) / iterations);
            gtc::native::setLinearTarget(false);
            // The same frame again, with the readback buffer in each of the
            // memory types a driver might expose. The GPU's copy out of the
            // image is the expensive half, and where it lands decides its speed.
            for (int mode = 1; mode <= 2; ++mode) {
                gtc::native::setReadbackMemory(mode);
                gtc::native::drainVulkan();
                gtc::native::renderVulkanScene(skull.scene, frame);
                gtc::native::drainVulkan();
                const double modeStart = nowMs();
                for (int iteration = 0; iteration < iterations; ++iteration) {
                    gtc::native::renderVulkanScene(skull.scene, frame);
                }
                reportLine(mode == 1 ? "skull vulkan (device-local read)"
                                     : "skull vulkan (wc read)", (nowMs() - modeStart) / iterations);
            }
            gtc::native::setReadbackMemory(0);
            gtc::native::drainVulkan();
            // The first block's exact configuration, measured again at the end.
            // If the two disagree, the frame time depends on when it is taken
            // and the block order -- not the renderer -- is what changed.
            gtc::native::renderVulkanScene(skull.scene, frame);
            gtc::native::drainVulkan();
            std::vector<double> repeat;
            repeat.reserve(static_cast<std::size_t>(iterations));
            double repeatTotal = 0.0;
            for (int iteration = 0; iteration < iterations; ++iteration) {
                const double frameStart = nowMs();
                gtc::native::renderVulkanScene(skull.scene, frame);
                const double spent = nowMs() - frameStart;
                repeat.push_back(spent);
                repeatTotal += spent;
            }
            reportLine("skull vulkan (full frame, again)", repeatTotal / iterations);
            reportSpread("  of which median", repeat);
            // Closed before the OpenGL block, which opens a window of its own and
            // takes over the renderer the window layer keeps for its backend.
            benchWindow.close();
        }
    }

    if (renderer == "opengl" || renderer == "all") {
        // The OpenGL backend needs a real context, so it is measured through the
        // window's own path -- the same call the game makes.
        std::string windowError;
        gtc::native::NativeWindow window;
        // Asked for by name. There is no environment variable for this any more:
        // it was how the game ended up on the other backend without saying so.
        const bool opened = window.open("GTC skull benchmark", windowError, true);
        if (!opened) {
            std::cout << "  opengl                    unavailable: " << windowError << '\n';
        } else {
            gtc::native::NativeFrame frame;
            window.renderScene(skull.scene, frame);
            const double start = nowMs();
            bool ok = true;
            for (int iteration = 0; iteration < iterations && ok; ++iteration) {
                ok = window.renderScene(skull.scene, frame);
            }
            if (!ok) {
                std::cerr << "opengl skull draw failed\n";
            } else {
                reportLine("skull opengl (submit only)", (nowMs() - start) / iterations);
                // The same draw, but the device is waited for -- and then the
                // picture is brought back to host memory. Those are the two
                // things a Vulkan frame already pays and this number above does
                // not, so the comparison is only fair here.
                const double waitStart = nowMs();
                for (int iteration = 0; iteration < iterations; ++iteration) {
                    window.renderScene(skull.scene, frame);
                    window.awaitGpu();
                }
                reportLine("skull opengl (gpu done)", (nowMs() - waitStart) / iterations);
                const double backStart = nowMs();
                for (int iteration = 0; iteration < iterations; ++iteration) {
                    window.renderScene(skull.scene, frame);
                    if (!window.readBack(skull.camera.width, skull.camera.height, skull.scene, frame)) break;
                }
                reportLine("skull opengl (frame back)", (nowMs() - backStart) / iterations);
                // Presenting is part of the frame the user sees, and on this
                // path it is a CPU blit, so it is measured on its own.
                const double presentStart = nowMs();
                for (int iteration = 0; iteration < iterations; ++iteration) window.present(frame);
                reportLine("  of which present", (nowMs() - presentStart) / iterations);
            }
            window.close();
        }
    }

    gtc::native::shutdownVulkan();
    return 0;
}

} // namespace gtc::native
