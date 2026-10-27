#include "client/client.h"
#include "client/renderer.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int integer(const std::string& value, int fallback) {
    try { return std::stoi(value); } catch (...) { return fallback; }
}

bool receivePair(gtc::native::NativeClient& a, gtc::native::NativeClient& b, std::string& error) {
    return a.receive(error) && b.receive(error);
}

const gtc::native::GpuScene* motionScene = nullptr;

bool captureMotionScene(const gtc::native::GpuScene& scene, gtc::native::NativeFrame& frame) {
    motionScene = &scene;
    frame.width = scene.width;
    frame.height = scene.height;
    const std::size_t pixels = static_cast<std::size_t>(scene.width) * scene.height;
    frame.rgb.assign(pixels * 3, 0);
    frame.alpha.assign(pixels, 0);
    return true;
}

bool motionChecks(gtc::native::WorldRenderer& renderer, std::string& error) {
    using namespace gtc::native;
    ClientState target, current;
    gtc::Character citizen;
    citizen.id = 1;
    current.characters.push_back(citizen);
    citizen.position = {4, 2};
    citizen.yaw = 1;
    target.characters.push_back(citizen);
    interpolateCharacters(target, current, 0.1F);
    const double characterBlend = 1.0 - std::exp(-1.5);
    if (std::abs(current.characters[0].position.x - 4 * characterBlend) > 1e-5 ||
        std::abs(current.characters[0].position.y - 2 * characterBlend) > 1e-5) {
        error = "character interpolation";
        return false;
    }
    ClientWorld world;
    ClientState state;
    state.tick = 1;
    state.characters.clear();
    citizen.id = 900;
    citizen.position = {0, 0};
    citizen.yaw = 0;
    state.characters.push_back(citizen);
    ViewState view;
    view.gpu = true;
    view.width = 64;
    view.height = 64;
    view.yaw = 3.14159265F;
    view.pitch = -0.34F;
    NativeFrame frame;
    setGpuRenderer(&captureMotionScene);
    const auto cleanup = [&] { setGpuRenderer(nullptr); motionScene = nullptr; };
    if (!renderer.render(world, state, citizen.id, view, frame, error) || !motionScene) {
        cleanup();
        return false;
    }
    const std::uint64_t revision = motionScene->revision;
    const auto initialView = motionScene->viewMatrix;
    ++state.tick;
    if (!renderer.render(world, state, citizen.id, view, frame, error) || motionScene->revision != revision) {
        error = "idle tick rebuilt the scene";
        cleanup();
        return false;
    }
    state.characters[0].position = {1, 2};
    state.characters[0].yaw = 0.5F;
    if (!renderer.render(world, state, citizen.id, view, frame, error) || !motionScene) {
        cleanup();
        return false;
    }
    if (motionScene->revision != revision || motionScene->viewMatrix == initialView) {
        error = motionScene->revision != revision ? "unexpected motion rebuild" : "camera did not follow";
        cleanup();
        return false;
    }
    const gtc::native::GpuBatch* body = nullptr;
    for (const auto& batch : motionScene->batches) if (batch.objectId == citizen.id) {
        body = &batch;
        break;
    }
    if (!body) {
        error = "missing character batch";
        cleanup();
        return false;
    }
    const double c = std::cos(0.5), s = std::sin(0.5);
    const bool matrix = std::abs(body->modelMatrix[0] - c) < 1e-9 &&
        std::abs(body->modelMatrix[2] + s) < 1e-9 && std::abs(body->modelMatrix[8] - s) < 1e-9 &&
        std::abs(body->modelMatrix[12] - 1) < 1e-9 && std::abs(body->modelMatrix[14] - 2) < 1e-9;
    if (!matrix) {
        error = "character model transform";
        cleanup();
        return false;
    }
    const std::uint64_t idleRevision = motionScene->revision;
    state.characters[0].speed = 1;
    ++state.tick;
    if (!renderer.render(world, state, citizen.id, view, frame, error) || motionScene->revision == idleRevision) {
        error = "walk pose did not rebuild";
        cleanup();
        return false;
    }
    view.mode = ViewMode::FirstPerson;
    view.pitch = 0;
    view.yaw = state.characters[0].yaw;
    if (!renderer.render(world, state, citizen.id, view, frame, error)) {
        cleanup();
        return false;
    }
    const Camera eye = renderer.viewCamera(world, state, citizen.id, view);
    const auto& m = motionScene->viewMatrix;
    const auto transform = [&](Vec3 point) {
        return Vec3{m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
                    m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
                    m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]};
    };
    const Vec3 eyeView = transform(eye.position);
    const Vec3 targetView = transform(eye.target);
    HumanModel eyeModel;
    if (!eyeModel.loadRuntime(".", true, error)) {
        cleanup();
        return false;
    }
    HumanRenderCache eyeCache;
    prepareHuman(eyeModel, state.characters[0], eyeCache);
    const Vec3 attachedEye = humanEyePosition(eyeModel, eyeCache.rest, state.characters[0], state.tick);
    bool torsoRetained = false;
    for (const auto& batch : motionScene->batches) if (batch.objectId == citizen.id) {
        for (const auto& vertex : batch.vertices) {
            if (vertex.y > 1.10 && vertex.y < 1.40) torsoRetained = true;
        }
    }
    const auto expectedEye = [&](const gtc::Character& character, double headYaw = 0, double headPitch = 0) {
        HumanRenderCache cache;
        prepareHuman(eyeModel, character, cache);
        return humanEyePosition(eyeModel, cache.rest, character, state.tick, headYaw, headPitch);
    };
    state.characters[0].onToilet = true;
    state.characters[0].toiletAction = gtc::ToiletAction::Shit;
    ++state.tick;
    if (!renderer.render(world, state, citizen.id, view, frame, error)) {
        cleanup();
        return false;
    }
    const Camera seatedEye = renderer.viewCamera(world, state, citizen.id, view);
    const Vec3 expectedSeatedEye = expectedEye(state.characters[0]);
    const bool seated = length(seatedEye.position - expectedSeatedEye) < 1e-6 &&
        length(transform(seatedEye.position)) < 1e-8 && seatedEye.position.y < eye.position.y - 0.01;
    ViewState neckView = view;
    neckView.yaw = state.characters[0].yaw + 3.0F;
    const Camera limited = renderer.viewCamera(world, state, citizen.id, neckView);
    const Vec3 limitedForward = normalized(limited.target - limited.position);
    const double limitedYaw = std::atan2(limitedForward.x, limitedForward.z);
    const double bodyYaw = state.characters[0].yaw;
    const double neckAngle = std::abs(std::atan2(std::sin(limitedYaw - bodyYaw), std::cos(limitedYaw - bodyYaw)));
    constexpr double neckLimit = 100.0 * 3.141592653589793 / 180.0;
    const Vec3 expectedNeckEye = expectedEye(state.characters[0], neckLimit, 0);
    const bool neck = std::abs(neckAngle - neckLimit) < 1e-4 &&
        length(limited.position - expectedNeckEye) < 1e-6 &&
        length(expectedNeckEye - expectedSeatedEye) > 0.01;
    ViewState pitchView = view;
    pitchView.pitch = -1.5F;
    const Camera pitched = renderer.viewCamera(world, state, citizen.id, pitchView);
    constexpr double pitchDown = -1.05;
    const Vec3 expectedPitchEye = expectedEye(state.characters[0], 0, pitchDown);
    const Vec3 pitchForward = normalized(pitched.target - pitched.position);
    const bool pitch = length(pitched.position - expectedPitchEye) < 1e-6 &&
        length(expectedPitchEye - expectedSeatedEye) > 0.01 && std::abs(pitchForward.y - std::sin(pitchDown)) < 1e-6;
    const int headHeight = eyeModel.specIndex("headHeight");
    bool shaped = headHeight >= 0;
    double shortY = 0, tallY = 0;
    for (int value : {-100, 100}) {
        if (!shaped) break;
        ClientState shapeState = state;
        gtc::Character& character = shapeState.characters[0];
        character.appearance.morphs[headHeight] = value;
        const Camera seatedShape = renderer.viewCamera(world, shapeState, citizen.id, view);
        shaped = length(seatedShape.position - expectedEye(character)) < 1e-6;
        character.onToilet = false;
        character.toiletAction = gtc::ToiletAction::None;
        const Camera standingShape = renderer.viewCamera(world, shapeState, citizen.id, view);
        shaped = shaped && length(standingShape.position - expectedEye(character)) < 1e-6;
        if (value < 0) shortY = standingShape.position.y;
        else tallY = standingShape.position.y;
    }
    shaped = shaped && std::abs(tallY - shortY) > 0.001;
    ViewState mouse;
    mouse.mode = ViewMode::FirstPerson;
    mouse.pitch = 0;
    rotateView(mouse, 10, -20);
    const bool eyeMatrix = length(eyeView) < 1e-8 && std::abs(targetView.z - 1) < 1e-8;
    const bool attached = length(eye.position - attachedEye) < 1e-6 && torsoRetained;
    const bool look = std::abs(mouse.yaw + 0.022F) < 1e-6F && std::abs(mouse.pitch - 0.044F) < 1e-6F;
    cleanup();
    if (!eyeMatrix) error = "first-person eye matrix";
    else if (!attached) error = "first-person body attachment";
    else if (!seated) error = "first-person sit-down head attachment";
    else if (!neck) error = "first-person human neck limit";
    else if (!pitch) error = "first-person neck pitch pivot";
    else if (!shaped) error = "first-person morphed head attachment";
    else if (!look) error = "first-person look input";
    return eyeMatrix && attached && seated && neck && pitch && shaped && look;
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", output = "build/native-smoke", root = ".";
    int port = 1453;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--host" && i + 1 < argc) host = argv[++i];
        else if (argument == "--port" && i + 1 < argc) port = integer(argv[++i], 1453);
        else if (argument == "--output" && i + 1 < argc) output = argv[++i];
        else if (argument == "--root" && i + 1 < argc) root = argv[++i];
        else {
            std::cerr << "Usage: gtc-native-smoke [--host 127.0.0.1] [--port 1453] [--output DIR]\n";
            return 2;
        }
    }
    std::string error;
    gtc::native::NativeClient alpha, beta;
    if (!alpha.connect(host, static_cast<std::uint16_t>(port), error) ||
        !beta.connect(host, static_cast<std::uint16_t>(port), error)) {
        std::cerr << error << '\n';
        return 1;
    }
    gtc::Appearance aAppearance;
    aAppearance.male = true;
    aAppearance.skin = 1;
    aAppearance.hair = 2;
    aAppearance.hairColor = 1;
    aAppearance.shirt = 1;
    aAppearance.coat = true;
    aAppearance.suit = true;
    aAppearance.tie = 2;
    aAppearance.morphs[0] = 35;
    aAppearance.morphs[3] = 8;
    gtc::Appearance bAppearance;
    bAppearance.male = false;
    bAppearance.skin = 3;
    bAppearance.hair = 5;
    bAppearance.hairColor = 3;
    bAppearance.shirt = 2;
    bAppearance.pants = 1;
    bAppearance.dress = true;
    bAppearance.morphs[0] = 28;
    bAppearance.morphs[7] = 12;
    if (!alpha.sendJoin("Native Alpha") || !beta.sendJoin("Native Beta") ||
        !alpha.sendCustomize(aAppearance) || !beta.sendCustomize(bAppearance) ||
        !alpha.sendCheat("bowel", 100) || !beta.sendCheat("bladder", 100) ||
        !alpha.sendPath(alpha.world().toiletFront) || !beta.sendInput({0, 1}, false, false, 3.14159265F, 0)) {
        std::cerr << "Could not send native smoke commands\n";
        return 1;
    }
    bool replicated = false, customized = false, inputMoved = false, pathSeen = false;
    bool backwardReplicated = false;
    bool toiletRequested = false, toiletSeen = false;
    int iterations = 0;
    for (; iterations < 100; ++iterations) {
        if (!receivePair(alpha, beta, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        if (iterations == 5) {
            if (!beta.sendInput({}, false, false, 0, 0) || !beta.sendPath({3.0, 2.3})) return 1;
        }
        const gtc::Character* a = alpha.state().character(alpha.playerId());
        const gtc::Character* b = beta.state().character(beta.playerId());
        const gtc::Character* aInB = beta.state().character(alpha.playerId());
        const gtc::Character* bInA = alpha.state().character(beta.playerId());
        replicated = a && b && aInB && bInA && aInB->name == "Native Alpha" && bInA->name == "Native Beta";
        customized = a && b && aInB && bInA && a->appearance.morphs[0] == 35 && a->appearance.morphs[3] == 8 &&
                     !b->appearance.male && b->appearance.hair == 5 && b->appearance.morphs[7] == 12 &&
                     aInB->appearance.coat && aInB->appearance.suit && aInB->appearance.tie == 2 &&
                     bInA->appearance.dress && bInA->appearance.morphs[7] == 12;
        inputMoved = inputMoved || (b && b->speed > 0.1F);
        backwardReplicated = backwardReplicated || (b && b->speed > 0.1F && b->manualMove.y > 0.9F &&
            std::abs(b->yaw - 3.14159265F) < 0.001F);
        pathSeen = pathSeen || (a && !a->path.empty()) || (b && !b->path.empty());
        if (a && !toiletRequested && gtc::distance(a->position, alpha.world().toiletFront) <= 1.55F) {
            if (!alpha.sendToilet("shit")) return 1;
            toiletRequested = true;
        }
        const gtc::Character* aOther = beta.state().character(alpha.playerId());
        toiletSeen = toiletSeen || (a && aOther && a->toiletAction == gtc::ToiletAction::Shit &&
            aOther->toiletAction == gtc::ToiletAction::Shit && a->onToilet &&
            alpha.state().toiletUser == alpha.playerId() && beta.state().toiletUser == alpha.playerId());
        if (replicated && customized && inputMoved && backwardReplicated && pathSeen && toiletSeen) break;
    }
    const bool passed = replicated && customized && inputMoved && backwardReplicated && pathSeen && toiletSeen;
    bool motion = false;
    std::error_code code;
    std::filesystem::create_directories(output, code);
    if (passed) {
        gtc::native::WorldRenderer renderer;
        if (!renderer.load(root, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        motion = motionChecks(renderer, error);
        if (!motion) {
            std::cerr << "Native motion checks failed: " << error << '\n';
            return 1;
        }
        struct Capture { const char* name; gtc::native::NativeClient* client; gtc::native::ViewState view; };
        gtc::native::ViewState tps;
        tps.yaw = 0;
        gtc::native::ViewState fps;
        fps.mode = gtc::native::ViewMode::FirstPerson;
        fps.pitch = -1.20;
        gtc::native::ViewState fpsLevel;
        fpsLevel.mode = gtc::native::ViewMode::FirstPerson;
        gtc::native::ViewState god;
        god.mode = gtc::native::ViewMode::God;
        const Capture captures[] = {{"alpha-tps", &alpha, tps}, {"beta-tps", &beta, tps},
                                    {"alpha-fps", &alpha, fps}, {"alpha-fps-level", &alpha, fpsLevel},
                                    {"beta-fps-level", &beta, fpsLevel}, {"god", &alpha, god}};
        for (const Capture& capture : captures) {
            gtc::native::NativeFrame frame;
            if (!renderer.render(capture.client->world(), capture.client->state(), capture.client->playerId(),
                                 capture.view, frame, error) || !frame.save(output + "/" + capture.name + ".png")) {
                std::cerr << "Native capture failed: " << error << '\n';
                return 1;
            }
        }
    }
    std::ofstream report(output + "/report.json");
    report << "{\"passed\":" << (passed ? "true" : "false") << ",\"alpha\":" << alpha.playerId()
           << ",\"beta\":" << beta.playerId() << ",\"tick\":" << alpha.state().tick
           << ",\"replicated\":" << (replicated ? "true" : "false")
           << ",\"customized\":" << (customized ? "true" : "false")
           << ",\"inputMoved\":" << (inputMoved ? "true" : "false")
           << ",\"pathSeen\":" << (pathSeen ? "true" : "false")
           << ",\"toiletSeen\":" << (toiletSeen ? "true" : "false")
           << ",\"motion\":" << (motion ? "true" : "false")
           << ",\"iterations\":" << iterations << "}\n";
    std::cout << "native smoke: " << (passed ? "PASS" : "FAIL") << ", alpha=" << alpha.playerId()
              << ", beta=" << beta.playerId() << ", tick=" << alpha.state().tick << '\n';
    if (!passed) std::cerr << "replication=" << replicated << " customize=" << customized
                           << " input=" << inputMoved << " path=" << pathSeen << " toilet=" << toiletSeen << '\n';
    return passed ? 0 : 1;
}
