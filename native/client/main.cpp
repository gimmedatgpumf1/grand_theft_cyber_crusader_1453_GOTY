#include "client/client.h"
#include "client/diagnostics.h"
#include "client/interactive.h"
#include "client/renderer.h"
#include "client/skeleton_body.h"
#include "client/skeleton_room.h"
#include "client/window.h"
#include "client/opengl_shared.h"
#include "vulkan/vk_renderer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <filesystem>
#include <vector>
#include <chrono>
#include <iostream>
#include <string>

namespace {

double number(const std::string& value, double fallback = 0) {
    try { return std::stod(value); } catch (...) { return fallback; }
}

// Accepts a scene and draws nothing. Registering it measures the half of the
// frame that stays on the CPU no matter which backend rasterizes: building the
// vertex batches. Whatever is left in a timed frame with this in place is the
// cost the GPU path can never remove.
bool acceptOnly(const gtc::native::GpuScene& scene, gtc::native::NativeFrame& frame) {
    // The frame contract still has to be honoured, or the world renderer treats
    // this as a failed draw; only the pixels are skipped.
    frame.width = std::max(1, scene.width);
    frame.height = std::max(1, scene.height);
    const std::size_t pixels = static_cast<std::size_t>(frame.width) * frame.height;
    frame.rgb.resize(pixels * 3);
    frame.alpha.resize(pixels);
    return true;
}

// One skull, every backend, the same scene and the same size each time. The
// point is to separate the two halves of a frame: what the CPU spends building
// vertex batches, and what the rasterizer spends turning them into pixels. A
// backend can only ever own the second half.
struct SkullScene {
    gtc::native::GpuScene scene;
    gtc::native::Camera camera;
    bool ready = false;
};


// The asset root is discovered, not demanded. Someone who runs the client from
// build/bin, or double-clicks it, has a working directory that is not the
// repository, and being told "cannot open skeleton" for that is a needle in the
// wrong haystack. The candidates are tried in order and the first one that
// actually contains the skeleton is used; if none does, the original value is
// kept so the error names the path that was asked for.
std::string resolveRoot(std::string root) {
    namespace fs = std::filesystem;
    const std::string marker = "/tools/skeleton-source/skeleton-male.obj";
    std::vector<std::string> candidates;
    const auto add = [&](const std::string& value) {
        if (value.empty()) return;
        if (std::find(candidates.begin(), candidates.end(), value) == candidates.end()) candidates.push_back(value);
    };
    const auto withSlash = [](std::string value) {
        while (value.size() > 1 && (value.back() == '/' || value.back() == '\\')) value.pop_back();
        return value;
    };
    add(withSlash(root));
    const std::string executable = gtc::native::executableDirectory();
    if (!executable.empty()) {
        std::error_code code;
        // .../build/bin -> the repository is two levels up; also check one, in
        // case the binary is somewhere else in the tree.
        fs::path cursor(executable);
        for (int level = 0; level < 4; ++level) {
            add(cursor.string());
            if (!cursor.has_parent_path() || cursor.parent_path() == cursor) break;
            cursor = cursor.parent_path();
        }
    }
    std::error_code code;
    fs::path working = fs::current_path(code);
    if (!code) {
        for (int level = 0; level < 4; ++level) {
            add(working.string());
            if (!working.has_parent_path() || working.parent_path() == working) break;
            working = working.parent_path();
        }
    }
    for (const std::string& candidate : candidates) {
        if (fs::exists(candidate + marker, code)) {
            if (candidate != withSlash(root)) {
                gtc::native::tracef("root: using %s (asked for %s)", candidate.c_str(), root.c_str());
            }
            return candidate;
        }
    }
    std::string tried;
    for (const std::string& candidate : candidates) tried += "\n  " + candidate + marker;
    gtc::native::tracef("root: no skeleton under any candidate:%s", tried.c_str());
    return root;
}

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
        if (!model.load(root + "/tools/skeleton-source/skeleton-male.obj", error)) return 1;
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
    }

    if (renderer == "cpu" || renderer == "all") {
        // The software rasterizer: builds and draws in the same pass.
        const double start = nowMs();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            gtc::native::Rasterizer rasterizer(skull.camera.width, skull.camera.height,
                                              gtc::native::colorHex("#0d1210"), skull.camera.focal(), false);
            rasterizer.setCamera(skull.camera);
            gtc::native::SkeletonModel model;
            if (!model.load(root + "/tools/skeleton-source/skeleton-male.obj", error)) return 1;
            const int bone = model.bone("skull");
            model.poseIdle(0.75, 0.0);
            model.draw(rasterizer, skull.camera, {0, 0, 0}, 0.0, bone);
            gtc::native::NativeFrame frame = rasterizer.finish();
            if (frame.rgb.empty()) { std::cerr << "cpu skull frame came back empty\n"; return 1; }
        }
        reportLine("skull cpu raster", (nowMs() - start) / iterations);
    }

    // The GPU backends draw a prebuilt scene, which is what a cache hit looks
    // like: their number is the backend's own, with geometry held constant.
    if (renderer == "vulkan" || renderer == "all") {
        if (!gtc::native::vulkanAvailable()) {
            std::cout << "  vulkan                    unavailable on this machine\n";
        } else {
            gtc::native::NativeFrame frame;
            gtc::native::renderVulkanScene(skull.scene, frame);
            const double start = nowMs();
            for (int iteration = 0; iteration < iterations; ++iteration) {
                if (!gtc::native::renderVulkanScene(skull.scene, frame)) {
                    std::cerr << "vulkan skull draw failed\n";
                    return 1;
                }
            }
            reportLine("skull vulkan (full frame)", (nowMs() - start) / iterations);
            // The same draw with the picture left on the device: commands are
            // submitted and the device is waited for, but nothing is copied
            // back. This is the only figure that can be compared with a backend
            // whose "frame time" is submission alone.
            gtc::native::setFrameReadback(false);
            const double submitStart = nowMs();
            for (int iteration = 0; iteration < iterations; ++iteration) {
                gtc::native::renderVulkanScene(skull.scene, frame);
            }
            reportLine("skull vulkan (submit only)", (nowMs() - submitStart) / iterations);
            gtc::native::setFrameReadback(true);
            // Same frame, colour image kept in linear layout: the copy back is
            // then a memory copy instead of a walk over tiled memory. Costs the
            // rasteriser its swizzle, so both parts are timed rather than assumed.
            gtc::native::setLinearTarget(true);
            gtc::native::renderVulkanScene(skull.scene, frame);
            const double linearStart = nowMs();
            for (int iteration = 0; iteration < iterations; ++iteration) {
                gtc::native::renderVulkanScene(skull.scene, frame);
            }
            reportLine("skull vulkan (linear image)", (nowMs() - linearStart) / iterations);
            gtc::native::setLinearTarget(false);
        }
    }

    if (renderer == "opengl" || renderer == "all") {
        // The OpenGL backend needs a real context, so it is measured through the
        // window's own path -- the same call the game makes.
        std::string windowError;
        gtc::native::NativeWindow window;
        const bool wasSet = std::getenv("GTC_NO_VULKAN") != nullptr;
#ifdef _WIN32
        _putenv_s("GTC_NO_VULKAN", "1");
#else
        setenv("GTC_NO_VULKAN", "1", 1);
#endif
        const bool opened = window.open("GTC skull benchmark", windowError);
        if (!wasSet) {
#ifdef _WIN32
            _putenv_s("GTC_NO_VULKAN", "");
#else
            unsetenv("GTC_NO_VULKAN");
#endif
        }
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
                reportLine("skull opengl", (nowMs() - start) / iterations);
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

// The street, not a test scene: the real world renderer, a real client state,
// and characters that are always moving so every frame takes the slow path the
// game actually takes.
int runWorldBenchmark(const std::string& root, gtc::native::ViewState view, const std::string& renderer,
                      int iterations, int characters) {
    requireProfiling();
    std::string error;
    gtc::native::WorldRenderer world3d;
    if (!world3d.load(root, error)) {
        std::cerr << "World benchmark: " << error << '\n';
        return 1;
    }
    gtc::native::ClientWorld world;
    gtc::native::ClientState state;
    state.tick = 1;
    for (int index = 0; index < std::max(1, characters); ++index) {
        gtc::Character citizen;
        citizen.id = static_cast<std::uint32_t>(index + 1);
        citizen.name = "Citizen";
        citizen.ai = index != 0;
        citizen.position = {static_cast<float>(index) * 0.9F - 1.5F, static_cast<float>(index % 3)};
        citizen.yaw = 3.14159265F;
        citizen.speed = 2.0F;
        gtc::Appearance appearance;
        appearance.male = index % 2 == 0;
        appearance.skin = static_cast<std::uint32_t>(index % 4);
        appearance.hair = static_cast<std::uint32_t>(index % 6);
        appearance.shirt = 1;
        appearance.pants = 1;
        citizen.appearance = appearance;
        state.characters.push_back(citizen);
    }
    view.width = std::max(1, view.width);
    view.height = std::max(1, view.height);
    view.gpu = renderer != "cpu";
    std::cout << "street: " << state.characters.size() << " characters, " << view.width << 'x' << view.height
              << ", renderer " << renderer << '\n';
    if (renderer == "vulkan") {
        if (!gtc::native::vulkanAvailable()) {
            std::cout << "  vulkan                    unavailable on this machine\n";
            return 1;
        }
        gtc::native::setGpuRenderer(&gtc::native::renderVulkanScene);
    } else if (renderer == "scene") {
        gtc::native::setGpuRenderer(&acceptOnly);
    } else if (renderer != "cpu") {
        std::cerr << "unknown renderer: " << renderer << '\n';
        return 2;
    }
    gtc::native::NativeFrame frame;
    // Warm up: compile, allocate, and take the first full rebuild.
    if (!world3d.render(world, state, 1, view, frame, error)) {
        std::cerr << "World benchmark: " << error << '\n';
        return 1;
    }
    // Every frame advances the tick and moves a character, which is exactly the
    // condition that makes the renderer rebuild: nothing here is a cache hit.
    const double start = nowMs();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        state.tick += 1;
        for (gtc::Character& citizen : state.characters) citizen.position.x += 0.01F;
        if (!world3d.render(world, state, 1, view, frame, error)) {
            std::cerr << "World benchmark: " << error << '\n';
            return 1;
        }
    }
    reportLine("street frame", (nowMs() - start) / iterations);
    gtc::native::setGpuRenderer(nullptr);
    if (renderer == "vulkan") {
        std::string path = "build/native-client/world-vulkan.png";
        std::filesystem::create_directories("build/native-client");
        if (frame.save(path)) std::cout << "  saved " << path << '\n';
    }
    gtc::native::shutdownVulkan();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    gtc::native::installCrashHandler(argc > 0 ? argv[0] : "gtc-native-client");
    gtc::native::trace("main: start");
    std::string host = "127.0.0.1", name = "Native Citizen", root = ".";
    std::string output = "build/native-client/frame.png", mode = "tps", toilet, reviewUi;
    int port = 1453, frames = 1, benchmark = 1;
    bool path = false, move = false, headless = false, verifyRoom = false;
    int roomBenchmark = 0;
    int skullBenchmark = 0, worldBenchmark = 0, worldCharacters = 3;
    std::string rendererName = "cpu";
    gtc::Vec2 destination{}, movement{};
    gtc::native::ViewState view;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--headless") headless = true;
        else if (argument == "--verify-room") verifyRoom = true;
        else if (argument == "--renderer" && i + 1 < argc) rendererName = argv[++i];
        else if (argument == "--room-benchmark" && i + 1 < argc) roomBenchmark = static_cast<int>(number(argv[++i], 1));
        else if (argument == "--skull-benchmark" && i + 1 < argc) skullBenchmark = static_cast<int>(number(argv[++i], 1));
        else if (argument == "--world-benchmark" && i + 1 < argc) worldBenchmark = static_cast<int>(number(argv[++i], 1));
        else if (argument == "--characters" && i + 1 < argc) worldCharacters = static_cast<int>(number(argv[++i], 3));
        else if (argument == "--review-ui" && i + 1 < argc) reviewUi = argv[++i];
        else if (argument == "--host" && i + 1 < argc) host = argv[++i];
        else if (argument == "--port" && i + 1 < argc) port = static_cast<int>(number(argv[++i], 1453));
        else if (argument == "--name" && i + 1 < argc) name = argv[++i];
        else if (argument == "--root" && i + 1 < argc) root = argv[++i];
        else if (argument == "--output" && i + 1 < argc) output = argv[++i];
        else if (argument == "--frames" && i + 1 < argc) frames = static_cast<int>(number(argv[++i], 1));
        else if (argument == "--benchmark" && i + 1 < argc) benchmark = std::max(1, static_cast<int>(number(argv[++i], 1)));
        else if (argument == "--width" && i + 1 < argc) view.width = std::max(1, static_cast<int>(number(argv[++i], gtc::native::kDefaultWindowWidth)));
        else if (argument == "--height" && i + 1 < argc) view.height = std::max(1, static_cast<int>(number(argv[++i], gtc::native::kDefaultWindowHeight)));
        else if (argument == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (argument == "--path" && i + 2 < argc) {
            destination = {static_cast<float>(number(argv[++i])), static_cast<float>(number(argv[++i]))};
            path = true;
        } else if (argument == "--move" && i + 2 < argc) {
            movement = {static_cast<float>(number(argv[++i])), static_cast<float>(number(argv[++i]))};
            move = true;
        } else if (argument == "--toilet" && i + 1 < argc) toilet = argv[++i];
        else if (argument == "--yaw" && i + 1 < argc) view.yaw = static_cast<float>(number(argv[++i]));
        else if (argument == "--pitch" && i + 1 < argc) view.pitch = static_cast<float>(number(argv[++i]));
        else {
            std::cerr << "Usage: gtc-native-client [--headless] [--host HOST] [--port 1453] [--name NAME] [--frames N]\n"
                         "  [--mode tps|fps|god] [--path X Z] [--move X Z] [--toilet sit|shit|piss]\n"
                         "  [--review-ui DIR] (render native overlays without a display)\n"
                         "  [--width PIXELS] [--height PIXELS] [--benchmark N]\n"
                         "  [--yaw RADIANS] [--pitch RADIANS] [--output PNG]\n";
            return 2;
        }
    }
    root = resolveRoot(root);
    if (skullBenchmark > 0) return runSkullBenchmark(root, view, rendererName, skullBenchmark);
    if (worldBenchmark > 0) return runWorldBenchmark(root, view, rendererName, worldBenchmark, worldCharacters);
    if (roomBenchmark > 0) {
        // This is the acceptance measurement for the room: the same scene, the
        // same size, timed end to end, with the backend named on the command
        // line. `scene` draws nothing and reports what the CPU still spends;
        // `vulkan` is the real thing and is refused outright when the machine
        // cannot start it, rather than quietly measuring the CPU fallback.
        gtc::native::SkeletonRoom room;
        std::string roomError;
        if (!room.load(root, roomError)) {
            std::cerr << "Test room unavailable: " << roomError << '\n';
            return 1;
        }
        gtc::native::ViewState roomView;
        roomView.width = view.width;
        roomView.height = view.height;
        roomView.yaw = view.yaw;
        roomView.pitch = view.pitch;
        if (rendererName == "vulkan") {
            if (!gtc::native::vulkanAvailable()) {
                std::cerr << "vulkan: unavailable on this machine\n";
                return 1;
            }
            gtc::native::setGpuRenderer(&gtc::native::renderVulkanScene);
            roomView.gpu = true;
        } else if (rendererName == "scene") {
            gtc::native::setGpuRenderer(&acceptOnly);
            roomView.gpu = true;
        } else if (rendererName != "cpu") {
            std::cerr << "unknown renderer: " << rendererName << '\n';
            return 2;
        }
        gtc::native::NativeFrame benchFrame;
        // One warm-up frame, so the first frame's target and buffer allocation
        // does not land in the measurement.
        room.render(benchFrame, roomView, 0.75);
        const auto start = std::chrono::steady_clock::now();
        for (int iteration = 0; iteration < roomBenchmark; ++iteration) {
            // Time advances like it does in the loop, so the pose rebuilds land
            // in the measurement the way they will in use.
            room.render(benchFrame, roomView, 0.75 + iteration * 0.0166);
        }
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() / roomBenchmark;
        std::cout << "room render [" << rendererName << "]: " << milliseconds << " ms/frame at "
                  << roomView.width << 'x' << roomView.height
                  << " (" << (milliseconds > 0 ? 1000.0 / milliseconds : 0.0) << " fps)\n";
        gtc::native::setGpuRenderer(nullptr);
        if (rendererName == "vulkan") {
            if (!benchFrame.save(output)) std::cerr << "could not save " << output << '\n';
        }
        gtc::native::shutdownVulkan();
        return 0;
    }
    if (verifyRoom) {
        // The room is presented through the same path as the world, and that
        // path needs a coverage mask as well as colour. This check exists
        // because a missing mask does not warn -- it just ends the session.
        gtc::native::SkeletonRoom room;
        std::string roomError;
        if (!room.load(root, roomError)) {
            std::cerr << "Test room unavailable: " << roomError << '\n';
            return 1;
        }
        for (const std::pair<int, int>& size : {std::pair<int, int>{640, 480}, std::pair<int, int>{1568, 929}}) {
            gtc::native::ViewState roomView;
            roomView.width = size.first;
            roomView.height = size.second;
            gtc::native::NativeFrame frame;
            room.render(frame, roomView, 0.75);
            const std::size_t pixels = static_cast<std::size_t>(frame.width) * frame.height;
            if (frame.width != size.first || frame.height != size.second || frame.rgb.size() < pixels * 3 ||
                frame.alpha.size() < pixels) {
                std::cerr << "Test room frame contract broken at " << size.first << 'x' << size.second
                          << ": rgb " << frame.rgb.size() << " alpha " << frame.alpha.size() << '\n';
                return 1;
            }
            if (!frame.save(output + ".room-" + std::to_string(size.first) + ".png")) return 1;
        }
        std::cout << "test room: PASS (male " << room.male().height() << " m, female " << room.female().height()
                  << " m, " << room.male().bones().size() << " bones each)\n";
        return 0;
    }
    if (mode == "fps") view.mode = gtc::native::ViewMode::FirstPerson;
    else if (mode == "god") view.mode = gtc::native::ViewMode::God;
    else view.mode = gtc::native::ViewMode::ThirdPerson;
    if (!headless || !reviewUi.empty()) {
        gtc::native::InteractiveOptions options;
        options.host = host;
        options.port = static_cast<std::uint16_t>(port);
        options.name = name;
        options.root = root;
        if (!reviewUi.empty()) return gtc::native::runInteractiveReview(options, reviewUi);
        return gtc::native::runInteractive(options);
    }
    std::string error;
    gtc::native::NativeClient client;
    if (!client.connect(host, static_cast<std::uint16_t>(port), error) || !client.sendJoin(name)) {
        std::cerr << error << '\n';
        return 1;
    }
    if (path) client.sendPath(destination);
    if (!toilet.empty()) client.sendToilet(toilet); // Strict server range check; never an implicit path.
    gtc::native::WorldRenderer renderer;
    if (!renderer.load(root, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    for (int frame = 0; frame < frames; ++frame) {
        if (move) client.sendInput(movement, false, false, view.yaw, view.pitch);
        if (!client.receive(error)) {
            std::cerr << error << '\n';
            return 1;
        }
    }
    gtc::native::NativeFrame frame;
    if (!renderer.render(client.world(), client.state(), client.playerId(), view, frame, error)) {
        std::cerr << "Native render failed: " << error << '\n';
        return 1;
    }
    const auto benchmarkStart = std::chrono::steady_clock::now();
    for (int iteration = 1; iteration < benchmark; ++iteration) {
        if (!renderer.render(client.world(), client.state(), client.playerId(), view, frame, error)) {
            std::cerr << "Native render failed: " << error << '\n';
            return 1;
        }
    }
    if (benchmark > 1) {
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - benchmarkStart).count() / (benchmark - 1);
        std::cout << "native render: " << milliseconds << " ms/frame at " << view.width << "x" << view.height << '\n';
    }
    if (!frame.save(output)) {
        std::cerr << "Native render failed: cannot save " << output << '\n';
        return 1;
    }
    std::cout << "native client: id=" << client.playerId() << " tick=" << client.state().tick
              << " citizens=" << client.state().characters.size() << " frame=" << output << '\n';
    return 0;
}
