#include "client/world_renderer_draw.h"

#include <cmath>
#include <cstdio>
#include <map>

namespace gtc::native::world_draw {

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
              const Material& material, int segments) {
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

}  // namespace gtc::native::world_draw
