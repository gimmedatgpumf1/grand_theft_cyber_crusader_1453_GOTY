// The colour arithmetic both halves of the rasterizer need: the linear/sRGB
// tables, the texture fetch, and the two comparisons that decide whether a
// material is the same material as last frame. They were in an anonymous
// namespace in raster.cpp; the frame buffer operations use them too, so they are
// inline here rather than copied.

#ifndef GTC_CLIENT_RASTER_COLOR_H
#define GTC_CLIENT_RASTER_COLOR_H

#include "client/raster.h"

#include <array>
#include <cmath>

namespace gtc::native::raster_detail {

constexpr int kColorLutSize = 4096;

struct ColorLuts {
    std::array<double, kColorLutSize> encode{};
    std::array<double, kColorLutSize> decode{};
    ColorLuts() {
        for (int i = 0; i < kColorLutSize; ++i) {
            const double value = i / static_cast<double>(kColorLutSize - 1);
            decode[i] = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            encode[i] = value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
        }
    }
};

inline const ColorLuts& colorLuts() {
    static const ColorLuts luts;
    return luts;
}

inline double toLinear(double value) {
    if (value >= 0.0 && value <= 1.0) {
        return colorLuts().decode[static_cast<int>(value * (kColorLutSize - 1) + 0.5)];
    }
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

inline double toSrgb(double value) {
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return colorLuts().encode[static_cast<int>(value * (kColorLutSize - 1) + 0.5)];
}

inline Color linear(Color value) { return {toLinear(value.r), toLinear(value.g), toLinear(value.b)}; }

inline double specularPower42(double value) {
    const double square = value * value;
    const double fourth = square * square;
    const double eighth = fourth * fourth;
    const double sixteenth = eighth * eighth;
    const double thirtySecond = sixteenth * sixteenth;
    return thirtySecond * eighth * square;
}

inline RenderVertex interpolate(const RenderVertex& a, const RenderVertex& b, double t) {
    return {a.view * (1 - t) + b.view * t, a.normal * (1 - t) + b.normal * t,
            a.source * (1 - t) + b.source * t,
            {a.uv.x * (1 - t) + b.uv.x * t, a.uv.y * (1 - t) + b.uv.y * t}};
}

inline Color sample(const Texture* texture, double u, double v) {
    if (!texture || texture->pixels.empty()) return {1, 1, 1};
    u -= std::floor(u);
    v -= std::floor(v);
    const double x = u * (texture->width - 1);
    const double y = (1.0 - v) * (texture->height - 1);
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, texture->width - 1);
    const int y1 = std::min(y0 + 1, texture->height - 1);
    const double fx = x - x0, fy = y - y0;
    auto at = [&](int px, int py) {
        const std::size_t offset = (static_cast<std::size_t>(py) * texture->width + px) * 4;
        return Color{texture->pixels[offset] / 255.0, texture->pixels[offset + 1] / 255.0,
                     texture->pixels[offset + 2] / 255.0};
    };
    return mix(mix(at(x0, y0), at(x1, y0), fx), mix(at(x0, y1), at(x1, y1), fx), fy);
}

inline double alpha(const Texture* texture, double u, double v) {
    if (!texture || texture->pixels.empty()) return 1;
    u -= std::floor(u);
    const int x = std::clamp(static_cast<int>(u * texture->width), 0, texture->width - 1);
    const int y = std::clamp(static_cast<int>((1.0 - v) * texture->height), 0, texture->height - 1);
    return texture->pixels[(static_cast<std::size_t>(y) * texture->width + x) * 4 + 3] / 255.0;
}

struct Projected {
    double x = 0;
    double y = 0;
    double inverseDepth = 0;
    Vec3 view;
    Vec3 normal;
    Vec3 source;
    Vec2 uv;
};

inline bool sameColor(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

inline bool sameMaterial(const Material& a, const Material& b) {
    return a.kind == b.kind && sameColor(a.color, b.color) && sameColor(a.second, b.second) &&
        a.texture == b.texture && a.young == b.young && a.old == b.old &&
        a.normalYoung == b.normalYoung && a.normalOld == b.normalOld && a.age == b.age &&
        a.specular == b.specular && a.unlit == b.unlit && a.constantLighting == b.constantLighting;
}

}  // namespace gtc::native::raster_detail

#endif
