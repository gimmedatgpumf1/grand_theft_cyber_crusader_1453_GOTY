#include "client/renderer.h"

#include "client/human_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace gtc::native {
namespace {

Vec3 unobstructedCamera(const ClientWorld& world, Vec3 target, Vec3 desired) {
    const Vec3 direction = desired - target;
    const double length = gtc::native::length(direction);
    if (length < 0.01) return desired;
    double nearest = 1.0;
    auto test = [&](Vec3 center, Vec3 half) {
        double enter = 0.0, exit = 1.0;
        const double origins[3] = {target.x, target.y, target.z};
        const double deltas[3] = {direction.x, direction.y, direction.z};
        const double centers[3] = {center.x, center.y, center.z};
        const double halves[3] = {half.x, half.y, half.z};
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(deltas[axis]) < 1e-9) {
                if (std::abs(origins[axis] - centers[axis]) > halves[axis]) return;
                continue;
            }
            double a = (centers[axis] - halves[axis] - origins[axis]) / deltas[axis];
            double b = (centers[axis] + halves[axis] - origins[axis]) / deltas[axis];
            if (a > b) std::swap(a, b);
            enter = std::max(enter, a);
            exit = std::min(exit, b);
            if (enter > exit) return;
        }
        if (enter > 0.02 && enter < nearest) nearest = enter;
    };
    for (const Obstacle& obstacle : world.obstacles) {
        if (obstacle.kind == "toilet") continue;
        test({obstacle.center.x, obstacle.height * 0.5, obstacle.center.y},
             {obstacle.halfSize.x, obstacle.height * 0.5, obstacle.halfSize.y});
    }
    test({world.toilet.x, 0.80, world.toilet.y - 0.52}, {0.55, 0.80, 0.21});
    const double adjusted = std::max(0.12, nearest - 0.15 / length);
    return target + direction * adjusted;
}

Camera makeCamera(const ClientWorld& world, const ClientState& state, std::uint32_t localId, const ViewState& view) {
    Camera camera;
    camera.width = std::max(1, view.width);
    camera.height = std::max(1, view.height);
    camera.fovDegrees = 62;
    const Character* local = state.character(localId);
    const Vec3 citizen = local ? Vec3{local->position.x, 0, local->position.y} : Vec3{};
    const float yaw = local ? view.yaw : 0.0F;
    if (view.mode == ViewMode::God) {
        camera.position = {view.godX + std::sin(view.godYaw) * view.godDistance * 0.72,
                           view.godDistance * 0.82,
                           view.godZ + std::cos(view.godYaw) * view.godDistance * 0.72};
        camera.target = {view.godX, 0.0, view.godZ};
        return camera;
    }
    const double cp = std::cos(view.pitch), sp = std::sin(view.pitch);
    const Vec3 forward{std::sin(yaw) * cp, sp, std::cos(yaw) * cp};
    if (view.mode == ViewMode::FirstPerson) {
        const bool seated = local && local->onToilet && !(local->appearance.male && local->toiletAction == ToiletAction::Piss);
        camera.position = citizen + Vec3{0, seated ? 1.43 : 1.65, 0} +
                          Vec3{std::sin(yaw) * 0.17, 0, std::cos(yaw) * 0.17};
        camera.target = camera.position + forward;
    } else {
        const Vec3 right{-std::cos(yaw), 0, std::sin(yaw)};
        camera.target = citizen + Vec3{0, 1.25, 0} + right * 0.42;
        camera.position = camera.target - forward * view.distance;
        camera.position.x = std::clamp(camera.position.x, -world.width * 0.5 + 0.35, world.width * 0.5 - 0.35);
        camera.position.z = std::clamp(camera.position.z, -world.depth * 0.5 + 0.35, world.depth * 0.5 - 0.35);
        const Vec3 direct = unobstructedCamera(world, camera.target, camera.position);
        camera.position = direct;
        if (length(direct - camera.target) < 1.8) {
            Vec3 lifted = camera.target - forward * view.distance + Vec3{0, 1.4, 0};
            lifted.x = std::clamp(lifted.x, -world.width * 0.5 + 0.35, world.width * 0.5 - 0.35);
            lifted.z = std::clamp(lifted.z, -world.depth * 0.5 + 0.35, world.depth * 0.5 - 0.35);
            lifted = unobstructedCamera(world, camera.target, lifted);
            if (length(lifted - camera.target) > length(direct - camera.target)) camera.position = lifted;
        }
    }
    return camera;
}

const Texture& floorTexture() {
    static const Texture texture = [] {
        Texture result;
        result.width = result.height = 512;
        result.pixels.resize(512U * 512U * 4U, 255);
        for (int y = 0; y < 512; ++y) for (int x = 0; x < 512; ++x) {
            const int tx = x / 64, ty = y / 64;
            const bool pale = ((tx + ty) & 1) == 0;
            unsigned char rgb[3] = {0, 0, 0};
            const unsigned char paleColor[3] = {131, 145, 122};
            const unsigned char darkColor[3] = {49, 71, 62};
            for (int channel = 0; channel < 3; ++channel) rgb[channel] = pale ? paleColor[channel] : darkColor[channel];
            double stain = 1.0;
            for (int spot = 0; spot < 6; ++spot) {
                const unsigned hash = static_cast<unsigned>(tx * 73 + ty * 151 + spot * 997);
                const double cx = tx * 64 + 8 + (hash * 37U % 48U);
                const double cy = ty * 64 + 8 + (hash * 53U % 48U);
                const double radius = 2 + (hash * 29U % 8U);
                const double dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy < radius * radius) stain = 0.82;
            }
            const bool grout = x % 64 < 2 || y % 64 < 2;
            const std::size_t offset = (static_cast<std::size_t>(y) * 512 + x) * 4;
            for (int channel = 0; channel < 3; ++channel) {
                const unsigned char groutColor[3] = {26, 40, 33};
                result.pixels[offset + channel] = grout ? groutColor[channel] :
                    static_cast<unsigned char>(rgb[channel] * stain);
            }
        }
        return result;
    }();
    return texture;
}

void drawFloor(Rasterizer& rasterizer, const Camera& camera, const ClientWorld& world) {
    Material floor;
    floor.kind = Material::Kind::Floor;
    floor.color = colorHex("#83917a");
    floor.second = colorHex("#31473e");
    floor.texture = &floorTexture();
    floor.specular = 0.0;
    floor.constantLighting = true;
    const double x = world.width * 0.5, z = world.depth * 0.5;
    const Vec3 a{-x, 0, -z}, b{x, 0, -z}, c{x, 0, z}, d{-x, 0, z};
    const Vec3 normal{0, 1, 0};
    const double u = world.width / 8, v = world.depth / 8;
    rasterizer.triangle({cameraVertex(camera, a, normal, a, {0, 0}), cameraVertex(camera, b, normal, b, {u, 0}),
                         cameraVertex(camera, c, normal, c, {u, v})}, floor);
    rasterizer.triangle({cameraVertex(camera, a, normal, a, {0, 0}), cameraVertex(camera, c, normal, c, {u, v}),
                         cameraVertex(camera, d, normal, d, {0, v})}, floor);
}

void drawToilet(Rasterizer& rasterizer, const Camera& camera, gtc::Vec2 toilet) {
    Material porcelain;
    porcelain.color = colorHex("#d5ddd7");
    porcelain.specular = 0.30;
    Material dark;
    dark.color = colorHex("#263f42");
    dark.specular = 0.42;
    const Vec3 center{toilet.x, 0, toilet.y};
    drawBox(rasterizer, camera, center + Vec3{0, 0.20, -0.10}, Vec3{0.58, 0.40, 0.55}, porcelain);
    drawEllipsoid(rasterizer, camera, center + Vec3{0, 0.48, 0.03}, Vec3{0.49, 0.34, 0.62}, porcelain, 12, 6);
    drawEllipsoid(rasterizer, camera, center + Vec3{0, 0.72, 0.05}, Vec3{0.53, 0.065, 0.64}, porcelain, 16, 4);
    drawEllipsoid(rasterizer, camera, center + Vec3{0, 0.765, 0.08}, Vec3{0.32, 0.018, 0.40}, dark, 12, 4);
    drawBox(rasterizer, camera, center + Vec3{0, 1.02, -0.52}, Vec3{1.02, 0.92, 0.34}, porcelain);
    drawBox(rasterizer, camera, center + Vec3{0, 1.51, -0.52}, Vec3{1.08, 0.08, 0.39}, porcelain);
    Material flush;
    flush.color = colorHex("#9ca9a4");
    flush.specular = 0.5;
    drawEllipsoid(rasterizer, camera, center + Vec3{0.34, 1.55, -0.50}, Vec3{0.07, 0.025, 0.05}, flush, 8, 4);
}

void drawObstacles(Rasterizer& rasterizer, const Camera& camera, const ClientWorld& world) {
    for (const Obstacle& obstacle : world.obstacles) {
        const Vec3 center{obstacle.center.x, obstacle.height * 0.5, obstacle.center.y};
        const Vec3 size{obstacle.halfSize.x * 2, obstacle.height, obstacle.halfSize.y * 2};
        if (obstacle.kind == "toilet") {
            drawToilet(rasterizer, camera, obstacle.center);
            continue;
        }
        Material material;
        if (obstacle.kind == "boundary") material.color = colorHex("#58625d");
        else if (obstacle.kind == "sink") material.color = colorHex("#82928c");
        else material.color = colorHex("#71583b");
        material.specular = obstacle.kind == "sink" ? 0.24 : 0.04;
        drawBox(rasterizer, camera, center, size, material);
        if (obstacle.kind == "sink") {
            Material basin;
            basin.color = colorHex("#b7c5bf");
            basin.specular = 0.32;
            drawEllipsoid(rasterizer, camera, Vec3{center.x, obstacle.height + 0.05, center.z},
                          Vec3{0.65, 0.12, 0.40}, basin, 10, 5);
            drawBox(rasterizer, camera, Vec3{center.x, obstacle.height + 0.28, center.z - 0.28},
                    Vec3{0.08, 0.45, 0.08}, basin);
        }
    }
}

void drawPaths(Rasterizer& rasterizer, const Camera& camera, const ClientState& state, std::uint32_t localId) {
    const Character* local = state.character(localId);
    if (!local || local->path.empty()) return;
    Material marker;
    marker.color = colorHex("#8ce6c8");
    marker.specular = 0.2;
    gtc::Vec2 previous = local->position;
    for (gtc::Vec2 point : local->path) {
        const double length = distance(previous, point);
        const int steps = std::max(1, static_cast<int>(length / 0.28));
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const gtc::Vec2 at{static_cast<float>(previous.x + (point.x - previous.x) * t),
                               static_cast<float>(previous.y + (point.y - previous.y) * t)};
            drawEllipsoid(rasterizer, camera, Vec3{at.x, 0.025, at.y}, Vec3{0.045, 0.012, 0.045}, marker, 8, 4);
        }
        previous = point;
    }
}

void drawPuddles(Rasterizer& rasterizer, const Camera& camera, const ClientState& state) {
    Material puddle;
    puddle.kind = Material::Kind::Puddle;
    puddle.color = colorHex("#527f72");
    puddle.specular = 0.65;
    for (const Puddle& puddleValue : state.puddles) {
        drawEllipsoid(rasterizer, camera, Vec3{puddleValue.position.x, 0.018, puddleValue.position.y},
                      Vec3{puddleValue.size, 0.012, puddleValue.size * 0.72}, puddle, 18, 5);
    }
}

void drawPissing(Rasterizer& rasterizer, const Camera& camera, const Character& character) {
    if (!character.pissing) return;
    Material stream;
    stream.color = colorHex("#b9c862");
    stream.specular = 0.4;
    const Vec3 forward{std::sin(character.yaw), 0, std::cos(character.yaw)};
    const Vec3 origin{character.position.x, 0.77, character.position.y};
    for (int i = 0; i < 12; ++i) {
        const double t = i / 11.0;
        const Vec3 at = origin + forward * (0.18 + t * 0.68) + Vec3{0, -0.70 * t * t, 0};
        drawEllipsoid(rasterizer, camera, at, Vec3{0.018, 0.035, 0.018}, stream, 6, 4);
    }
}

void drawHud(NativeFrame& frame, const ClientWorld& world, const ClientState& state,
             std::uint32_t localId, const ViewState& view) {
    const Character* local = state.character(localId);
    const Color panel = colorHex("#111815");
    const Color text = colorHex("#d7ff54");
    const Color muted = colorHex("#b4c5ba");
    frame.rectangle(0, 0, 480, 60, panel);
    drawText(frame, 8, 7, "GTC 1453", text);
    const char* mode = view.mode == ViewMode::FirstPerson ? "FPS" : view.mode == ViewMode::God ? "GOD" : "TPS";
    drawText(frame, 8, 34, std::string(mode) + " TICK " + std::to_string(state.tick), muted);
    if (view.fps > 0) drawText(frame, 300, 34, std::string(view.gpu ? "GPU " : "CPU ") +
        std::to_string(static_cast<int>(view.fps + 0.5)) + " FPS", muted);
    if (local) {
        drawText(frame, 285, 7, local->name.substr(0, 10), text);
        const double range = distance(local->position, world.toiletFront);
        if (local->toiletAction != ToiletAction::None || range <= world.interactionRange) {
            frame.rectangle(0, 478, 480, 34, panel);
            drawText(frame, 8, 484, local->toiletAction != ToiletAction::None ? "E STAND" : "E TOILET", text);
        }
        if (!local->notice.empty()) {
            frame.rectangle(0, 518, 480, 34, panel);
            drawText(frame, 8, 524, local->notice.substr(0, 25), muted);
        } else if (!state.events.empty()) {
            frame.rectangle(0, 518, 480, 34, panel);
            drawText(frame, 8, 524, state.events.back().text.substr(0, 25), muted);
        }
        frame.rectangle(0, 562, 480, 78, panel);
        drawText(frame, 8, 571, "BLADDER " + std::to_string(static_cast<int>(local->needs.bladder)), text);
        drawText(frame, 250, 571, "BOWEL " + std::to_string(static_cast<int>(local->needs.bowel)), text);
        frame.rectangle(8, 607, 210, 14, colorHex("#29332d"));
        frame.rectangle(250, 607, 210, 14, colorHex("#29332d"));
        frame.rectangle(8, 607, static_cast<int>(210 * std::clamp(local->needs.bladder / 100.0F, 0.0F, 1.0F)), 14,
                        colorHex("#6edbd2"));
        frame.rectangle(250, 607, static_cast<int>(210 * std::clamp(local->needs.bowel / 100.0F, 0.0F, 1.0F)), 14,
                        colorHex("#ffcc62"));
    } else {
        drawText(frame, 8, 570, "WAITING FOR CITIZEN", text);
    }
}

} // namespace

bool WorldRenderer::load(const std::string& root, std::string& error) {
    root_ = root;
    if (!male_.loadRuntime(root, true, error) || !female_.loadRuntime(root, false, error)) return false;
    if (!maleHair_.load(root, male_, error) || !femaleHair_.load(root, female_, error)) return false;
    humanCache_.clear();
    ready_ = true;
    return true;
}

const std::vector<MorphSpec>& WorldRenderer::morphSpecs(bool male) const {
    return male ? male_.specs : female_.specs;
}

Camera makeViewCamera(const ClientWorld& world, const ClientState& state, std::uint32_t localId,
                      const ViewState& view) {
    return makeCamera(world, state, localId, view);
}

bool WorldRenderer::render(const ClientWorld& world, const ClientState& state, std::uint32_t localId,
                           const ViewState& view, NativeFrame& frame, std::string& error) const {
    if (!ready_) {
        error = "Native world renderer is not loaded";
        return false;
    }
    const auto profileStart = std::chrono::steady_clock::now();
    const Camera camera = makeCamera(world, state, localId, view);
    Rasterizer rasterizer(std::max(1, view.width), std::max(1, view.height), colorHex("#111913"), camera.focal(), view.gpu);
    drawFloor(rasterizer, camera, world);
    if (view.highlightToilet) {
        Material highlight;
        highlight.color = colorHex("#86b83e");
        drawEllipsoid(rasterizer, camera, Vec3{world.toilet.x, 0.025, world.toilet.y},
                      Vec3{0.82, 0.018, 0.94}, highlight, 24, 5);
    }
    drawObstacles(rasterizer, camera, world);
    drawPaths(rasterizer, camera, state, localId);
    drawPuddles(rasterizer, camera, state);
    const auto humanStart = std::chrono::steady_clock::now();
    for (const Character& character : state.characters) {
        const HumanModel& model = character.appearance.male ? male_ : female_;
        const HairLibrary& hair = character.appearance.male ? maleHair_ : femaleHair_;
        const bool hideHead = view.mode == ViewMode::FirstPerson && character.id == localId;
        drawHuman(rasterizer, camera, model, hair, character, humanCache_[character.id], root_, state.tick, hideHead);
        drawPissing(rasterizer, camera, character);
    }
    const auto finishStart = std::chrono::steady_clock::now();
    frame = rasterizer.finish();
    if (view.gpu && frame.rgb.size() != static_cast<std::size_t>(frame.width) * frame.height * 3) {
        error = "Native GPU rendering or readback failed";
        return false;
    }
    if (std::getenv("GTC_PROFILE")) {
        const auto end = std::chrono::steady_clock::now();
        const auto milliseconds = [](auto start, auto stop) {
            return std::chrono::duration<double, std::milli>(stop - start).count();
        };
        std::cerr << "profile world=" << milliseconds(profileStart, humanStart)
                  << " humans=" << milliseconds(humanStart, finishStart)
                  << " finish=" << milliseconds(finishStart, end) << " ms\n";
    }
    frame.logicalUi();
    drawHud(frame, world, state, localId, view);
    return true;
}

} // namespace gtc::native
