#include "native.h"
#include "hair.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"

namespace gtc::native {
namespace {

struct Color { double r = 0, g = 0, b = 0; };
struct Image {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};
struct Vertex {
    double x = 0, y = 0, inverseDepth = 0;
    Vec3 position;
    Vec3 source;
    Vec3 normal;
    Vec2 uv;
};
struct Material {
    enum Kind { Skin, Solid, Hair, Scalp } kind = Solid;
    Color color;
    double eyeY = 0, eyeZ = 0, hairline = 0.085, headWidth = 0.1;
    const Image* hairTexture = nullptr;
    const Image* young = nullptr;
    const Image* old = nullptr;
    const Image* normalYoung = nullptr;
    const Image* normalOld = nullptr;
    double age = 0;
};

Color operator+(Color a, Color b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
Color operator*(Color a, double b) { return {a.r * b, a.g * b, a.b * b}; }

Color hex(const char* value) {
    const auto channel = [&](int offset) {
        const std::string text(value + offset, 2);
        return std::strtoul(text.c_str(), nullptr, 16) / 255.0;
    };
    return {channel(1), channel(3), channel(5)};
}

double toLinear(double value) { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); }
double toSrgb(double value) { return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(std::max(0.0, value), 1 / 2.4) - 0.055; }
Color linear(Color value) { return {toLinear(value.r), toLinear(value.g), toLinear(value.b)}; }

const Image& image(const std::string& path) {
    static std::map<std::string, Image> cache;
    auto found = cache.find(path);
    if (found != cache.end()) return found->second;
    Image result;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &result.width, &result.height, &channels, 4);
    if (pixels) {
        result.pixels.assign(pixels, pixels + static_cast<std::size_t>(result.width) * result.height * 4);
        stbi_image_free(pixels);
    }
    return cache.emplace(path, std::move(result)).first->second;
}

Color sample(const Image* image, double u, double v) {
    if (!image || image->pixels.empty()) return {1, 1, 1};
    u = u - std::floor(u);
    v = std::clamp(v, 0.0, 1.0);
    const double x = u * (image->width - 1);
    const double y = (1.0 - v) * (image->height - 1);
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, image->width - 1), y1 = std::min(y0 + 1, image->height - 1);
    const double fx = x - x0, fy = y - y0;
    auto at = [&](int px, int py) {
        const std::size_t offset = (static_cast<std::size_t>(py) * image->width + px) * 4;
        return Color{image->pixels[offset] / 255.0, image->pixels[offset + 1] / 255.0, image->pixels[offset + 2] / 255.0};
    };
    const Color a = at(x0, y0) * (1 - fx) + at(x1, y0) * fx;
    const Color b = at(x0, y1) * (1 - fx) + at(x1, y1) * fx;
    return a * (1 - fy) + b * fy;
}

double alpha(const Image* image, double u, double v) {
    if (!image || image->pixels.empty()) return 1;
    u = u - std::floor(u);
    v = std::clamp(v, 0.0, 1.0);
    const int x = std::clamp(static_cast<int>(u * image->width), 0, image->width - 1);
    const int y = std::clamp(static_cast<int>((1 - v) * image->height), 0, image->height - 1);
    return image->pixels[(static_cast<std::size_t>(y) * image->width + x) * 4 + 3] / 255.0;
}

Color mix(Color a, Color b, double amount) { return a * (1 - amount) + b * amount; }

class Rasterizer {
public:
    Rasterizer(int width, int height) : width_(width), height_(height),
        depth_(static_cast<std::size_t>(width) * height, 1e100),
        pixels_(static_cast<std::size_t>(width) * height, linear(hex("#111713"))) {}

    void triangle(const std::array<Vertex, 3>& vertices, const Material& material) {
        const double area = edge(vertices[0], vertices[1], vertices[2]);
        if (std::abs(area) < 1e-9) return;
        int minX = std::max(0, static_cast<int>(std::floor(std::min({vertices[0].x, vertices[1].x, vertices[2].x}))));
        int maxX = std::min(width_ - 1, static_cast<int>(std::ceil(std::max({vertices[0].x, vertices[1].x, vertices[2].x}))));
        int minY = std::max(0, static_cast<int>(std::floor(std::min({vertices[0].y, vertices[1].y, vertices[2].y}))));
        int maxY = std::min(height_ - 1, static_cast<int>(std::ceil(std::max({vertices[0].y, vertices[1].y, vertices[2].y}))));
        if (minX > maxX || minY > maxY) return;
        Vec3 tangent{1, 0, 0}, bitangent{0, 1, 0};
        if (material.kind == Material::Skin) {
            const Vec3 e1 = vertices[1].position - vertices[0].position;
            const Vec3 e2 = vertices[2].position - vertices[0].position;
            const double du1 = vertices[1].uv.x - vertices[0].uv.x, dv1 = vertices[1].uv.y - vertices[0].uv.y;
            const double du2 = vertices[2].uv.x - vertices[0].uv.x, dv2 = vertices[2].uv.y - vertices[0].uv.y;
            const double denominator = du1 * dv2 - du2 * dv1;
            if (std::abs(denominator) > 1e-9) {
                tangent = normalized((e1 * dv2 - e2 * dv1) / denominator);
                bitangent = normalized((e2 * du1 - e1 * du2) / denominator);
            }
        }
        for (int y = minY; y <= maxY; ++y) for (int x = minX; x <= maxX; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double w0 = ((vertices[1].x - px) * (vertices[2].y - py) - (vertices[1].y - py) * (vertices[2].x - px)) / area;
            const double w1 = ((vertices[2].x - px) * (vertices[0].y - py) - (vertices[2].y - py) * (vertices[0].x - px)) / area;
            const double w2 = 1.0 - w0 - w1;
            if (w0 < -1e-7 || w1 < -1e-7 || w2 < -1e-7) continue;
            const double inverseDepth = w0 * vertices[0].inverseDepth + w1 * vertices[1].inverseDepth + w2 * vertices[2].inverseDepth;
            if (inverseDepth <= 1e-9) continue;
            const double depth = 1.0 / inverseDepth;
            const std::size_t pixel = static_cast<std::size_t>(y) * width_ + x;
            if (depth >= depth_[pixel]) continue;
            const double a = w0 * vertices[0].inverseDepth / inverseDepth;
            const double b = w1 * vertices[1].inverseDepth / inverseDepth;
            const double c = w2 * vertices[2].inverseDepth / inverseDepth;
            const Vec3 position = vertices[0].position * a + vertices[1].position * b + vertices[2].position * c;
            const Vec3 source = vertices[0].source * a + vertices[1].source * b + vertices[2].source * c;
            if (material.kind == Material::Scalp) {
                const auto smooth = [](double value) { return value * value * (3 - 2 * value); };
                const double front = smooth(std::clamp((source.z - material.eyeZ * 0.45) /
                    std::max(1e-9, material.eyeZ * 0.50), 0.0, 1.0));
                const double edge = material.eyeY + material.hairline + 0.018 *
                    std::pow(std::clamp(std::abs(source.x) / material.headWidth, 0.0, 1.5), 2.0);
                if (source.y < (-100.0 * (1 - front) + edge * front)) continue;
            }
            Vec3 normal = normalized(vertices[0].normal * a + vertices[1].normal * b + vertices[2].normal * c);
            const Vec2 uv{vertices[0].uv.x * a + vertices[1].uv.x * b + vertices[2].uv.x * c,
                          vertices[0].uv.y * a + vertices[1].uv.y * b + vertices[2].uv.y * c};
            if (material.kind == Material::Hair && alpha(material.hairTexture, uv.x, uv.y) < 0.32) continue;
            Color base = material.color;
            if (material.kind == Material::Skin) {
                const double oldBlend = std::clamp((material.age - 24.0) / 58.0, 0.0, 1.0);
                const double ageBlend = oldBlend * oldBlend * (3 - 2 * oldBlend);
                const Color detail = mix(sample(material.young, uv.x, uv.y), sample(material.old, uv.x, uv.y), ageBlend * 0.94);
                base = linear({material.color.r * detail.r, material.color.g * detail.g, material.color.b * detail.b});
                const Color nm = mix(sample(material.normalYoung, uv.x, uv.y), sample(material.normalOld, uv.x, uv.y), ageBlend);
                normal = normalized(normal + tangent * ((nm.r * 2 - 1) * 0.42) + bitangent * ((nm.g * 2 - 1) * 0.42));
            } else if (material.kind == Material::Hair) {
                const Color detail = sample(material.hairTexture, uv.x, uv.y);
                base = {material.color.r * toLinear(detail.r), material.color.g * toLinear(detail.g), material.color.b * toLinear(detail.b)};
            } else {
                base = linear(base);
            }
            const Vec3 view = normalized(Vec3{0, 0, 1.75} - position);
            if (dot(normal, view) < 0) normal = normal * -1.0;
            const Vec3 key = normalized(Vec3{-3, 6, 5});
            const Vec3 fill = normalized(Vec3{4, 3, 1});
            const Vec3 rim = normalized(Vec3{0, 4, -5});
            const double diffuse = 0.30 + 0.72 * std::max(0.0, dot(normal, key)) +
                0.24 * std::max(0.0, dot(normal, fill)) + 0.14 * std::max(0.0, dot(normal, rim));
            const Vec3 halfway = normalized(key + view);
            const double specular = std::pow(std::max(0.0, dot(normal, halfway)), material.kind == Material::Skin ? 28 : 48) *
                (material.kind == Material::Skin ? 0.035 : 0.12);
            pixels_[pixel] = base * diffuse + Color{specular, specular, specular};
            depth_[pixel] = depth;
        }
    }

    bool save(const std::string& path) {
        std::vector<unsigned char> bytes(pixels_.size() * 3);
        for (std::size_t i = 0; i < pixels_.size(); ++i) {
            const Color color = pixels_[i] * 1.08;
            const auto tone = [](double x) { return std::clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); };
            bytes[i * 3] = static_cast<unsigned char>(std::round(std::clamp(toSrgb(tone(color.r)), 0.0, 1.0) * 255));
            bytes[i * 3 + 1] = static_cast<unsigned char>(std::round(std::clamp(toSrgb(tone(color.g)), 0.0, 1.0) * 255));
            bytes[i * 3 + 2] = static_cast<unsigned char>(std::round(std::clamp(toSrgb(tone(color.b)), 0.0, 1.0) * 255));
        }
        return stbi_write_png(path.c_str(), width_, height_, 3, bytes.data(), width_ * 3) != 0;
    }

private:
    static double edge(const Vertex& a, const Vertex& b, const Vertex& c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    }
    int width_, height_;
    std::vector<double> depth_;
    std::vector<Color> pixels_;
};

struct Camera {
    Pose pose;
    Vec3 center;
    int width;
    int height;
    double focal;

    Vec3 rotate(Vec3 point) const {
        point = point - center;
        const double cy = std::cos(pose.yaw), sy = std::sin(pose.yaw);
        const double cp = std::cos(pose.pitch), sp = std::sin(pose.pitch);
        const double cr = std::cos(pose.roll), sr = std::sin(pose.roll);
        const double x = cy * point.x + sy * point.z;
        const double z = -sy * point.x + cy * point.z;
        const double y = cp * point.y - sp * z;
        const double depth = sp * point.y + cp * z;
        return {cr * x - sr * y, sr * x + cr * y, depth};
    }
    Vertex vertex(Vec3 point, Vec3 normal, Vec2 uv) const {
        const Vec3 rotated = rotate(point);
        const double depth = std::max(0.03, pose.distance - rotated.z);
        Vertex result;
        result.x = width * 0.5 + focal * rotated.x / depth;
        result.y = height * 0.5 - focal * rotated.y / depth;
        result.inverseDepth = 1.0 / depth;
        result.position = rotated;
        result.source = point;
        result.normal = normalized(rotate(normal + center));
        result.uv = uv;
        return result;
    }
};

void sphere(Rasterizer& rasterizer, const Camera& camera, Vec3 center, double radius, double depth,
            Vec3 direction, const Material& material, int segments = 20, int rings = 12) {
    direction = normalized(direction);
    Vec3 u = cross(direction, {0, 1, 0});
    if (length(u) < 0.01) u = cross(direction, {1, 0, 0});
    u = normalized(u);
    const Vec3 v = normalized(cross(u, direction));
    for (int ring = 0; ring < rings; ++ring) for (int segment = 0; segment < segments; ++segment) {
        const double t0 = 3.141592653589793 * ring / rings;
        const double t1 = 3.141592653589793 * (ring + 1) / rings;
        const double p0 = 2 * 3.141592653589793 * segment / segments;
        const double p1 = 2 * 3.141592653589793 * (segment + 1) / segments;
        auto point = [&](double t, double p) {
            return center + u * (radius * std::sin(t) * std::cos(p)) +
                v * (radius * std::sin(t) * std::sin(p)) + direction * (depth * std::cos(t));
        };
        const Vec3 a = point(t0, p0), b = point(t1, p0), c = point(t1, p1), d = point(t0, p1);
        const Vec3 na = normalized(a - center), nb = normalized(b - center), nc = normalized(c - center), nd = normalized(d - center);
        rasterizer.triangle({camera.vertex(a, na, {}), camera.vertex(b, nb, {}), camera.vertex(c, nc, {})}, material);
        rasterizer.triangle({camera.vertex(a, na, {}), camera.vertex(c, nc, {}), camera.vertex(d, nd, {})}, material);
    }
}

bool scalpTriangle(const HumanModel& model, const Triangle& triangle, int style) {
    if (style <= 0 || style > 8) return false;
    const Vec3 point = (model.vertices[triangle.v[0]] + model.vertices[triangle.v[1]] + model.vertices[triangle.v[2]]) / 3.0;
    const double x = point.x, y = point.y, z = point.z, h = model.headShift;
    const double receded = 7.72 + h + 0.12 * std::min(1.0, std::abs(x) / 0.8) + 0.06 * std::max(0.0, z - 1.0);
    const double swept = 7.57 + h + 0.15 * std::max(0.0, x) + 0.06 * std::max(0.0, z - 1.0);
    const bool masks[] = {
        y > 7.62 + h + 0.10 * std::abs(x) || (z < 0.38 && y > 6.98 + h) || (std::abs(x) > 0.77 && z < 0.60 && y > 7.22 + h),
        y > receded || (z < 0.42 && y > 6.82 + h) || (std::abs(x) > 0.76 && z < 0.55 && y > 7.02 + h),
        y > 7.48 + h + 0.10 * std::abs(x) || (z < 0.64 && y > 6.10 + h) || (std::abs(x) > 0.70 && z < 0.93 && y > 6.35 + h),
        y > swept || (z < 0.35 && y > 6.88 + h) || (x < -0.55 && z < 0.85 && y > 7.04 + h),
        y > 7.50 + h + 0.10 * std::abs(x) || (z < 0.62 && y > 6.45 + h) || (std::abs(x) > 0.68 && z < 0.90 && y > 6.55 + h),
        y > 7.48 + h + 0.10 * std::abs(x) || (z < 0.70 && y > 5.75 + h) || (std::abs(x) > 0.72 && z < 0.95 && y > 5.95 + h),
        y > 7.78 + h + 0.04 * std::abs(x) || (z < 0.32 && y > 7.15 + h) || (std::abs(x) > 0.78 && z < 0.42 && y > 7.35 + h),
        y > 7.96 + h + 0.05 * std::abs(x) || (z < 0.30 && y > 6.95 + h) || (std::abs(x) > 0.80 && z < 0.48 && y > 7.25 + h)
    };
    return masks[style - 1];
}

void drawHair(Rasterizer& rasterizer, const Camera& camera, const HumanModel& model,
              const Preset& preset, const HairStyle& style, const Material& material) {
    const std::vector<Vec3> mapped = model.shape(style.required, preset.morphs);
    std::unordered_map<int, Vec3> refs;
    for (std::size_t i = 0; i < style.required.size(); ++i) refs[style.required[i]] = mapped[i];
    double ratios[3] = {1, 1, 1};
    for (int axis = 0; axis < 3; ++axis) {
        if (!style.scaleValid[axis]) continue;
        const Vec3 a = refs[style.scaleRefs[axis][0]];
        const Vec3 b = refs[style.scaleRefs[axis][1]];
        const double distance = axis == 0 ? std::abs(a.x - b.x) : axis == 1 ? std::abs(a.y - b.y) : std::abs(a.z - b.z);
        ratios[axis] = distance / std::max(1e-9, style.scaleDefaults[axis] * model.scale);
    }
    std::vector<Vec3> positions;
    positions.reserve(style.vertices.size());
    for (const HairVertex& vertex : style.vertices) {
        Vec3 point = vertex.offset * model.scale;
        point.x *= ratios[0]; point.y *= ratios[1]; point.z *= ratios[2];
        for (int anchor = 0; anchor < 3; ++anchor) point += refs[vertex.refs[anchor]] * vertex.weights[anchor];
        positions.push_back(point);
    }
    const std::vector<Vec3> head = model.shape(model.headVertices, preset.morphs);
    Vec3 low{1e100, 1e100, 1e100}, high{-1e100, -1e100, -1e100};
    for (Vec3 point : head) {
        low.x = std::min(low.x, point.x); low.y = std::min(low.y, point.y); low.z = std::min(low.z, point.z);
        high.x = std::max(high.x, point.x); high.y = std::max(high.y, point.y); high.z = std::max(high.z, point.z);
    }
    const Vec3 center = (low + high) * 0.5;
    const Vec3 eyes[2] = {model.shapedJoint(model.eyeSource[0], preset.morphs),
                          model.shapedJoint(model.eyeSource[1], preset.morphs)};
    const auto clamp01 = [](double value) { return std::clamp(value, 0.0, 1.0); };
    for (Vec3& point : positions) {
        const double rawY = point.y / model.scale - model.ground;
        const double rawZ = point.z / model.scale;
        if (style.receded) {
            const double front = clamp01((rawZ - 0.30) / 1.1);
            const double lowAmount = clamp01((8.75 - rawY) / 1.3);
            point.y += front * lowAmount * 0.12;
        } else if (style.key == "waves" || style.key == "long-bangs") {
            const double front = clamp01((rawZ - 0.45) / 0.85);
            const double lowAmount = clamp01((8.85 - rawY) / 1.25);
            point.y += front * lowAmount * 0.045;
        }
    }
    if (style.key == "waves") {
        for (Vec3& point : positions) {
            point = {center.x + (point.x - center.x) * 0.74,
                     center.y + (point.y - center.y) * 0.85,
                     center.z + (point.z - center.z) * 0.74};
        }
    } else if (style.key == "short01" || style.key == "short02" || style.key == "short04") {
        const double top = high.y - 0.035;
        const double factor = style.key == "short04" ? 1.15 : 0.48;
        for (Vec3& point : positions) if (point.y > top) point.y = top + (point.y - top) * factor;
    }
    if (style.key == "wavy-bob" || style.key == "curly-bob") {
        const double factor = style.key == "wavy-bob" ? 0.84 : 0.90;
        for (Vec3& point : positions) {
            point.x = center.x + (point.x - center.x) * factor;
            point.z = center.z + (point.z - center.z) * factor;
        }
    }
    if (style.key == "long-bangs") {
        const double fringeTop = eyes[0].y + 0.12;
        for (Vec3& point : positions) {
            const double front = clamp01((point.z - 0.11) / 0.05);
            const double lower = clamp01((fringeTop - point.y) / 0.10);
            const double side = clamp01((point.x + 0.09) / 0.18);
            const double amount = front * lower;
            point.x -= amount * 0.025;
            point.y += amount * side * 0.035;
            point.z -= amount * 0.008;
            const double length = clamp01((eyes[0].y - 0.03 - point.y) / 0.22);
            const double edge = clamp01((std::abs(point.x) - 0.055) / 0.045);
            const double phase = point.y * 55 + (point.x < 0 ? 0.8 : 0);
            point.x += (point.x < 0 ? -1 : 1) * std::sin(phase) * length * edge * 0.025;
            point.z += std::cos(phase) * length * edge * 0.018;
        }
    }
    if (style.key == "wavy-bob") {
        const double top = high.y + 0.005;
        for (Vec3& point : positions) {
            if (point.y > top) point.y = top + (point.y - top) * 0.78;
            const double lower = clamp01((eyes[0].y - point.y + 0.02) / 0.16);
            const double front = clamp01((point.z - eyes[0].z * 0.65) / (eyes[0].z * 0.5));
            const double side = clamp01((std::abs(point.x) - 0.035) / 0.07);
            const double amount = lower * front * side;
            point.x += (point.x < 0 ? -1 : 1) * amount * 0.018;
            point.z -= amount * 0.07;
        }
    }
    if (style.key == "fifties-updo") {
        const double crown = high.y - 0.025;
        for (Vec3& point : positions) if (point.y > crown) point.y = crown + (point.y - crown) * 0.65;
    }
    if (style.key == "long-waves" || style.key == "curly-bob" || style.key == "wavy-bob") {
        const double lift = style.key == "wavy-bob" ? 0.02 : 0.065;
        for (Vec3& point : positions) {
            const double front = clamp01((point.z - eyes[0].z * 0.68) / (eyes[0].z * 0.45));
            const double band = clamp01(1 - std::abs(point.y - eyes[0].y - 0.025) / 0.075);
            point.y += front * band * lift;
            point.z -= front * band * 0.018;
            if (style.key == "wavy-bob") {
                const double side = clamp01((std::abs(point.x) - 0.025) / 0.04);
                const double eyeBand = clamp01(1 - std::abs(point.y - eyes[0].y - 0.005) / 0.055);
                point.y += front * side * eyeBand * 0.025;
                point.z -= front * side * eyeBand * 0.055;
            }
        }
    }
    std::vector<Vec3> normals(positions.size());
    for (const auto& triangle : style.triangles) {
        const Vec3 normal = cross(positions[triangle[1]] - positions[triangle[0]], positions[triangle[2]] - positions[triangle[0]]);
        for (int index : triangle) normals[index] += normal;
    }
    for (auto& normal : normals) normal = normalized(normal);
    for (const auto& triangle : style.triangles) {
        std::array<Vertex, 3> vertices;
        for (int corner = 0; corner < 3; ++corner) {
            const int index = triangle[corner];
            vertices[corner] = camera.vertex(positions[index], normals[index], style.vertices[index].uv);
        }
        rasterizer.triangle(vertices, material);
    }
}

} // namespace

bool renderPortrait(const HumanModel& model, const Preset& preset, const std::string& root,
                    const std::string& output, int width, int height, std::string& error) {
    if (width != 480 || height != 640) {
        error = "Native portrait review is fixed at 480x640";
        return false;
    }
    std::set<int> ids;
    for (const auto& triangle : model.headTriangles) for (int index : triangle.v) ids.insert(index);
    std::vector<int> vertexIds(ids.begin(), ids.end());
    const std::vector<Vec3> positions = model.shape(vertexIds, preset.morphs);
    std::unordered_map<int, int> local;
    for (std::size_t i = 0; i < vertexIds.size(); ++i) local[vertexIds[i]] = static_cast<int>(i);
    std::vector<Vec3> normals(positions.size());
    for (const auto& triangle : model.headTriangles) {
        const int a = local[triangle.v[0]], b = local[triangle.v[1]], c = local[triangle.v[2]];
        const Vec3 normal = cross(positions[b] - positions[a], positions[c] - positions[a]);
        normals[a] += normal; normals[b] += normal; normals[c] += normal;
    }
    for (auto& normal : normals) normal = normalized(normal);
    Camera camera{preset.pose, model.headCenter, width, height,
                  height / (2.0 * std::tan(preset.pose.fovDegrees * 3.141592653589793 / 360.0))};
    Rasterizer rasterizer(width, height);
    Material skin;
    skin.kind = Material::Skin;
    static const char* colors[] = {"#f0c8a8", "#d9a17d", "#b97c58", "#8a573e", "#714a38", "#e5b894"};
    skin.color = hex(colors[std::clamp(preset.skin, 0, 5)]);
    const std::string sex = model.male ? "male" : "female";
    skin.young = &image(root + "/web/assets/human/skin-" + sex + "-young-detail.jpg");
    skin.old = &image(root + "/web/assets/human/skin-" + sex + "-old-detail.jpg");
    skin.normalYoung = &image(root + "/web/assets/human/skin-" + sex + "-young-normal.png");
    skin.normalOld = &image(root + "/web/assets/human/skin-" + sex + "-old-normal.png");
    const int ageIndex = model.specIndex("age");
    skin.age = ageIndex >= 0 ? preset.morphs[ageIndex] : 0;
    for (const auto& triangle : model.headTriangles) {
        std::array<Vertex, 3> vertices;
        for (int corner = 0; corner < 3; ++corner) {
            const int index = local[triangle.v[corner]];
            vertices[corner] = camera.vertex(positions[index], normals[index], model.texcoords[triangle.uv[corner]]);
        }
        rasterizer.triangle(vertices, skin);
    }
    static const char* hairColors[] = {"#171a1c", "#3b281d", "#75604a", "#b5a075", "#5b6268", "#69362c", "#e5d5aa", "#d2b477", "#a9adb0"};
    static const double boosts[] = {0.9, 1.1, 1.0, 1.0, 1.3, 1.1, 1.6, 1.5, 1.3};
    const int colorIndex = std::clamp(preset.hairColor, 0, 8);
    int capStyle = preset.hair == 8 ? 0 : preset.hair;
    if (preset.hair == 9 || preset.hair == 10) capStyle = 3;
    if (preset.hair == 11 || preset.hair == 13 || preset.hair == 15) capStyle = 5;
    if (preset.hair == 12 || preset.hair == 14) capStyle = 6;
    if (capStyle > 0) {
        const Vec3 scalpEye = model.shapedJoint(model.eyeSource[0], preset.morphs);
        double headWidth = 0.06;
        for (const Vec3& point : positions) headWidth = std::max(headWidth, std::abs(point.x));
        Material scalp;
        scalp.kind = Material::Scalp;
        scalp.eyeY = scalpEye.y;
        scalp.eyeZ = scalpEye.z;
        scalp.hairline = capStyle == 5 ? 0.070 : 0.085;
        scalp.headWidth = headWidth;
        scalp.color = hex(hairColors[colorIndex]) * std::min(1.0, boosts[colorIndex] * 0.5);
        for (const auto& triangle : model.headTriangles) {
            if (!scalpTriangle(model, triangle, capStyle)) continue;
            std::array<Vertex, 3> vertices;
            for (int corner = 0; corner < 3; ++corner) {
                const int index = local[triangle.v[corner]];
                const Vec3 offset = normals[index] * 0.0024;
                vertices[corner] = camera.vertex(positions[index] + offset, normals[index], model.texcoords[triangle.uv[corner]]);
            }
            rasterizer.triangle(vertices, scalp);
        }
    }
    static std::map<std::string, HairLibrary> hairLibraries;
    auto library = hairLibraries.find(root);
    if (library == hairLibraries.end()) {
        HairLibrary loaded;
        if (!loaded.load(root, model, error)) return false;
        library = hairLibraries.emplace(root, std::move(loaded)).first;
    }
    if (const HairStyle* style = library->second.find(preset.hair)) {
        Material hair;
        hair.kind = Material::Hair;
        hair.color = hex(hairColors[colorIndex]) * boosts[colorIndex];
        hair.hairTexture = &image(style->texture);
        drawHair(rasterizer, camera, model, preset, *style, hair);
    }
    const int eyeSizeIndex = model.specIndex("eyeSize");
    const double eyeSize = std::clamp(1.0 + (eyeSizeIndex >= 0 ? preset.morphs[eyeSizeIndex] : 0) * 0.0015, 0.65, 1.35);
    static const char* eyeColors[] = {"#34271f", "#6a4d2c", "#496b82", "#4c6854", "#777d82", "#171b1d"};
    Material white; white.color = hex("#d8d5ca");
    Material iris; iris.color = hex(eyeColors[std::clamp(preset.eyeColor, 0, 5)]);
    Material pupil; pupil.color = hex("#090c0d");
    for (int side = 0; side < 2; ++side) {
        const Vec3 center = model.shapedJoint(model.eyeSource[side], preset.morphs);
        const Vec3 direction = model.eyeDirection[side];
        sphere(rasterizer, camera, center, 0.011 * eyeSize, 0.011 * eyeSize, direction, white);
        sphere(rasterizer, camera, center + direction * (0.010 * eyeSize), 0.0054 * eyeSize,
               0.0016 * eyeSize, direction, iris, 16, 8);
        sphere(rasterizer, camera, center + direction * (0.0114 * eyeSize), 0.0022 * eyeSize,
               0.001 * eyeSize, direction, pupil, 12, 6);
    }
    std::error_code code;
    const std::filesystem::path path(output);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), code);
    if (!rasterizer.save(output)) {
        error = "Cannot write native PNG " + output;
        return false;
    }
    return true;
}

} // namespace gtc::native
