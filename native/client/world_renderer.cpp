#include "client/renderer.h"

#include "client/blockbody.h"
#include "client/human_renderer.h"
#include "client/world_hud.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

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
    test({world.toilet.x, 0.5425, world.toilet.y - 0.2445}, {0.2285, 0.20, 0.108});
    const double adjusted = std::max(0.12, nearest - 0.15 / length);
    return target + direction * adjusted;
}

Camera makeExternalCamera(const ClientWorld& world, const ClientState& state, std::uint32_t localId,
                          const ViewState& view) {
    Camera camera;
    camera.width = std::max(1, view.width);
    camera.height = std::max(1, view.height);
    camera.fovDegrees = view.aiming ? 52 : 62;
    const Character* local = state.character(localId);
    const Vec3 citizen = local ? Vec3{local->position.x, local->elevation, local->position.y} : Vec3{};
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
    const Vec3 right{-std::cos(yaw), 0, std::sin(yaw)};
    const double targetHeight = local && local->prone ? 0.35 : local && local->crouched ? 0.82 : 1.15;
    camera.target = citizen + Vec3{0, targetHeight, 0} + right * 0.20;
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

void drawQuad(Rasterizer& rasterizer, const Camera& camera, const std::array<Vec3, 4>& points,
              Vec3 normal, const Material& material) {
    const Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    std::array<RenderVertex, 4> vertices;
    for (int i = 0; i < 4; ++i) vertices[i] = cameraVertex(camera, points[i], normal, points[i], uv[i]);
    rasterizer.triangle({vertices[0], vertices[1], vertices[2]}, material);
    rasterizer.triangle({vertices[0], vertices[2], vertices[3]}, material);
}

void drawRing(Rasterizer& rasterizer, const Camera& camera, Vec3 center, double inner, double outer,
              const Material& material, int segments = 40) {
    constexpr double pi = 3.141592653589793;
    const Vec3 normal{0, 1, 0};
    for (int i = 0; i < segments; ++i) {
        const double a = i * 2 * pi / segments, b = (i + 1) * 2 * pi / segments;
        const std::array<Vec3, 4> points = {
            center + Vec3{std::cos(a) * inner, 0, std::sin(a) * inner},
            center + Vec3{std::cos(a) * outer, 0, std::sin(a) * outer},
            center + Vec3{std::cos(b) * outer, 0, std::sin(b) * outer},
            center + Vec3{std::cos(b) * inner, 0, std::sin(b) * inner}};
        drawQuad(rasterizer, camera, points, normal, material);
    }
}

void drawOctahedron(Rasterizer& rasterizer, const Camera& camera, Vec3 center, double radius,
                    const Material& material) {
    const Vec3 top = center + Vec3{0, radius, 0}, bottom = center - Vec3{0, radius, 0};
    const Vec3 points[4] = {center + Vec3{radius, 0, 0}, center + Vec3{0, 0, radius},
                            center - Vec3{radius, 0, 0}, center - Vec3{0, 0, radius}};
    for (int i = 0; i < 4; ++i) {
        const Vec3 a = points[i], b = points[(i + 1) % 4];
        const Vec3 normalA = normalized(cross(a - top, b - top));
        const Vec3 normalB = normalized(cross(b - bottom, a - bottom));
        rasterizer.triangle({cameraVertex(camera, top, normalA, top), cameraVertex(camera, a, normalA, a),
                             cameraVertex(camera, b, normalA, b)}, material);
        rasterizer.triangle({cameraVertex(camera, bottom, normalB, bottom), cameraVertex(camera, b, normalB, b),
                             cameraVertex(camera, a, normalB, a)}, material);
    }
}

const Texture& signTexture(const std::string& text, const char* foreground, const char* background) {
    const std::string key = text + foreground + background;
    static std::map<std::string, Texture> cache;
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    NativeFrame canvas;
    canvas.width = 512;
    canvas.height = 256;
    canvas.rgb.resize(512U * 256U * 3U);
    canvas.rectangle(0, 0, 512, 256, colorHex(foreground));
    canvas.rectangle(8, 8, 496, 240, colorHex(background));
    const int split = static_cast<int>(text.find('\n'));
    const std::string first = text.substr(0, split);
    const std::string second = split >= 0 ? text.substr(static_cast<std::size_t>(split) + 1) : "";
    drawText(canvas, std::max(18, 256 - static_cast<int>(first.size()) * 12), second.empty() ? 105 : 68,
             first, colorHex(foreground), 5);
    if (!second.empty()) {
        drawText(canvas, std::max(18, 256 - static_cast<int>(second.size()) * 12), 143,
                 second, colorHex(foreground), 5);
    }
    Texture texture;
    texture.width = 512;
    texture.height = 256;
    texture.pixels.resize(512U * 256U * 4U, 255);
    for (std::size_t i = 0; i < 512U * 256U; ++i) {
        texture.pixels[i * 4] = canvas.rgb[i * 3];
        texture.pixels[i * 4 + 1] = canvas.rgb[i * 3 + 1];
        texture.pixels[i * 4 + 2] = canvas.rgb[i * 3 + 2];
    }
    return cache.emplace(key, std::move(texture)).first->second;
}

void drawSign(Rasterizer& rasterizer, const Camera& camera, Vec3 center, double width, double height,
              const Texture& texture) {
    Material material;
    material.kind = Material::Kind::Textured;
    material.color = {1, 1, 1};
    material.texture = &texture;
    material.unlit = true;
    drawQuad(rasterizer, camera, {center + Vec3{-width * 0.5, -height * 0.5, 0},
        center + Vec3{width * 0.5, -height * 0.5, 0}, center + Vec3{width * 0.5, height * 0.5, 0},
        center + Vec3{-width * 0.5, height * 0.5, 0}}, {0, 0, 1}, material);
}

void drawDressing(Rasterizer& rasterizer, const Camera& camera, const ClientWorld& world,
                  const ClientState& state, const ViewState& view) {
    drawSign(rasterizer, camera, {-3.8, 2.05, -6.82}, 2.4, 1.15,
             signTexture("FLUSH THE\nFLESH", "#d7ff54", "#2c3c31"));
    drawSign(rasterizer, camera, {3.0, 2.25, -6.82}, 2.15, 0.95,
             signTexture("NO GODS\nONLY CLOGS", "#e9a45f", "#26332b"));
    drawSign(rasterizer, camera, {0, 2.35, -6.81}, 2.5, 0.5,
             signTexture("PORCELAIN.EXE", "#d7ff54", "#101a13"));
    Material metal;
    metal.color = colorHex("#677568");
    metal.specular = 0.18;
    for (double x : {-7.7, 7.65}) {
        drawEllipsoid(rasterizer, camera, {x, 1.55, -6.65}, {0.11, 1.55, 0.11}, metal, 8, 6);
    }
    Material wood;
    wood.color = colorHex("#9b7748");
    drawEllipsoid(rasterizer, camera, {-1.1, 0.65, -5.35}, {0.035, 0.52, 0.035}, wood, 6, 5);
    Material rubber;
    rubber.color = colorHex("#6c382c");
    drawEllipsoid(rasterizer, camera, {-1.18, 0.15, -5.35}, {0.22, 0.15, 0.22}, rubber, 8, 4);
    Material paper;
    paper.color = colorHex("#d9decb");
    drawEllipsoid(rasterizer, camera, {1.25, 1.0, -6.55}, {0.16, 0.16, 0.24}, paper, 10, 5);
    Material fixture;
    fixture.color = colorHex("#c9efaf");
    fixture.unlit = true;
    for (double x : {-3.5, 3.5}) drawBox(rasterizer, camera, {x, 3.05, -1.7}, {2.3, 0.09, 0.28}, fixture);
    Material marker;
    marker.color = colorHex("#d7ff54");
    marker.unlit = true;
    drawRing(rasterizer, camera, {world.toiletFront.x, 0.025, world.toiletFront.y},
             world.interactionRange - 0.035, world.interactionRange, marker);
    drawOctahedron(rasterizer, camera, {world.toilet.x, 1.05 + std::sin(state.tick * 0.08) * 0.06, world.toilet.y},
                   view.highlightToilet ? 0.22 : 0.16, marker);
}

void drawBowl(Rasterizer& rasterizer, const Camera& camera, Vec3 center, const Material& porcelain) {
    constexpr double pi = 3.141592653589793;
    auto point = [&](double rx, double rz, double y, double angle) {
        return center + Vec3{std::cos(angle) * rx, y, 0.06 + std::sin(angle) * rz};
    };
    for (int i = 0; i < 32; ++i) {
        const double a = i * 2 * pi / 32, b = (i + 1) * 2 * pi / 32, middle = (a + b) * 0.5;
        const Vec3 rimA = point(0.1905, 0.270, kToiletRimHeight, a);
        const Vec3 rimB = point(0.1905, 0.270, kToiletRimHeight, b);
        const Vec3 seatA = point(0.1905, 0.270, kToiletSeatHeight, a);
        const Vec3 seatB = point(0.1905, 0.270, kToiletSeatHeight, b);
        const Vec3 innerA = point(0.145, 0.205, kToiletSeatHeight, a);
        const Vec3 innerB = point(0.145, 0.205, kToiletSeatHeight, b);
        const Vec3 baseA = point(0.12, 0.16, 0.08, a), baseB = point(0.12, 0.16, 0.08, b);
        const Vec3 lowerA = point(0.1335, 0.0985, kToiletWaterHeight, a);
        const Vec3 lowerB = point(0.1335, 0.0985, kToiletWaterHeight, b);
        drawQuad(rasterizer, camera, {baseA, rimA, rimB, baseB},
                 normalized(Vec3{std::cos(middle), 0.15, std::sin(middle)}), porcelain);
        drawQuad(rasterizer, camera, {rimA, seatA, seatB, rimB},
                 normalized(Vec3{std::cos(middle), 0.2, std::sin(middle)}), porcelain);
        drawQuad(rasterizer, camera, {seatA, innerA, innerB, seatB}, {0, 1, 0}, porcelain);
        drawQuad(rasterizer, camera, {innerA, lowerA, lowerB, innerB},
                 normalized(Vec3{-std::cos(middle), 0.65, -std::sin(middle)}), porcelain);
    }
}

void drawToilet(Rasterizer& rasterizer, const Camera& camera, gtc::Vec2 toilet) {
    Material porcelain;
    porcelain.color = colorHex("#d7ddc9");
    porcelain.specular = 0.30;
    const Vec3 center{toilet.x, 0, toilet.y};
    drawBox(rasterizer, camera, center + Vec3{0, 0.10, 0.02}, Vec3{0.24, 0.20, 0.30}, porcelain);
    drawBowl(rasterizer, camera, center, porcelain);
    drawBox(rasterizer, camera, center + Vec3{0, 0.5425, -0.2445}, Vec3{0.457, 0.365, 0.216}, porcelain);
    drawBox(rasterizer, camera, center + Vec3{0, 0.734, -0.2445}, Vec3{0.467, 0.018, 0.226}, porcelain);
    Material flush;
    flush.color = colorHex("#9ca9a4");
    flush.specular = 0.5;
    drawEllipsoid(rasterizer, camera, center + Vec3{0.16, 0.64, -0.132}, Vec3{0.035, 0.018, 0.012}, flush, 8, 4);
}

void drawObstacles(Rasterizer& rasterizer, const Camera& camera, const ClientWorld& world) {
    for (const Obstacle& obstacle : world.obstacles) {
        const Vec3 center{obstacle.center.x, (obstacle.base + obstacle.height) * 0.5, obstacle.center.y};
        const Vec3 size{obstacle.halfSize.x * 2, obstacle.height - obstacle.base, obstacle.halfSize.y * 2};
        if (obstacle.kind == "toilet") {
            drawToilet(rasterizer, camera, obstacle.center);
            continue;
        }
        Material material;
        if (obstacle.kind == "boundary") material.color = colorHex(obstacle.height > 1 ? "#53665a" : "#293a31");
        else if (obstacle.kind == "sink" || obstacle.kind == "tunnel") material.color = colorHex("#354b40");
        else if (obstacle.kind == "step") material.color = colorHex("#53665a");
        else material.color = colorHex("#5e5135");
        drawBox(rasterizer, camera, center, size, material);
        if (obstacle.kind == "boundary" && obstacle.height > 1) {
            Material trim;
            trim.color = colorHex("#1c2d25");
            drawBox(rasterizer, camera, {center.x, 1.05, center.z}, {size.x + 0.04, 0.16, size.z + 0.04}, trim);
        }
        if (obstacle.kind == "sink") {
            Material basin;
            basin.color = colorHex("#bdc9b5");
            basin.specular = 0.32;
            drawEllipsoid(rasterizer, camera, Vec3{center.x, obstacle.height + 0.05, center.z},
                          Vec3{0.65, 0.12, 0.40}, basin, 10, 5);
            drawBox(rasterizer, camera, Vec3{center.x, obstacle.height + 0.28, center.z - 0.28},
                    Vec3{0.08, 0.45, 0.08}, basin);
            Material mirrorFrame;
            mirrorFrame.color = colorHex("#1b2b23");
            drawBox(rasterizer, camera, {obstacle.center.x, 2.0, -6.84}, {1.84, 1.28, 0.05}, mirrorFrame);
            Material mirror;
            mirror.color = colorHex("#263d33");
            mirror.unlit = true;
            drawQuad(rasterizer, camera, std::array<Vec3, 4>{
                Vec3{obstacle.center.x - 0.85, 1.425, -6.80},
                Vec3{obstacle.center.x + 0.85, 1.425, -6.80}, Vec3{obstacle.center.x + 0.85, 2.575, -6.80},
                Vec3{obstacle.center.x - 0.85, 2.575, -6.80}}, {0, 0, 1}, mirror);
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

void drawWater(Rasterizer& rasterizer, const Camera& camera, const ClientWorld& world,
               const ClientState& state, double time) {
    constexpr double pi = 3.141592653589793;
    const Vec3 center{world.toilet.x, kToiletWaterHeight, world.toilet.y + kToiletBowlOffset};
    auto heightAt = [&](double x, double z) {
        double height = 0.0018 * std::sin(x * 31.0 + time * 3.4) * std::cos(z * 37.0 - time * 2.7);
        for (const WaterRipple& ripple : state.waterRipples) {
            const double d = std::hypot(x + center.x - ripple.position.x, z + center.z - ripple.position.y);
            height += std::sin(d * 68.0 - ripple.age * 19.0) * std::exp(-d * 10.0 - ripple.age * 2.4) *
                0.009 * ripple.strength;
        }
        return height;
    };
    auto point = [&](double radius, double angle) {
        const double x = std::cos(angle) * kToiletWater.x * radius;
        const double z = std::sin(angle) * kToiletWater.y * radius;
        return center + Vec3{x, heightAt(x, z), z};
    };
    Material water;
    water.color = colorHex("#55a894");
    water.specular = 0.82;
    water.opacity = 0.38;
    for (int ring = 0; ring < 5; ++ring) {
        for (int i = 0; i < 36; ++i) {
            const double a = i * 2.0 * pi / 36.0, b = (i + 1) * 2.0 * pi / 36.0;
            drawQuad(rasterizer, camera, {point(ring / 5.0, a), point((ring + 1) / 5.0, a),
                point((ring + 1) / 5.0, b), point(ring / 5.0, b)}, {0, 1, 0}, water);
        }
    }
    int splashed = 0;
    for (const WaterRipple& ripple : state.waterRipples) {
        const double fade = std::clamp(1.0 - ripple.age / 1.8, 0.0, 1.0);
        Material foam;
        foam.color = colorHex("#b5e4cf");
        foam.specular = 0.5;
        foam.opacity = 0.62 * fade;
        const double radius = 0.012 + ripple.age * 0.095;
        for (int i = 0; i < 28; ++i) {
            const double a = i * 2.0 * pi / 28.0, b = (i + 1) * 2.0 * pi / 28.0;
            auto edge = [&](double r, double angle) {
                return Vec3{ripple.position.x + std::cos(angle) * r,
                    kToiletWaterHeight + 0.005, ripple.position.y + std::sin(angle) * r};
            };
            const Vec3 a0 = edge(radius, a), a1 = edge(radius + 0.0035, a);
            const Vec3 b0 = edge(radius, b), b1 = edge(radius + 0.0035, b);
            auto inside = [&](Vec3 p) {
                const double x = (p.x - center.x) / kToiletWater.x, z = (p.z - center.z) / kToiletWater.y;
                return x * x + z * z < 0.98;
            };
            if (inside(a0) && inside(b0)) drawQuad(rasterizer, camera, {a0, a1, b1, b0}, {0, 1, 0}, foam);
        }
        if (ripple.age < 0.48 && splashed++ < 20) {
            Material splash = water;
            splash.opacity = 0.7 * (1.0 - ripple.age / 0.48);
            for (int j = 0; j < 4; ++j) {
                const double angle = j * 2.39996323 + ripple.id;
                const double travel = ripple.age * 0.12;
                const double lift = std::sin(ripple.age / 0.48 * pi) * 0.075 * ripple.strength;
                drawEllipsoid(rasterizer, camera, {ripple.position.x + std::cos(angle) * travel,
                    kToiletWaterHeight + 0.012 + lift, ripple.position.y + std::sin(angle) * travel},
                    {0.005, 0.014, 0.005}, splash, 6, 3);
            }
        }
    }
}

void drawPuddles(Rasterizer& rasterizer, const Camera& camera, const ClientState& state) {
    Material puddle;
    puddle.kind = Material::Kind::Puddle;
    puddle.color = colorHex("#414713");
    puddle.specular = 0.18;
    puddle.opacity = 0.54;
    const auto center = [](const Puddle& value) { return Vec3{value.position.x, value.height, value.position.y}; };
    const auto normal = [](const Puddle& value) {
        return value.vertical ? Vec3{value.normal.x, 0, value.normal.y} : Vec3{0, 1, 0};
    };
    std::vector<const Puddle*> chain;
    for (const Puddle& value : state.puddles) chain.push_back(&value);
    std::sort(chain.begin(), chain.end(), [](const Puddle* a, const Puddle* b) { return a->id < b->id; });
    std::map<const Puddle*, int> connections;
    for (std::size_t i = 1; i < chain.size(); ++i) {
        const Puddle& a = *chain[i - 1];
        const Puddle& b = *chain[i];
        const Vec3 delta = center(b) - center(a);
        const double span = length(delta);
        if (a.vertical != b.vertical || length(normal(a) - normal(b)) > 0.1 ||
            std::abs(dot(delta, normal(a))) > 0.02 || span < 0.02 || span > 0.55) continue;
        const Vec3 direction = delta / span;
        const Vec3 sideA = normalized(cross(normal(a), direction)) * a.size;
        const Vec3 sideB = normalized(cross(normal(b), direction)) * b.size;
        const Vec3 ca = center(a), cb = center(b);
        drawQuad(rasterizer, camera, {ca + sideA, cb + sideB, cb - sideB, ca - sideA}, normal(a), puddle);
        ++connections[&a];
        ++connections[&b];
    }
    for (const Puddle& puddleValue : state.puddles) {
        if (connections[&puddleValue] > 1) continue; // Interior discs are replaced by the continuous ribbon.
        const Vec3 centerPoint = center(puddleValue);
        const Vec2 tangent{-puddleValue.normal.y, puddleValue.normal.x};
        const Vec3 axisA = puddleValue.vertical ? Vec3{tangent.x * puddleValue.size * 0.72, 0,
            tangent.y * puddleValue.size * 0.72} : Vec3{puddleValue.size, 0, 0};
        const Vec3 axisB = puddleValue.vertical ? Vec3{0, puddleValue.size, 0} : Vec3{0, 0, puddleValue.size * 0.72};
        for (int i = 0; i < 10; ++i) {
            const double a = i * 2.0 * 3.141592653589793 / 10.0, b = (i + 1) * 2.0 * 3.141592653589793 / 10.0;
            const Vec3 first = centerPoint + axisA * std::cos(a) + axisB * std::sin(a);
            const Vec3 second = centerPoint + axisA * std::cos(b) + axisB * std::sin(b);
            rasterizer.triangle({cameraVertex(camera, centerPoint, normal(puddleValue), centerPoint),
                cameraVertex(camera, first, normal(puddleValue), first),
                cameraVertex(camera, second, normal(puddleValue), second)}, puddle);
        }
    }
}

void drawProjectiles(Rasterizer& rasterizer, const Camera& camera, const ClientState& state,
                     const std::unordered_map<std::uint32_t, HumanRenderCache>& humanCaches) {
    Material piss, bullet;
    piss.color = colorHex("#f2ce24");
    piss.unlit = true;
    piss.opacity = 0.88;
    bullet.color = colorHex("#fff0a0");
    bullet.unlit = true;
    const auto point = [](const Projectile& value) { return Vec3{value.position.x, value.height, value.position.y}; };
    auto link = [&](Vec3 a, Vec3 b, double phase, double maxSpan = 0.85) {
        const Vec3 delta = b - a;
        const double span = length(delta);
        if (span < 0.008 || span > maxSpan) return;
        const Vec3 direction = delta / span;
        Vec3 side = cross(direction, camera.position - (a + b) * 0.5);
        if (length(side) < 0.001) side = cross(direction, Vec3{0, 1, 0});
        if (length(side) < 0.001) side = {1, 0, 0};
        side = normalized(side);
        const Vec3 normal = normalized(cross(side, direction));
        Vec3 previous = a;
        for (int segment = 1; segment <= 3; ++segment) {
            const double t = segment / 3.0;
            Vec3 next = a + delta * t;
            if (segment < 3) next = next + side * (0.0016 * std::sin(phase + t * 13.0) +
                0.0007 * std::sin(phase * 1.7 + t * 29.0));
            drawQuad(rasterizer, camera, {previous + side * 0.0038, next + side * 0.0038,
                next - side * 0.0038, previous - side * 0.0038}, normal, piss);
            previous = next;
        }
    };
    std::vector<const Projectile*> stream;
    for (const Projectile& projectile : state.projectiles)
        if (projectile.kind == ProjectileKind::Piss) stream.push_back(&projectile);
    std::sort(stream.begin(), stream.end(), [](const Projectile* a, const Projectile* b) {
        return a->owner != b->owner ? a->owner < b->owner : a->id < b->id;
    });
    for (std::size_t index = 0; index < stream.size(); ++index) {
        const Projectile& oldest = *stream[index];
        if (oldest.toilet && (index == 0 || stream[index - 1]->owner != oldest.owner))
            link(point(oldest), {oldest.target.x, oldest.targetHeight, oldest.target.y}, oldest.owner * 0.713, 25.0);
        if (index == 0) continue;
        const Projectile& older = *stream[index - 1];
        const Projectile& newer = *stream[index];
        const double ageGap = older.age - newer.age;
        if (older.owner == newer.owner && ageGap >= -0.001 && ageGap <= 0.075)
            link(point(older), point(newer), older.owner * 0.713 + state.tick * 0.17);
    }
    for (std::size_t index = 0; index < stream.size(); ++index) {
        const Projectile& newest = *stream[index];
        const Character* owner = state.character(newest.owner);
        if ((index + 1 < stream.size() && stream[index + 1]->owner == newest.owner) || !owner ||
            !humanStreamAttached(*owner) || newest.age > 0.075) continue;
        const float pitch = std::atan2(newest.verticalVelocity + kProjectileGravity * newest.age, length(newest.velocity));
        const auto cached = humanCaches.find(newest.owner);
        const Vec3 exit = humanStreamOrigin(*owner, cached == humanCaches.end() ? nullptr : &cached->second, pitch);
        // Start inside the organ so the ribbon emerges from the opening itself.
        link(exit - normalized(point(newest) - exit) * 0.006, point(newest), owner->id * 0.713 + state.tick * 0.17);
    }
    for (const Projectile& projectile : state.projectiles) {
        if (projectile.kind != ProjectileKind::Bullet) continue;
        const Vec3 center = point(projectile);
        Vec3 direction{projectile.velocity.x, projectile.verticalVelocity, projectile.velocity.y};
        if (length(direction) < 0.001) direction = {0, 1, 0};
        direction = normalized(direction);
        Vec3 side = cross(direction, Vec3{0, 1, 0});
        if (length(side) < 0.001) side = {1, 0, 0};
        side = normalized(side);
        const Vec3 up = normalized(cross(side, direction));
        const Vec3 front = center + direction * 0.12, back = center - direction * 0.12;
        const Vec3 ring[4] = {center + side * 0.018, center + up * 0.018, center - side * 0.018, center - up * 0.018};
        for (int i = 0; i < 4; ++i) {
            const Vec3 a = ring[i], b = ring[(i + 1) % 4];
            const Vec3 normalA = normalized(cross(a - front, b - front));
            const Vec3 normalB = normalized(cross(b - back, a - back));
            rasterizer.triangle({cameraVertex(camera, front, normalA, front), cameraVertex(camera, a, normalA, a),
                                 cameraVertex(camera, b, normalA, b)}, bullet);
            rasterizer.triangle({cameraVertex(camera, back, normalB, back), cameraVertex(camera, b, normalB, b),
                                 cameraVertex(camera, a, normalB, a)}, bullet);
        }
    }
}

bool sameAppearance(const Appearance& a, const Appearance& b) {
    return a.male == b.male && a.skin == b.skin && a.hair == b.hair && a.hairColor == b.hairColor &&
        a.shirt == b.shirt && a.pants == b.pants && a.beard == b.beard && a.coat == b.coat &&
        a.sunglasses == b.sunglasses && a.eyeColor == b.eyeColor && a.makeup == b.makeup &&
        a.lipColor == b.lipColor && a.suit == b.suit && a.tie == b.tie && a.dress == b.dress &&
        a.beret == b.beret && a.earrings == b.earrings && a.necklace == b.necklace && a.fur == b.fur &&
        a.morphs == b.morphs;
}

bool animatedPose(const ClientState& state) {
    for (const Character& character : state.characters) {
        if (!character.onToilet && character.speed > 0.01F) return true;
    }
    return false;
}

bool samePuddles(const std::vector<Puddle>& a, const std::vector<Puddle>& b) {
    if (a.size() != b.size()) return false;
    const auto withinMillimeter = [](float x, float y) { return std::abs(static_cast<double>(x) - y) <= 0.001; };
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id || !withinMillimeter(a[i].position.x, b[i].position.x) ||
            !withinMillimeter(a[i].position.y, b[i].position.y) || !withinMillimeter(a[i].size, b[i].size) ||
            !withinMillimeter(a[i].height, b[i].height) || a[i].normal.x != b[i].normal.x ||
            a[i].normal.y != b[i].normal.y || a[i].vertical != b[i].vertical) return false;
    }
    return true;
}

bool sameGeometry(const ClientState& a, const ClientState& b) {
    if (a.characters.size() != b.characters.size()) return false;
    for (std::size_t i = 0; i < a.characters.size(); ++i) {
        const Character& left = a.characters[i];
        const Character& right = b.characters[i];
        if (left.id != right.id || !sameAppearance(left.appearance, right.appearance) ||
            left.speed != right.speed || left.elevation != right.elevation || left.charge != right.charge ||
            left.grabHeight != right.grabHeight || left.grounded != right.grounded ||
            left.pitch != right.pitch || left.pissYaw != right.pissYaw || left.pissPitch != right.pissPitch || left.attackAnim != right.attackAnim ||
            left.unzipped != right.unzipped || left.blocking != right.blocking ||
            left.crouched != right.crouched || left.prone != right.prone ||
            left.grabbing != right.grabbing || left.pissing != right.pissing || left.onToilet != right.onToilet ||
            left.toiletAction != right.toiletAction || left.path.size() != right.path.size()) return false;
        for (std::size_t p = 0; p < left.path.size(); ++p) {
            if (left.path[p].x != right.path[p].x || left.path[p].y != right.path[p].y) return false;
        }
    }
    return !(animatedPose(a) || animatedPose(b)) || a.tick == b.tick;
}

void updateObjectTransforms(GpuScene& scene, const ClientState& state) {
    for (GpuBatch& batch : scene.batches) {
        if (!batch.objectId) continue;
        const Character* character = state.character(batch.objectId);
        if (!character) continue;
        const double delta = std::atan2(std::sin(character->yaw - batch.yaw), std::cos(character->yaw - batch.yaw));
        const double c = std::cos(delta), s = std::sin(delta);
        const double rotatedX = c * batch.pivotX + s * batch.pivotZ;
        const double rotatedZ = -s * batch.pivotX + c * batch.pivotZ;
        batch.modelMatrix = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0,
            character->position.x - rotatedX, 0, character->position.y - rotatedZ, 1};
    }
}

} // namespace

bool WorldRenderer::load(const std::string& root, std::string& error) {
    root_ = root;
    // The authored block figure is the body of record. GTC_SCANBODY brings
    // back the imported scan, which the harness uses for the anatomical rig
    // checks that are written against its topology.
    if (!importedBody_ && !std::getenv("GTC_SCANBODY")) {
        male_ = blockBody(true);
        female_ = blockBody(false);
        male_.loadMorphSpecs(root);
        female_.loadMorphSpecs(root);
    } else {
        if (!male_.loadRuntime(root, true, error) || !female_.loadRuntime(root, false, error)) return false;
        if (!maleHair_.load(root, male_, error) || !femaleHair_.load(root, female_, error)) return false;
    }
    humanCache_.clear();
    gpuPuddleGeometry_.clear();
    gpuPuddleBatchCount_ = 0;
    gpuValid_ = false;
    ready_ = true;
    return true;
}

const std::vector<MorphSpec>& WorldRenderer::morphSpecs(bool male) const {
    return male ? male_.specs : female_.specs;
}

double headAngleForGaze(double gazeAngle) {
    // Small gaze shifts are ocular; beyond a comfortable 30 degrees the head
    // supplies the remainder instead of rotating the skull by the full gaze.
    return gazeAngle - std::clamp(gazeAngle, -kEyeInHeadRange, kEyeInHeadRange);
}

double clampedBodyRelativeYaw(float viewYaw, float bodyYaw) {
    constexpr double limit = 100.0 * 3.141592653589793 / 180.0;
    constexpr double fullTurn = 2.0 * 3.141592653589793;
    // Reduce only complete turns. Wrapping through atan2 first would change
    // +180 degrees into -180 degrees and snap the clamped head to the other side.
    const double continuous = std::fmod(static_cast<double>(viewYaw) - bodyYaw, fullTurn);
    return std::clamp(continuous, -limit, limit);
}

void clampViewToBody(ViewState& view, float bodyYaw, bool normalize) {
    constexpr double limit = 100.0 * 3.141592653589793 / 180.0;
    double relative = static_cast<double>(view.yaw) - bodyYaw;
    if (normalize) relative = std::atan2(std::sin(relative), std::cos(relative));
    view.yaw = bodyYaw + static_cast<float>(std::clamp(relative, -limit, limit));
}

Camera WorldRenderer::viewCamera(const ClientWorld& world, const ClientState& state,
                                 std::uint32_t localId, const ViewState& view) const {
    const Character* local = state.character(localId);
    if (view.mode != ViewMode::FirstPerson || !local) return makeExternalCamera(world, state, localId, view);
    const HumanModel& model = local->appearance.male ? male_ : female_;
    HumanRenderCache& cache = humanCache_[localId];
    prepareHuman(model, *local, cache);
    Camera camera;
    camera.width = std::max(1, view.width);
    camera.height = std::max(1, view.height);
    camera.fovDegrees = view.aiming ? 52 : 62;
    const double gazeYaw = clampedBodyRelativeYaw(view.yaw, local->yaw);
    constexpr double gazeYawLimit = 100.0 * 3.141592653589793 / 180.0;
    // Cervical motion is coupled rather than two independent maximum angles.
    // Keep the combined downward gaze clear of the posed shoulder and arm.
    constexpr double maximumDowngaze = 3.141592653589793 * 0.5;
    const double downwardLimit = maximumDowngaze - (maximumDowngaze - 0.5) *
        std::abs(gazeYaw) / gazeYawLimit;
    const double gazePitch = std::clamp(static_cast<double>(view.pitch), -downwardLimit, 1.57079633);
    const double headYaw = headAngleForGaze(gazeYaw);
    // The anatomical pose applies eye-in-head motion and solves downward
    // chin/manubrium contact from the total requested gaze angle.
    camera.position = humanEyePosition(model, cache.rest, *local, state.tick, headYaw, gazePitch);
    const double viewYaw = local->yaw + gazeYaw;
    const double cp = std::cos(gazePitch), sp = std::sin(gazePitch);
    const Vec3 forward{std::sin(viewYaw) * cp, sp, std::cos(viewYaw) * cp};
    camera.target = camera.position + forward;
    return camera;
}

void turnViewWithBody(ViewState& view, float previousBodyYaw, float currentBodyYaw) {
    const double delta = std::atan2(std::sin(currentBodyYaw - previousBodyYaw),
                                    std::cos(currentBodyYaw - previousBodyYaw));
    const double yaw = view.yaw + delta;
    view.yaw = static_cast<float>(std::atan2(std::sin(yaw), std::cos(yaw)));
}

void rotateView(ViewState& view, double mouseX, double mouseY) {
    if (view.mode == ViewMode::God) return;
    view.yaw -= static_cast<float>(mouseX * 0.0022);
    view.pitch -= static_cast<float>(mouseY * 0.0022);
    view.pitch = std::clamp(view.pitch, view.mode == ViewMode::FirstPerson ? -1.57079633F : -1.05F,
                            view.mode == ViewMode::FirstPerson ? 1.57079633F : -0.06F);
}

bool WorldRenderer::render(const ClientWorld& world, const ClientState& state, std::uint32_t localId,
                           const ViewState& view, NativeFrame& frame, std::string& error) const {
    if (!ready_) {
        error = "Native world renderer is not loaded";
        return false;
    }
    const auto profileStart = std::chrono::steady_clock::now();
    Camera camera = viewCamera(world, state, localId, view);
    camera.worldSpaceGpu = view.gpu;
    const bool rebuild = !view.gpu || !gpuValid_ || !sameGeometry(gpuGeometryState_, state) || gpuLocalId_ != localId ||
        gpuMode_ != static_cast<int>(view.mode) || gpuHighlight_ != view.highlightToilet;
    auto humanStart = profileStart;
    auto finishStart = profileStart;
    if (rebuild) {
        if (view.gpu) gpuScene_.batches.resize(gpuStaticBatchCount_);
        Rasterizer rasterizer(std::max(1, view.width), std::max(1, view.height), colorHex("#111913"), camera.focal(), view.gpu,
                              view.gpu ? std::move(gpuScene_) : GpuScene{}, &buffer_);
        rasterizer.setCamera(camera);
        const bool subProfile = std::getenv("GTC_PROFILE2") != nullptr;
        const auto t0 = std::chrono::steady_clock::now();
        drawFloor(rasterizer, camera, world);
        const auto t1 = std::chrono::steady_clock::now();
        if (view.mode != ViewMode::God) {
            Material ceiling;
            ceiling.color = colorHex("#1b2720");
            drawBox(rasterizer, camera, {0, 3.41, 0}, {world.width, 0.04, world.depth}, ceiling);
        }
        const auto t2 = std::chrono::steady_clock::now();
        drawDressing(rasterizer, camera, world, state, view);
        const auto t3 = std::chrono::steady_clock::now();
        if (subProfile) {
            const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            std::cerr << "  floor=" << ms(t0, t1) << " ceiling=" << ms(t1, t2) << " dressing=" << ms(t2, t3) << "\n";
        }
        if (view.highlightToilet) {
            Material highlight;
            highlight.color = colorHex("#86b83e");
            drawEllipsoid(rasterizer, camera, Vec3{world.toilet.x, 0.025, world.toilet.y},
                          Vec3{0.82, 0.018, 0.94}, highlight, 24, 5);
        }
        drawObstacles(rasterizer, camera, world);
        drawPaths(rasterizer, camera, state, localId);
        humanStart = std::chrono::steady_clock::now();
        for (const Character& character : state.characters) {
            rasterizer.setObject(character.id, {character.position.x, character.position.y}, character.yaw);
            const HumanModel& model = character.appearance.male ? male_ : female_;
            const HairLibrary& hair = character.appearance.male ? maleHair_ : femaleHair_;
            const bool hideHead = view.mode == ViewMode::FirstPerson && character.id == localId;
            if (character.id == localId && view.mode != ViewMode::FirstPerson && character.toiletAction == ToiletAction::None) {
                Material ring;
                ring.color = colorHex("#d7ff54");
                ring.unlit = true;
                drawRing(rasterizer, camera, {character.position.x, character.elevation + 0.028, character.position.y},
                         0.36, 0.41, ring, 24);
            }
            drawHuman(rasterizer, camera, model, hair, character, humanCache_[character.id], root_, state.tick, hideHead);
        }
        rasterizer.setObject(0, {}, 0);
        if (!view.gpu) {
            const double time = std::chrono::duration<double>(profileStart.time_since_epoch()).count();
            drawWater(rasterizer, camera, world, state, time);
            drawPuddles(rasterizer, camera, state);
            drawProjectiles(rasterizer, camera, state, humanCache_);
        }
        finishStart = std::chrono::steady_clock::now();
        if (view.gpu) {
            gpuScene_ = rasterizer.takeGpuScene();
            gpuStaticBatchCount_ = gpuScene_.batches.size();
            gpuPuddleBatchCount_ = 0;
            gpuPuddleGeometry_.clear();
            gpuScene_.revision = ++gpuRevision_;
            gpuGeometryState_ = state;
            gpuLocalId_ = localId;
            gpuMode_ = static_cast<int>(view.mode);
            gpuHighlight_ = view.highlightToilet;
            gpuValid_ = true;
        } else {
            frame = rasterizer.finish();
        }
    }
    if (view.gpu) {
        if (!samePuddles(gpuPuddleGeometry_, state.puddles)) {
            gpuScene_.batches.resize(gpuStaticBatchCount_);
            Rasterizer decals(std::max(1, view.width), std::max(1, view.height), gpuScene_.background,
                              camera.focal(), true);
            decals.setCamera(camera);
            drawPuddles(decals, camera, state);
            GpuScene cached = decals.takeGpuScene();
            const std::uint64_t revision = ++gpuProjectileRevision_;
            for (GpuBatch& batch : cached.batches) {
                if (batch.vertices.empty()) continue;
                batch.revision = revision;
                gpuScene_.batches.push_back(std::move(batch));
            }
            gpuPuddleBatchCount_ = gpuScene_.batches.size() - gpuStaticBatchCount_;
            gpuPuddleGeometry_ = state.puddles;
        }
        // Only water and flying droplets are re-uploaded each frame.
        gpuScene_.batches.resize(gpuStaticBatchCount_ + gpuPuddleBatchCount_);
        Rasterizer particles(std::max(1, view.width), std::max(1, view.height), gpuScene_.background,
                             camera.focal(), true);
        particles.setCamera(camera);
        const double time = std::chrono::duration<double>(profileStart.time_since_epoch()).count();
        drawWater(particles, camera, world, state, time);
        drawProjectiles(particles, camera, state, humanCache_);
        GpuScene dynamic = particles.takeGpuScene();
        const std::uint64_t dynamicRevision = ++gpuProjectileRevision_;
        for (GpuBatch& batch : dynamic.batches) {
            if (batch.vertices.empty()) continue;
            batch.revision = dynamicRevision;
            gpuScene_.batches.push_back(std::move(batch));
        }
        gpuScene_.width = std::max(1, view.width);
        gpuScene_.height = std::max(1, view.height);
        gpuScene_.focal = camera.focal();
        gpuScene_.viewMatrix = camera.viewMatrix();
        updateObjectTransforms(gpuScene_, state);
        if (!renderGpuScene(gpuScene_, frame)) {
            error = "Native GPU scene rendering failed";
            return false;
        }
    }
    const std::size_t framePixels = static_cast<std::size_t>(frame.width) * frame.height;
    // A frame that came back in window order is a finished frame: it has four
    // bytes a pixel and no coverage mask, because nothing has to blend it. Only
    // the rgb form has a mask to check.
    const bool havePixels = frame.canvasIsBgra() ||
        (frame.rgb.size() == framePixels * 3 && frame.alpha.size() == framePixels);
    if (view.gpu && !havePixels) {
        error = "Native GPU scene rendering failed";
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
    drawLabels(frame, camera, state, localId);
    frame.logicalUi();
    drawHud(frame, world, state, localId, view);
    drawCrosshair(frame, view);
    return true;
}

} // namespace gtc::native
