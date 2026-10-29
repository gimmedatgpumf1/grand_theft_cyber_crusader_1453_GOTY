#include "client/benchmark.h"
#include "client/client.h"
#include "client/diagnostics.h"
#include "client/cheat_menu.h"
#include "client/crosshair.h"
#include "client/fps_counter.h"
#include "client/mode_bar.h"
#include "client/overlay_panel.h"
#include "client/interactive.h"
#include "client/renderer.h"
#include "client/skeleton_body.h"
#include "client/skeleton_room.h"
#include "client/threadpool.h"
#include "client/window.h"
#include "client/opengl_shared.h"
#include "vulkan/vk_renderer.h"

#include <algorithm>
#include <cmath>
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
    // The game's frame, not a different one: the street asks for the picture in
    // the order the window takes it, so the benchmark measures the same path.
    frame.wantPixels = true;
    frame.wantsBgra = true;
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
    std::string output = "build/native-client/frame.png", mode = "tps", toilet;
    bool cheats = false;
    std::string overlay;
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
        // A capture of the need panel, drawn by the same call the F1 key makes the
        // window loop draw. Without a way to photograph it, the only proof of the
        // panel would be a claim about it.
        else if (argument == "--cheats") cheats = true;
        else if (argument == "--overlay" && i + 1 < argc) overlay = argv[++i];
        else if (argument == "--yaw" && i + 1 < argc) view.yaw = static_cast<float>(number(argv[++i]));
        else if (argument == "--pitch" && i + 1 < argc) view.pitch = static_cast<float>(number(argv[++i]));
        else {
            std::cerr << "Usage: gtc-native-client [--headless] [--host HOST] [--port 1453] [--name NAME] [--frames N]\n"
                         "  [--mode tps|fps|god] [--path X Z] [--move X Z] [--toilet sit|shit|piss]\n"
                         "  [--width PIXELS] [--height PIXELS] [--benchmark N]\n"
                         "  [--yaw RADIANS] [--pitch RADIANS] [--output PNG] [--cheats]\n"
                         "  [--overlay toilet|manual] (draw an overlay into the capture)\n";
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
                  << " (" << (milliseconds > 0 ? 1000.0 / milliseconds : 0.0) << " fps, "
                  << room.triangles() << " triangles)\n";
        gtc::native::setGpuRenderer(nullptr);
        if (rendererName == "vulkan") {
            if (!benchFrame.save(output)) std::cerr << "could not save " << output << '\n';
        }
        gtc::native::shutdownVulkan();
        return 0;
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
        // The rest scene is indexed: one vertex per point, and a triangle is
        // three indices into it. The posed scene is the same triangles unrolled,
        // a vertex per corner. Expanding the indices has to reproduce the posed
        // vertex sequence exactly, or the two backends draw different geometry.
        std::vector<gtc::native::GpuVertex> posedVertices;
        for (const gtc::native::GpuBatch& batch : posed.batches) {
            posedVertices.insert(posedVertices.end(), batch.vertices.begin(), batch.vertices.end());
        }
        const std::vector<std::array<double, 16>> matrices = model.boneMatrices({-0.52, 0, 0}, 3.14159265);
        std::vector<gtc::native::GpuVertex> restVertices;
        std::size_t indexed = 0;
        for (const gtc::native::GpuBatch& batch : rest.batches) {
            const std::size_t bone = static_cast<std::size_t>(static_cast<int>(batch.objectId) - base);
            if (bone >= matrices.size()) {
                std::cerr << "pose check: batch " << batch.objectId << " names no bone\n";
                return 1;
            }
            const std::array<double, 16>& m = matrices[bone];
            const auto emit = [&](std::uint32_t index) {
                const gtc::native::GpuVertex& vertex = batch.vertices[index];
                gtc::native::GpuVertex posedVertex;
                posedVertex.x = static_cast<float>(m[0] * vertex.x + m[4] * vertex.y + m[8] * vertex.z + m[12]);
                posedVertex.y = static_cast<float>(m[1] * vertex.x + m[5] * vertex.y + m[9] * vertex.z + m[13]);
                posedVertex.z = static_cast<float>(m[2] * vertex.x + m[6] * vertex.y + m[10] * vertex.z + m[14]);
                posedVertex.nx = static_cast<float>(m[0] * vertex.nx + m[4] * vertex.ny + m[8] * vertex.nz);
                posedVertex.ny = static_cast<float>(m[1] * vertex.nx + m[5] * vertex.ny + m[9] * vertex.nz);
                posedVertex.nz = static_cast<float>(m[2] * vertex.nx + m[6] * vertex.ny + m[10] * vertex.nz);
                restVertices.push_back(posedVertex);
            };
            if (batch.indices.empty()) {
                for (std::uint32_t index = 0; index < batch.vertices.size(); ++index) emit(index);
                continue;
            }
            if (batch.indices.size() % 3 != 0) {
                std::cerr << "pose check: batch " << batch.objectId << " has " << batch.indices.size()
                          << " indices, which is not whole triangles\n";
                return 1;
            }
            indexed += batch.indices.size() / 3;
            for (std::uint32_t index : batch.indices) {
                if (index >= batch.vertices.size()) {
                    std::cerr << "pose check: index " << index << " is past the end of batch " << batch.objectId
                              << '\n';
                    return 1;
                }
                emit(index);
            }
        }
        if (posedVertices.size() != restVertices.size()) {
            std::cerr << "pose check: " << posedVertices.size() << " posed vertices against "
                      << restVertices.size() << " expanded rest vertices\n";
            return 1;
        }
        double worst = 0.0, worstNormal = 0.0;
        for (std::size_t index = 0; index < posedVertices.size(); ++index) {
            const gtc::native::GpuVertex& a = posedVertices[index];
            const gtc::native::GpuVertex& b = restVertices[index];
            worst = std::max(worst, static_cast<double>(std::abs(a.x - b.x)));
            worst = std::max(worst, static_cast<double>(std::abs(a.y - b.y)));
            worst = std::max(worst, static_cast<double>(std::abs(a.z - b.z)));
            worstNormal = std::max(worstNormal, static_cast<double>(std::abs(a.nx - b.nx)));
            worstNormal = std::max(worstNormal, static_cast<double>(std::abs(a.ny - b.ny)));
            worstNormal = std::max(worstNormal, static_cast<double>(std::abs(a.nz - b.nz)));
        }
        if (worst > 0.001 || worstNormal > 0.01) {
            std::cerr << "pose check: rest geometry is " << worst << " m and " << worstNormal
                      << " off the posed geometry\n";
            return 1;
        }
        std::cout << "  (" << indexed << " indexed triangles, " << posedVertices.size() << " corners)\n";
        std::cout << "pose matrices: PASS (" << posedVertices.size() << " vertices, worst " << worst * 1000.0
                  << " mm, normals " << worstNormal << ")\n";
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
    if (!headless) {
        gtc::native::InteractiveOptions options;
        options.host = host;
        options.port = static_cast<std::uint16_t>(port);
        options.name = name;
        options.root = root;
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
    const auto firstStart = std::chrono::steady_clock::now();
    if (!renderer.render(client.world(), client.state(), client.playerId(), view, frame, error)) {
        std::cerr << "Native render failed: " << error << '\n';
        return 1;
    }
    double measuredMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - firstStart).count();
    const auto benchmarkStart = std::chrono::steady_clock::now();
    for (int iteration = 1; iteration < benchmark; ++iteration) {
        if (!renderer.render(client.world(), client.state(), client.playerId(), view, frame, error)) {
            std::cerr << "Native render failed: " << error << '\n';
            return 1;
        }
    }
    if (benchmark > 1) {
        measuredMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - benchmarkStart).count() / (benchmark - 1);
        std::cout << "native render: " << measuredMilliseconds << " ms/frame at " << view.width << "x" << view.height
                  << '\n';
    }
    // The counter is drawn by the host into the picture, so a capture carries it:
    // this is the same call the window loop makes, over a real frame, with the
    // rate this machine actually rendered at -- which is this machine's number
    // and not the frame rate of the machine the picture is being sent to.
    // The same three things the window loop draws, in the same order: the
    // reticle, the need panel when it is open, and the frame rate.
    gtc::native::drawCrosshair(frame, view);
    if (overlay == "toilet") gtc::native::drawToiletChoice(frame);
    else if (overlay == "manual") gtc::native::drawManual(frame);
    if (cheats) {
        const gtc::Character* citizen = client.state().character(client.playerId());
        const int bladder = citizen ? static_cast<int>(std::lround(citizen->needs.bladder)) : 0;
        const int bowel = citizen ? static_cast<int>(std::lround(citizen->needs.bowel)) : 0;
        gtc::native::drawCheatMenu(frame, bladder, bowel);
    }
    gtc::native::drawModeBar(frame, view.mode);
    gtc::native::drawFpsCounter(frame, measuredMilliseconds > 0.0 ? 1000.0 / measuredMilliseconds : 0.0);
    if (!frame.save(output)) {
        std::cerr << "Native render failed: cannot save " << output << '\n';
        return 1;
    }
    std::cout << "native client: id=" << client.playerId() << " tick=" << client.state().tick
              << " citizens=" << client.state().characters.size() << " frame=" << output << '\n';
    return 0;
}
