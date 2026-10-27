#include "client/raster.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"

namespace gtc::native {
namespace {

double toLinear(double value) {
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double toSrgb(double value) {
    return value <= 0.0031308 ? value * 12.92 :
        1.055 * std::pow(std::max(0.0, value), 1.0 / 2.4) - 0.055;
}

Color linear(Color value) { return {toLinear(value.r), toLinear(value.g), toLinear(value.b)}; }

RenderVertex interpolate(const RenderVertex& a, const RenderVertex& b, double t) {
    return {a.view * (1 - t) + b.view * t, a.normal * (1 - t) + b.normal * t,
            a.source * (1 - t) + b.source * t,
            {a.uv.x * (1 - t) + b.uv.x * t, a.uv.y * (1 - t) + b.uv.y * t}};
}

Color sample(const Texture* texture, double u, double v) {
    if (!texture || texture->pixels.empty()) return {1, 1, 1};
    u -= std::floor(u);
    v = std::clamp(v, 0.0, 1.0);
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

double alpha(const Texture* texture, double u, double v) {
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

} // namespace

Color operator+(Color a, Color b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
Color operator*(Color a, double b) { return {a.r * b, a.g * b, a.b * b}; }
Color mix(Color a, Color b, double amount) { return a * (1 - amount) + b * amount; }

Color colorHex(const char* value) {
    const auto channel = [&](int offset) {
        return std::strtoul(std::string(value + offset, 2).c_str(), nullptr, 16) / 255.0;
    };
    return linear({channel(1), channel(3), channel(5)});
}

const Texture& loadTexture(const std::string& path) {
    static std::map<std::string, Texture> cache;
    auto found = cache.find(path);
    if (found != cache.end()) return found->second;
    Texture texture;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &texture.width, &texture.height, &channels, 4);
    if (pixels) {
        texture.pixels.assign(pixels, pixels + static_cast<std::size_t>(texture.width) * texture.height * 4);
        stbi_image_free(pixels);
    }
    return cache.emplace(path, std::move(texture)).first->second;
}

bool NativeFrame::save(const std::string& path) const {
    std::error_code error;
    const std::filesystem::path destination(path);
    if (destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path(), error);
    return stbi_write_png(path.c_str(), width, height, 3, rgb.data(), width * 3) != 0;
}

void NativeFrame::rectangle(int x, int y, int w, int h, Color color) {
    const int r = static_cast<int>(std::clamp(toSrgb(color.r), 0.0, 1.0) * 255);
    const int g = static_cast<int>(std::clamp(toSrgb(color.g), 0.0, 1.0) * 255);
    const int b = static_cast<int>(std::clamp(toSrgb(color.b), 0.0, 1.0) * 255);
    for (int py = std::max(0, y); py < std::min(height, y + h); ++py) {
        for (int px = std::max(0, x); px < std::min(width, x + w); ++px) {
            const std::size_t offset = (static_cast<std::size_t>(py) * width + px) * 3;
            rgb[offset] = static_cast<unsigned char>(r);
            rgb[offset + 1] = static_cast<unsigned char>(g);
            rgb[offset + 2] = static_cast<unsigned char>(b);
        }
    }
}

Vec3 Camera::view(Vec3 point) const {
    const Vec3 forward = normalized(target - position);
    const Vec3 right = normalized(cross(forward, up));
    const Vec3 vertical = normalized(cross(right, forward));
    const Vec3 relative = point - position;
    return {dot(relative, right), dot(relative, vertical), dot(relative, forward)};
}

Vec3 Camera::viewDirection(Vec3 direction) const {
    const Vec3 forward = normalized(target - position);
    const Vec3 right = normalized(cross(forward, up));
    const Vec3 vertical = normalized(cross(right, forward));
    return {dot(direction, right), dot(direction, vertical), dot(direction, forward)};
}

double Camera::focal() const {
    return height / (2.0 * std::tan(fovDegrees * 3.141592653589793 / 360.0));
}

RenderVertex cameraVertex(const Camera& camera, Vec3 point, Vec3 normal, Vec3 source, Vec2 uv) {
    return {camera.view(point), normalized(camera.viewDirection(normal)), source, uv};
}

Rasterizer::Rasterizer(int width, int height, Color background, double focal) : width_(width), height_(height), focal_(focal),
    depth_(static_cast<std::size_t>(width) * height, 1e100),
    pixels_(static_cast<std::size_t>(width) * height, background) {}

void Rasterizer::triangle(const std::array<RenderVertex, 3>& input, const Material& material) {
    constexpr double near = 0.035;
    std::vector<RenderVertex> polygon;
    for (int i = 0; i < 3; ++i) {
        const RenderVertex& a = input[i];
        const RenderVertex& b = input[(i + 1) % 3];
        const bool insideA = a.view.z >= near, insideB = b.view.z >= near;
        if (insideA) polygon.push_back(a);
        if (insideA != insideB) polygon.push_back(interpolate(a, b, (near - a.view.z) / (b.view.z - a.view.z)));
    }
    for (std::size_t face = 1; face + 1 < polygon.size(); ++face) {
        const std::array<RenderVertex, 3> vertices = {polygon[0], polygon[face], polygon[face + 1]};
        std::array<Projected, 3> p;
        for (int i = 0; i < 3; ++i) {
            p[i].x = width_ * 0.5 + focal_ * vertices[i].view.x / vertices[i].view.z;
            p[i].y = height_ * 0.5 - focal_ * vertices[i].view.y / vertices[i].view.z;
            p[i].inverseDepth = 1.0 / vertices[i].view.z;
            p[i].view = vertices[i].view;
            p[i].normal = vertices[i].normal;
            p[i].source = vertices[i].source;
            p[i].uv = vertices[i].uv;
        }
        const double area = (p[1].x - p[0].x) * (p[2].y - p[0].y) -
                            (p[1].y - p[0].y) * (p[2].x - p[0].x);
        if (std::abs(area) < 1e-9) continue;
        const int minX = std::max(0, static_cast<int>(std::floor(std::min({p[0].x, p[1].x, p[2].x}))));
        const int maxX = std::min(width_ - 1, static_cast<int>(std::ceil(std::max({p[0].x, p[1].x, p[2].x}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({p[0].y, p[1].y, p[2].y}))));
        const int maxY = std::min(height_ - 1, static_cast<int>(std::ceil(std::max({p[0].y, p[1].y, p[2].y}))));
        Vec3 tangent{1, 0, 0}, bitangent{0, 1, 0};
        if (material.kind == Material::Kind::Skin) {
            const Vec3 e1 = p[1].view - p[0].view, e2 = p[2].view - p[0].view;
            const double du1 = p[1].uv.x - p[0].uv.x, dv1 = p[1].uv.y - p[0].uv.y;
            const double du2 = p[2].uv.x - p[0].uv.x, dv2 = p[2].uv.y - p[0].uv.y;
            const double denominator = du1 * dv2 - du2 * dv1;
            if (std::abs(denominator) > 1e-9) {
                tangent = normalized((e1 * dv2 - e2 * dv1) / denominator);
                bitangent = normalized((e2 * du1 - e1 * du2) / denominator);
            }
        }
        for (int y = minY; y <= maxY; ++y) for (int x = minX; x <= maxX; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double w0 = ((p[1].x - px) * (p[2].y - py) - (p[1].y - py) * (p[2].x - px)) / area;
            const double w1 = ((p[2].x - px) * (p[0].y - py) - (p[2].y - py) * (p[0].x - px)) / area;
            const double w2 = 1 - w0 - w1;
            if (w0 < -1e-7 || w1 < -1e-7 || w2 < -1e-7) continue;
            const double inverse = w0 * p[0].inverseDepth + w1 * p[1].inverseDepth + w2 * p[2].inverseDepth;
            if (inverse <= 1e-9) continue;
            const double depth = 1 / inverse;
            const std::size_t pixel = static_cast<std::size_t>(y) * width_ + x;
            if (depth >= depth_[pixel]) continue;
            const double a = w0 * p[0].inverseDepth / inverse;
            const double b = w1 * p[1].inverseDepth / inverse;
            const double c = w2 * p[2].inverseDepth / inverse;
            const Vec2 uv{p[0].uv.x * a + p[1].uv.x * b + p[2].uv.x * c,
                          p[0].uv.y * a + p[1].uv.y * b + p[2].uv.y * c};
            if (material.kind == Material::Kind::Hair && alpha(material.texture, uv.x, uv.y) < 0.32) continue;
            const Vec3 view = p[0].view * a + p[1].view * b + p[2].view * c;
            Vec3 normal = normalized(p[0].normal * a + p[1].normal * b + p[2].normal * c);
            Color base = material.color;
            if (material.kind == Material::Kind::Floor) {
                const int tile = (static_cast<int>(std::floor(p[0].source.x * a + p[1].source.x * b + p[2].source.x * c)) +
                                  static_cast<int>(std::floor(p[0].source.z * a + p[1].source.z * b + p[2].source.z * c))) & 1;
                base = tile ? material.second : material.color;
            } else if (material.kind == Material::Kind::Skin) {
                const double old = std::clamp((material.age - 24) / 58, 0.0, 1.0);
                const double blend = old * old * (3 - 2 * old);
                const Color detail = mix(linear(sample(material.young, uv.x, uv.y)),
                                         linear(sample(material.old, uv.x, uv.y)), blend * 0.94);
                base = {base.r * detail.r, base.g * detail.g, base.b * detail.b};
                const Color nm = mix(sample(material.normalYoung, uv.x, uv.y),
                                     sample(material.normalOld, uv.x, uv.y), blend);
                normal = normalized(normal + tangent * ((nm.r * 2 - 1) * 0.32) +
                                    bitangent * ((nm.g * 2 - 1) * 0.32));
            } else if (material.kind == Material::Kind::Hair) {
                const Color detail = linear(sample(material.texture, uv.x, uv.y));
                base = {base.r * detail.r, base.g * detail.g, base.b * detail.b};
            }
            const Vec3 toward = normalized(view * -1.0);
            if (dot(normal, toward) < 0) normal = normal * -1.0;
            const Vec3 key = normalized(Vec3{-0.45, 0.78, 0.55});
            const Vec3 fill = normalized(Vec3{0.65, 0.25, 0.35});
            const double diffuse = 0.30 + 0.72 * std::max(0.0, dot(normal, key)) +
                                   0.20 * std::max(0.0, dot(normal, fill));
            const double specular = std::pow(std::max(0.0, dot(normal, normalized(key + toward))), 42) * material.specular;
            pixels_[pixel] = base * diffuse + Color{specular, specular, specular};
            depth_[pixel] = depth;
        }
    }
}

NativeFrame Rasterizer::finish() const {
    NativeFrame frame;
    frame.width = width_;
    frame.height = height_;
    frame.rgb.resize(pixels_.size() * 3);
    const auto tone = [](double x) {
        return std::clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    };
    for (std::size_t i = 0; i < pixels_.size(); ++i) {
        for (int channel = 0; channel < 3; ++channel) {
            const double value = channel == 0 ? pixels_[i].r : channel == 1 ? pixels_[i].g : pixels_[i].b;
            frame.rgb[i * 3 + channel] = static_cast<unsigned char>(std::clamp(toSrgb(tone(value)), 0.0, 1.0) * 255);
        }
    }
    return frame;
}

void drawBox(Rasterizer& rasterizer, const Camera& camera, Vec3 center, Vec3 size,
             const Material& material) {
    const Vec3 half = size * 0.5;
    const Vec3 points[8] = {
        center + Vec3{-half.x, -half.y, -half.z}, center + Vec3{half.x, -half.y, -half.z},
        center + Vec3{half.x, half.y, -half.z}, center + Vec3{-half.x, half.y, -half.z},
        center + Vec3{-half.x, -half.y, half.z}, center + Vec3{half.x, -half.y, half.z},
        center + Vec3{half.x, half.y, half.z}, center + Vec3{-half.x, half.y, half.z}};
    const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
    const Vec3 normals[6] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    for (int face = 0; face < 6; ++face) {
        const RenderVertex a = cameraVertex(camera, points[faces[face][0]], normals[face], points[faces[face][0]], {0, 0});
        const RenderVertex b = cameraVertex(camera, points[faces[face][1]], normals[face], points[faces[face][1]], {1, 0});
        const RenderVertex c = cameraVertex(camera, points[faces[face][2]], normals[face], points[faces[face][2]], {1, 1});
        const RenderVertex d = cameraVertex(camera, points[faces[face][3]], normals[face], points[faces[face][3]], {0, 1});
        rasterizer.triangle({a, b, c}, material);
        rasterizer.triangle({a, c, d}, material);
    }
}

void drawEllipsoid(Rasterizer& rasterizer, const Camera& camera, Vec3 center, Vec3 radii,
                   const Material& material, int segments, int rings) {
    constexpr double pi = 3.141592653589793;
    auto point = [&](int ring, int segment) {
        const double t = pi * ring / rings, p = 2 * pi * segment / segments;
        return center + Vec3{radii.x * std::sin(t) * std::cos(p), radii.y * std::cos(t),
                             radii.z * std::sin(t) * std::sin(p)};
    };
    for (int ring = 0; ring < rings; ++ring) for (int segment = 0; segment < segments; ++segment) {
        const Vec3 a = point(ring, segment), b = point(ring + 1, segment);
        const Vec3 c = point(ring + 1, segment + 1), d = point(ring, segment + 1);
        auto normal = [&](Vec3 value) {
            return normalized(Vec3{(value.x - center.x) / (radii.x * radii.x),
                                   (value.y - center.y) / (radii.y * radii.y),
                                   (value.z - center.z) / (radii.z * radii.z)});
        };
        const RenderVertex va = cameraVertex(camera, a, normal(a), a, {0, 0});
        const RenderVertex vb = cameraVertex(camera, b, normal(b), b, {0, 1});
        const RenderVertex vc = cameraVertex(camera, c, normal(c), c, {1, 1});
        const RenderVertex vd = cameraVertex(camera, d, normal(d), d, {1, 0});
        rasterizer.triangle({va, vb, vc}, material);
        rasterizer.triangle({va, vc, vd}, material);
    }
}

} // namespace gtc::native
