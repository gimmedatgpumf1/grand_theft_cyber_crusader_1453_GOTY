#include "client/benchmark.h"
#include "client/client.h"
#include "client/diagnostics.h"
#include "client/interactive.h"
#include "client/renderer.h"
#include "client/skeleton_body.h"
#include "client/skeleton_room.h"
#include "client/threadpool.h"
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

// Shared with the benchmark, which is where the numbers are now written down.
using gtc::native::nowMs;
using gtc::native::reportLine;
using gtc::native::requireProfiling;

double number(const std::string& value, double fallback = 0) {
    try { return std::stod(value); } catch (...) { return fallback; }
}

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

// The test room, end to end: the whole scene, the backend named, the frame
// timed. Extracted from main() so a no-argument run can use it too.
int runRoomBenchmark(const std::string& root, gtc::native::ViewState view, const std::string& rendererName,
                     int roomBenchmark, const std::string& output) {
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
                  << " (" << (milliseconds > 0 ? 1000.0 / milliseconds : 0.0) << " fps, "
                  << room.triangles() << " triangles)\n";
        // The number that matters is the one the window sees, not the one a
        // readback measures. Same room, same size, drawn straight to a window:
        // no copy back, no conversion, nothing between the device and the
        // screen -- which is the shape of the game's frame.
        if (rendererName == "vulkan" && gtc::native::vulkanAvailable()) {
            gtc::native::NativeWindow window;
            std::string windowError;
            if (!window.open("GTC room", windowError)) {
                std::cout << "  room                      the window did not open: " << windowError << '\n';
            } else {
                window.resize(roomView.width, roomView.height);
                int windowWidth = 0, windowHeight = 0;
                window.size(windowWidth, windowHeight);
                double seconds = 0.75;
                for (int warmUp = 0; warmUp < 24; ++warmUp) {
                    seconds += 0.0166;
                    room.render(benchFrame, roomView, seconds);
                    window.present(benchFrame);
                }
                std::vector<double> samples;
                samples.reserve(static_cast<std::size_t>(roomBenchmark));
                for (int iteration = 0; iteration < roomBenchmark; ++iteration) {
                    seconds += 0.0166;
                    const double frameStart = gtc::native::nowMs();
                    room.render(benchFrame, roomView, seconds);
                    window.present(benchFrame);
                    samples.push_back(gtc::native::nowMs() - frameStart);
                }
                std::sort(samples.begin(), samples.end());
                std::cout << "  room at " << windowWidth << 'x' << windowHeight << '\n';
                gtc::native::reportLine("room vulkan (window frame)", samples[samples.size() / 2]);
                window.close();
            }
        }
        gtc::native::setGpuRenderer(nullptr);
        if (rendererName == "vulkan") {
            if (!benchFrame.save(output)) std::cerr << "could not save " << output << '\n';
        }
        return 0;
}

// One skull, every backend, the same scene and the same size each time. The
// point is to separate the two halves of a frame: what the CPU spends building
// vertex batches, and what the rasterizer spends turning them into pixels. A
// backend can only ever own the second half.
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
    bool path = false, move = false, headless = false, verifyRoom = false, verifyPose = false;
    int roomBenchmark = 0;
    int skullBenchmark = 0, worldBenchmark = 0, worldCharacters = 3;
    std::string rendererName = "cpu";
    gtc::Vec2 destination{}, movement{};
    gtc::native::ViewState view;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--headless") headless = true;
        else if (argument == "--verify-room") verifyRoom = true;
        else if (argument == "--verify-pose") verifyPose = true;
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
    // No arguments at all is the normal case: the whole measurement runs, at the
    // size the game is played at, and then the game opens. Nothing to remember,
    // nothing to type, and the numbers land in the same place every time.
    if (argc <= 1) {
        gtc::native::ViewState benchView = view;
        benchView.width = 1568;
        benchView.height = 929;
        const int rounds = 20;
        std::cout << "gtc: no arguments, so everything is measured at " << benchView.width << 'x'
                  << benchView.height << '\n';
        if (gtc::native::vulkanAvailable()) {
            runSkullBenchmark(root, benchView, "all", rounds);
            runRoomBenchmark(root, benchView, "vulkan", rounds, output);
            runWorldBenchmark(root, benchView, "vulkan", rounds, worldCharacters);
        } else {
            std::cout << "vulkan: unavailable, so the software paths are measured\n";
            runSkullBenchmark(root, benchView, "cpu", rounds);
            runRoomBenchmark(root, benchView, "cpu", rounds, output);
        }
        std::cout << "gtc: measurement done, opening the game\n";
    }
    if (skullBenchmark > 0) return runSkullBenchmark(root, view, rendererName, skullBenchmark);
    if (worldBenchmark > 0) return runWorldBenchmark(root, view, rendererName, worldBenchmark, worldCharacters);
    if (roomBenchmark > 0) {
        const int rc = runRoomBenchmark(root, view, rendererName, roomBenchmark, output);
        gtc::native::shutdownVulkan();
        return rc;
    }
    if (verifyPose) {
        // The room draws bones as rest geometry moved by one matrix each, while
        // the software path transforms every vertex on the CPU. Those two have
        // to agree, or the same scene looks different depending on the backend.
        gtc::native::SkeletonModel model;
        std::string poseError;
        if (!model.load(root + "/tools/skeleton-source/skeleton-male.obj", poseError)) return 1;
        model.poseIdle(0.75, 0.0);
        gtc::native::Camera camera;
        camera.width = 1568;
        camera.height = 929;
        const gtc::native::GpuScene posed = [&] {
            gtc::native::Rasterizer rasterizer(camera.width, camera.height, gtc::native::colorHex("#0d1210"),
                                               camera.focal(), true);
            rasterizer.setCamera(camera);
            model.draw(rasterizer, camera, {-0.52, 0, 0}, 3.14159265);
            return rasterizer.takeGpuScene();
        }();
        gtc::native::GpuScene rest;
        const int base = 9001;
        {
            gtc::native::Rasterizer rasterizer(camera.width, camera.height, gtc::native::colorHex("#0d1210"),
                                               camera.focal(), true);
            rasterizer.setCamera(camera);
            model.buildRestGeometry(rasterizer, camera, base);
            rest = rasterizer.takeGpuScene();
        }
        const std::vector<std::array<double, 16>> matrices = model.boneMatrices({-0.52, 0, 0}, 3.14159265);
        std::size_t posedCount = 0;
        for (const gtc::native::GpuBatch& batch : posed.batches) posedCount += batch.vertices.size();
        std::size_t restCount = 0;
        for (const gtc::native::GpuBatch& batch : rest.batches) restCount += batch.vertices.size();
        if (posedCount != restCount) {
            std::cerr << "pose check: " << posedCount << " posed vertices against " << restCount
                      << " rest vertices\n";
            return 1;
        }
        double worst = 0.0, worstNormal = 0.0;
        std::size_t posedIndex = 0;
        for (const gtc::native::GpuBatch& batch : rest.batches) {
            const std::size_t bone = static_cast<std::size_t>(static_cast<int>(batch.objectId) - base);
            const std::array<double, 16>& m = matrices[bone];
            for (const gtc::native::GpuVertex& vertex : batch.vertices) {
                // Find the posed vertex this rest vertex corresponds to. Both are
                // emitted bone by bone, in the same order, so the sequence is the
                // same; the index only has to walk.
                const gtc::native::GpuVertex* expected = nullptr;
                std::size_t seen = 0;
                bool found = false;
                for (const gtc::native::GpuBatch& outer : posed.batches) {
                    if (seen + outer.vertices.size() > posedIndex) {
                        expected = &outer.vertices[posedIndex - seen];
                        found = true;
                        break;
                    }
                    seen += outer.vertices.size();
                }
                if (!found) { std::cerr << "pose check: posed geometry ran out\n"; return 1; }
                const double x = m[0] * vertex.x + m[4] * vertex.y + m[8] * vertex.z + m[12];
                const double y = m[1] * vertex.x + m[5] * vertex.y + m[9] * vertex.z + m[13];
                const double z = m[2] * vertex.x + m[6] * vertex.y + m[10] * vertex.z + m[14];
                worst = std::max(worst, std::abs(x - expected->x));
                worst = std::max(worst, std::abs(y - expected->y));
                worst = std::max(worst, std::abs(z - expected->z));
                const double nx = m[0] * vertex.nx + m[4] * vertex.ny + m[8] * vertex.nz;
                const double ny = m[1] * vertex.nx + m[5] * vertex.ny + m[9] * vertex.nz;
                const double nz = m[2] * vertex.nx + m[6] * vertex.ny + m[10] * vertex.nz;
                worstNormal = std::max(worstNormal, std::abs(nx - expected->nx));
                worstNormal = std::max(worstNormal, std::abs(ny - expected->ny));
                worstNormal = std::max(worstNormal, std::abs(nz - expected->nz));
                ++posedIndex;
            }
        }
        // A millimetre on a 1.7 m body is far tighter than anything the eye can
        // see, and loose enough for float rounding on the way through.
        if (worst > 0.001 || worstNormal > 0.001) {
            std::cerr << "pose check: rest geometry does not match the posed geometry (worst "
                      << worst * 1000.0 << " mm, normal " << worstNormal << ")\n";
            return 1;
        }
        std::cout << "pose matrices: PASS (" << restCount << " vertices, worst " << worst * 1000.0
                  << " mm)\n";
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
