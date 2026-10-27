#include "client/renderer.h"

#include "client/human_renderer.h"

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

void drawBowl(Rasterizer& rasterizer, const Camera& camera, Vec3 center,
              const Material& porcelain, const Material& water) {
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
    drawEllipsoid(rasterizer, camera, center + Vec3{0, kToiletWaterHeight, 0.06},
                  Vec3{0.1335, 0.006, 0.0985}, water, 24, 4);
}

void drawToilet(Rasterizer& rasterizer, const Camera& camera, gtc::Vec2 toilet) {
    Material porcelain;
    porcelain.color = colorHex("#d7ddc9");
    porcelain.specular = 0.30;
    Material dark;
    dark.color = colorHex("#25443a");
    dark.specular = 0.42;
    const Vec3 center{toilet.x, 0, toilet.y};
    drawBox(rasterizer, camera, center + Vec3{0, 0.10, 0.02}, Vec3{0.24, 0.20, 0.30}, porcelain);
    drawBowl(rasterizer, camera, center, porcelain, dark);
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

void drawPuddles(Rasterizer& rasterizer, const Camera& camera, const ClientState& state) {
    Material puddle;
    puddle.kind = Material::Kind::Puddle;
    puddle.color = colorHex("#414713");
    puddle.specular = 0.18;
    puddle.opacity = 0.54;
    for (const Puddle& puddleValue : state.puddles) {
        const Vec3 center{puddleValue.position.x, puddleValue.height, puddleValue.position.y};
        const Vec2 tangent{-puddleValue.normal.y, puddleValue.normal.x};
        const Vec3 axisA = puddleValue.vertical ? Vec3{tangent.x * puddleValue.size * 0.72, 0,
            tangent.y * puddleValue.size * 0.72} : Vec3{puddleValue.size, 0, 0};
        const Vec3 axisB = puddleValue.vertical ? Vec3{0, puddleValue.size, 0} : Vec3{0, 0, puddleValue.size * 0.72};
        const Vec3 normal = puddleValue.vertical ? Vec3{puddleValue.normal.x, 0, puddleValue.normal.y} : Vec3{0, 1, 0};
        for (int i = 0; i < 14; ++i) {
            const double a = i * 2.0 * 3.141592653589793 / 14.0, b = (i + 1) * 2.0 * 3.141592653589793 / 14.0;
            const Vec3 first = center + axisA * std::cos(a) + axisB * std::sin(a);
            const Vec3 second = center + axisA * std::cos(b) + axisB * std::sin(b);
            rasterizer.triangle({cameraVertex(camera, center, normal, center), cameraVertex(camera, first, normal, first),
                                 cameraVertex(camera, second, normal, second)}, puddle);
        }
    }
}

void drawProjectiles(Rasterizer& rasterizer, const Camera& camera, const ClientState& state) {
    Material piss, bullet;
    piss.color = colorHex("#d4ce68");
    piss.specular = 0.35;
    bullet.color = colorHex("#fff0a0");
    bullet.unlit = true;
    for (const Projectile& projectile : state.projectiles) {
        const bool isBullet = projectile.kind == ProjectileKind::Bullet;
        const Vec3 center{projectile.position.x, projectile.height, projectile.position.y};
        Vec3 direction{projectile.velocity.x, projectile.verticalVelocity, projectile.velocity.y};
        if (length(direction) < 0.001) direction = {0, 1, 0};
        direction = normalized(direction);
        Vec3 side = cross(direction, Vec3{0, 1, 0});
        if (length(side) < 0.001) side = {1, 0, 0};
        side = normalized(side);
        const Vec3 up = normalized(cross(side, direction));
        const double radius = isBullet ? 0.018 : 0.030, length = isBullet ? 0.24 : 0.17;
        const Vec3 front = center + direction * (length * 0.5), back = center - direction * (length * 0.5);
        const Vec3 ring[4] = {center + side * radius, center + up * radius, center - side * radius, center - up * radius};
        const Material& material = isBullet ? bullet : piss;
        for (int i = 0; i < 4; ++i) {
            const Vec3 a = ring[i], b = ring[(i + 1) % 4];
            const Vec3 normalA = normalized(cross(a - front, b - front));
            const Vec3 normalB = normalized(cross(b - back, a - back));
            rasterizer.triangle({cameraVertex(camera, front, normalA, front), cameraVertex(camera, a, normalA, a),
                                 cameraVertex(camera, b, normalA, b)}, material);
            rasterizer.triangle({cameraVertex(camera, back, normalB, back), cameraVertex(camera, b, normalB, b),
                                 cameraVertex(camera, a, normalB, a)}, material);
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

bool sameGeometry(const ClientState& a, const ClientState& b) {
    if (a.characters.size() != b.characters.size() || a.puddles.size() != b.puddles.size() ||
        a.projectiles.size() != b.projectiles.size()) return false;
    for (std::size_t i = 0; i < a.characters.size(); ++i) {
        const Character& left = a.characters[i];
        const Character& right = b.characters[i];
        if (left.id != right.id || !sameAppearance(left.appearance, right.appearance) ||
            left.speed != right.speed || left.elevation != right.elevation || left.charge != right.charge ||
            left.grabHeight != right.grabHeight || left.grounded != right.grounded ||
            left.pitch != right.pitch || left.attackAnim != right.attackAnim ||
            left.unzipped != right.unzipped || left.blocking != right.blocking ||
            left.crouched != right.crouched || left.prone != right.prone ||
            left.grabbing != right.grabbing || left.pissing != right.pissing || left.onToilet != right.onToilet ||
            left.toiletAction != right.toiletAction || left.path.size() != right.path.size()) return false;
        for (std::size_t p = 0; p < left.path.size(); ++p) {
            if (left.path[p].x != right.path[p].x || left.path[p].y != right.path[p].y) return false;
        }
    }
    for (std::size_t i = 0; i < a.puddles.size(); ++i) {
        const Puddle& left = a.puddles[i];
        const Puddle& right = b.puddles[i];
        if (left.id != right.id || left.position.x != right.position.x || left.position.y != right.position.y ||
            left.size != right.size || left.height != right.height || left.normal.x != right.normal.x ||
            left.normal.y != right.normal.y || left.vertical != right.vertical) return false;
    }
    for (std::size_t i = 0; i < a.projectiles.size(); ++i) {
        const Projectile& left = a.projectiles[i];
        const Projectile& right = b.projectiles[i];
        if (left.id != right.id || left.kind != right.kind || left.position.x != right.position.x ||
            left.position.y != right.position.y || left.height != right.height || left.velocity.x != right.velocity.x ||
            left.velocity.y != right.velocity.y || left.verticalVelocity != right.verticalVelocity) return false;
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

void drawLabels(NativeFrame& frame, const Camera& camera, const ClientState& state, std::uint32_t localId) {
    for (const Character& character : state.characters) {
        if (character.id == localId) continue;
        const double labelHeight = character.prone ? 0.65 : character.crouched ? 1.35 : 2.08;
        const Vec3 world{character.position.x, character.elevation + labelHeight, character.position.y};
        if (length(camera.position - world) < 1.6) continue;
        const Vec3 point = camera.view(world);
        if (point.z <= 0.2) continue;
        const double sx = camera.width * 0.5 + camera.focal() * point.x / point.z;
        const double sy = camera.height * 0.5 - camera.focal() * point.y / point.z;
        if (sx < 20 || sy < 40 || sx >= camera.width - 20 || sy >= camera.height - 40) continue;
        const std::string label = std::string(character.ai ? "AI " : "") + character.name.substr(0, 18);
        const int width = std::min(250, std::max(64, static_cast<int>(label.size()) * 13 + 18));
        const int x = std::clamp(static_cast<int>(sx) - width / 2, 4, std::max(4, frame.width - width - 4));
        const int y = std::clamp(static_cast<int>(sy) - 36, 64, std::max(64, frame.height - 90));
        frame.blendRectangle(x, y, width, 36, colorHex("#07100c"), 0.84);
        frame.rectangle(x, y, 3, 36, colorHex(character.color.c_str()));
        drawText(frame, x + 9, y + 4, label, colorHex(character.color.c_str()));
    }
}

void panel(NativeFrame& frame, int x, int y, int width, int height, bool accent = false) {
    frame.blendRectangle(x + 3, y + 4, width, height, colorHex("#020604"), 0.42);
    frame.blendRectangle(x, y, width, height, colorHex("#0b1411"), 0.86);
    frame.rectangle(x, y, width, 2, colorHex("#52665d"));
    frame.rectangle(x, y, 2, height, colorHex(accent ? "#d7ff54" : "#33483e"));
}

void drawMinimap(NativeFrame& frame, const ClientWorld& world, const ClientState& state,
                 std::uint32_t localId, int x) {
    const int y = 112, width = 132, height = 104;
    frame.rectangle(x, y, width, height, colorHex("#07100b"));
    const double scale = std::min((width - 12.0) / world.width, (height - 12.0) / world.depth);
    auto project = [&](double px, double pz, int& sx, int& sy) {
        sx = x + width / 2 + static_cast<int>(px * scale);
        sy = y + height / 2 + static_cast<int>(pz * scale);
    };
    for (const Obstacle& obstacle : world.obstacles) {
        if (obstacle.kind == "boundary") continue;
        int sx = 0, sy = 0;
        project(obstacle.center.x, obstacle.center.y, sx, sy);
        const int w = std::max(2, static_cast<int>(obstacle.halfSize.x * 2 * scale));
        const int h = std::max(2, static_cast<int>(obstacle.halfSize.y * 2 * scale));
        frame.rectangle(sx - w / 2, sy - h / 2, w, h,
                        colorHex(obstacle.kind == "toilet" ? "#d7ff54" : "#526158"));
    }
    for (const Puddle& puddle : state.puddles) {
        int sx = 0, sy = 0;
        project(puddle.position.x, puddle.position.y, sx, sy);
        frame.rectangle(sx - 1, sy - 1, 3, 3, colorHex("#b5a636"));
    }
    for (const Character& character : state.characters) {
        int sx = 0, sy = 0;
        project(character.position.x, character.position.y, sx, sy);
        const Color color = character.id == localId ? colorHex("#d7ff54") :
            character.ai ? colorHex("#ff765f") : colorHex(character.color.c_str());
        frame.rectangle(sx - 2, sy - 2, character.id == localId ? 5 : 4, character.id == localId ? 5 : 4, color);
        frame.rectangle(sx + static_cast<int>(std::sin(character.yaw) * 5),
                        sy + static_cast<int>(std::cos(character.yaw) * 5), 2, 2, color);
    }
}

void drawCrosshair(NativeFrame& frame, ViewMode mode) {
    if (mode == ViewMode::God) return;
    const int cx = frame.width / 2, cy = frame.height / 2;
    const int unit = std::clamp(static_cast<int>(std::lround(std::min(frame.width, frame.height) / 640.0)), 1, 4);
    const int gap = 3 * unit, arm = 7 * unit, length = arm - gap + 1;
    const int x = cx - unit / 2, y = cy - unit / 2;
    const double scale = frame.uiScale, offsetX = frame.uiOffsetX, offsetY = frame.uiOffsetY;
    frame.uiScale = 1.0;
    frame.uiOffsetX = frame.uiOffsetY = 0.0;
    const Color shadow = colorHex("#020604"), ink = colorHex("#d7ff54");
    const auto stroke = [&](int sx, int sy, int width, int height, bool outline) {
        const int border = outline ? unit : 0;
        frame.rectangle(sx - border, sy - border, width + 2 * border, height + 2 * border,
                        outline ? shadow : ink);
    };
    for (int pass = 0; pass < 2; ++pass) {
        const bool outline = pass == 0;
        stroke(x, y, unit, unit, outline);
        stroke(cx - arm, y, length, unit, outline);
        stroke(cx + gap, y, length, unit, outline);
        stroke(x, cy - arm, unit, length, outline);
        stroke(x, cy + gap, unit, length, outline);
    }
    frame.uiScale = scale;
    frame.uiOffsetX = offsetX;
    frame.uiOffsetY = offsetY;
}

void drawHud(NativeFrame& frame, const ClientWorld& world, const ClientState& state,
             std::uint32_t localId, const ViewState& view) {
    const Character* local = state.character(localId);
    const Color text = colorHex("#d7ff54"), muted = colorHex("#b4c5ba"), cyan = colorHex("#6edbd2");
    const double centeredX = frame.uiOffsetX;
    panel(frame, 0, 0, 640, 60, true);
    drawText(frame, 12, 5, "GTC // 1453", text);
    drawText(frame, 12, 34, "PORCELAIN PROTOCOL", muted);
    const int minuteOfDay = static_cast<int>(state.gameTime / 60.0) % 1440;
    const auto two = [](int value) { return (value < 10 ? "0" : "") + std::to_string(value); };
    drawText(frame, 282, 5, "DAY " + std::to_string(state.day) + " " + two(minuteOfDay / 60) + ":" +
        two(minuteOfDay % 60), text);
    drawText(frame, 350, 34, "15X TIME", muted);
    drawText(frame, 476, 5, "NET // ONLINE", text);
    std::string performance = std::to_string(state.characters.size()) + " CIT / " + (view.gpu ? "GPU" : "CPU");
    if (view.fps > 0) performance += " " + std::to_string(static_cast<int>(view.fps + 0.5));
    drawText(frame, 476, 34, performance, muted);
    if (local) {
        frame.uiOffsetX = 0.0;
        panel(frame, 12, 74, 218, 174, true);
        drawText(frame, 24, 82, "MEAT " + std::to_string(static_cast<int>(local->health)) + " HP", text);
        drawText(frame, 24, 112, local->name.substr(0, 15), muted);
        drawText(frame, 24, 145, "BLADDER " + std::to_string(static_cast<int>(local->needs.bladder)) + "%", cyan);
        frame.rectangle(24, 174, 194, 8, colorHex("#29332d"));
        frame.rectangle(24, 174, static_cast<int>(194 * std::clamp(local->needs.bladder / 100.0F, 0.0F, 1.0F)), 8, cyan);
        drawText(frame, 24, 191, "BOWEL " + std::to_string(static_cast<int>(local->needs.bowel)) + "%", colorHex("#ffcc62"));
        frame.rectangle(24, 220, 194, 8, colorHex("#29332d"));
        frame.rectangle(24, 220, static_cast<int>(194 * std::clamp(local->needs.bowel / 100.0F, 0.0F, 1.0F)), 8,
                        colorHex("#ffcc62"));
        frame.uiOffsetX = frame.width - 640 * frame.uiScale;
        panel(frame, 478, 74, 150, 162);
        drawText(frame, 488, 82, "SECTOR 01", text);
        drawMinimap(frame, world, state, localId, 490);
        drawText(frame, 488, 222, "LOCAL / AI", muted);
        frame.uiOffsetX = centeredX;
        frame.uiOffsetY = (frame.height - 480 * frame.uiScale) * 0.5;
        if (local->grabbing) {
            panel(frame, 220, 264, 200, 34, true);
            drawText(frame, 232, 270, "W CLIMB / S DROP", text);
        } else if (local->charge > 0.001F) {
            const Color chargeColor = local->fullChargeTime > 1.0F ? colorHex("#ff765f") : text;
            panel(frame, 220, 264, 200, 34, local->fullChargeTime > 0.0F);
            drawText(frame, 232, 266, "JUMP " + std::to_string(static_cast<int>(local->charge * 100 + 0.5F)) + "%", chargeColor);
            frame.rectangle(228, 292, 184, 3, colorHex("#29332d"));
            frame.rectangle(228, 292, static_cast<int>(184 * local->charge), 3, chargeColor);
        }
        frame.uiOffsetY = std::max(0.0, frame.height - 480 * frame.uiScale);
        panel(frame, 12, 302, 616, 44);
        if (!local->notice.empty()) drawText(frame, 24, 310, local->notice.substr(0, 52), muted);
        else if (!state.events.empty()) drawText(frame, 24, 310, state.events.back().text.substr(0, 52), muted);
        else drawText(frame, 24, 310, "QUEST // ANSWER NATURE'S CALL", text);
        const double range = distance(local->position, world.toiletFront);
        panel(frame, 12, 354, 616, 44, range <= world.interactionRange || local->toiletAction != ToiletAction::None);
        if (local->toiletAction != ToiletAction::None) drawText(frame, 24, 362, "E // STAND UP", text);
        else if (range <= world.interactionRange) drawText(frame, 24, 362, "E // CHOOSE ACTION", text);
        else drawText(frame, 24, 362, "MOVE CLOSER TO INTERACT", muted);
    }
    frame.uiOffsetX = centeredX;
    frame.uiOffsetY = std::max(0.0, frame.height - 480 * frame.uiScale);
    panel(frame, 0, 408, 640, 72);
    drawText(frame, 12, 416, "1 THIRD", view.mode == ViewMode::ThirdPerson ? text : muted);
    drawText(frame, 132, 416, "2 GOD", view.mode == ViewMode::God ? text : muted);
    drawText(frame, 232, 416, "3 FIRST", view.mode == ViewMode::FirstPerson ? text : muted);
    if (local && view.mode != ViewMode::God) {
        const char* stance = local->blocking ? "BLOCK" : view.aiming ? "AIM" : local->prone ? "PRONE" :
            local->grabbing ? "GRAB" : local->crouched ? "CROUCH" : local->unzipped ? "UNZIP" :
            !local->grounded ? "AIR" : view.slowWalk ? "SLOW" : "FAST";
        drawText(frame, 520, 416, stance, muted);
    }
    if (!local) drawText(frame, 12, 448, "WAITING FOR CITIZEN", text);
    else if (view.mode == ViewMode::God) drawText(frame, 12, 448, "RMB GO / SHIFT RUN / 2X ADV / LMB ATTACK", muted);
    else drawText(frame, 12, 448, "WASD / SPACE JUMP / CTRL GRAB / ALT CROUCH", muted);
}

} // namespace

bool WorldRenderer::load(const std::string& root, std::string& error) {
    root_ = root;
    if (!male_.loadRuntime(root, true, error) || !female_.loadRuntime(root, false, error)) return false;
    if (!maleHair_.load(root, male_, error) || !femaleHair_.load(root, female_, error)) return false;
    humanCache_.clear();
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

void interpolateCharacters(const ClientState& target, ClientState& current, float dt) {
    const double blend = 1.0 - std::exp(-15.0 * dt);
    for (Character& character : current.characters) {
        const Character* destination = target.character(character.id);
        if (!destination) continue;
        if (destination->onToilet || destination->toiletAction == ToiletAction::Piss) {
            character.position = destination->position;
            character.elevation = destination->elevation;
            character.yaw = destination->yaw;
            continue;
        }
        character.elevation += static_cast<float>((destination->elevation - character.elevation) * blend);
        character.position.x += static_cast<float>((destination->position.x - character.position.x) * blend);
        character.position.y += static_cast<float>((destination->position.y - character.position.y) * blend);
        const double angle = std::atan2(std::sin(destination->yaw - character.yaw),
                                        std::cos(destination->yaw - character.yaw));
        character.yaw = static_cast<float>(character.yaw + angle * blend);
    }
    for (Projectile& projectile : current.projectiles) {
        const auto found = std::find_if(target.projectiles.begin(), target.projectiles.end(), [&](const Projectile& candidate) {
            return candidate.id == projectile.id;
        });
        if (found == target.projectiles.end()) continue;
        projectile.position.x += static_cast<float>((found->position.x - projectile.position.x) * blend);
        projectile.position.y += static_cast<float>((found->position.y - projectile.position.y) * blend);
        projectile.height += static_cast<float>((found->height - projectile.height) * blend);
        projectile.velocity.x += static_cast<float>((found->velocity.x - projectile.velocity.x) * blend);
        projectile.velocity.y += static_cast<float>((found->velocity.y - projectile.velocity.y) * blend);
        projectile.verticalVelocity += static_cast<float>((found->verticalVelocity - projectile.verticalVelocity) * blend);
    }
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
        Rasterizer rasterizer(std::max(1, view.width), std::max(1, view.height), colorHex("#111913"), camera.focal(), view.gpu,
                              view.gpu ? std::move(gpuScene_) : GpuScene{});
        rasterizer.setCamera(camera);
        drawFloor(rasterizer, camera, world);
        if (view.mode != ViewMode::God) {
            Material ceiling;
            ceiling.color = colorHex("#1b2720");
            drawBox(rasterizer, camera, {0, 3.41, 0}, {world.width, 0.04, world.depth}, ceiling);
        }
        drawDressing(rasterizer, camera, world, state, view);
        if (view.highlightToilet) {
            Material highlight;
            highlight.color = colorHex("#86b83e");
            drawEllipsoid(rasterizer, camera, Vec3{world.toilet.x, 0.025, world.toilet.y},
                          Vec3{0.82, 0.018, 0.94}, highlight, 24, 5);
        }
        drawObstacles(rasterizer, camera, world);
        drawPaths(rasterizer, camera, state, localId);
        drawPuddles(rasterizer, camera, state);
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
        drawProjectiles(rasterizer, camera, state);
        finishStart = std::chrono::steady_clock::now();
        if (view.gpu) {
            gpuScene_ = rasterizer.takeGpuScene();
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
    if (view.gpu && (frame.rgb.size() != framePixels * 3 || frame.alpha.size() != framePixels)) {
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
    drawCrosshair(frame, view.mode);
    return true;
}

} // namespace gtc::native
