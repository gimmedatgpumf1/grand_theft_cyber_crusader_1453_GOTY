#include "client/raster.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"

namespace gtc::native {
namespace {

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

const ColorLuts& colorLuts() {
    static const ColorLuts luts;
    return luts;
}

double toLinear(double value) {
    if (value >= 0.0 && value <= 1.0) {
        return colorLuts().decode[static_cast<int>(value * (kColorLutSize - 1) + 0.5)];
    }
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double toSrgb(double value) {
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return colorLuts().encode[static_cast<int>(value * (kColorLutSize - 1) + 0.5)];
}

Color linear(Color value) { return {toLinear(value.r), toLinear(value.g), toLinear(value.b)}; }

double specularPower42(double value) {
    const double square = value * value;
    const double fourth = square * square;
    const double eighth = fourth * fourth;
    const double sixteenth = eighth * eighth;
    const double thirtySecond = sixteenth * sixteenth;
    return thirtySecond * eighth * square;
}

RenderVertex interpolate(const RenderVertex& a, const RenderVertex& b, double t) {
    return {a.view * (1 - t) + b.view * t, a.normal * (1 - t) + b.normal * t,
            a.source * (1 - t) + b.source * t,
            {a.uv.x * (1 - t) + b.uv.x * t, a.uv.y * (1 - t) + b.uv.y * t}};
}

Color sample(const Texture* texture, double u, double v) {
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

bool sameColor(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

bool sameMaterial(const Material& a, const Material& b) {
    return a.kind == b.kind && sameColor(a.color, b.color) && sameColor(a.second, b.second) &&
        a.texture == b.texture && a.young == b.young && a.old == b.old &&
        a.normalYoung == b.normalYoung && a.normalOld == b.normalOld && a.age == b.age &&
        a.specular == b.specular && a.unlit == b.unlit && a.constantLighting == b.constantLighting;
}

GpuRenderer activeGpuRenderer = nullptr;

} // namespace

void setGpuRenderer(GpuRenderer renderer) { activeGpuRenderer = renderer; }

bool renderGpuScene(const GpuScene& scene, NativeFrame& frame) {
    return activeGpuRenderer && activeGpuRenderer(scene, frame);
}

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

void NativeFrame::logicalUi(int logicalWidth, int logicalHeight) {
    uiScale = std::min(width / static_cast<double>(logicalWidth), height / static_cast<double>(logicalHeight));
    uiOffsetX = (width - logicalWidth * uiScale) * 0.5;
    uiOffsetY = (height - logicalHeight * uiScale) * 0.5;
}

void NativeFrame::rectangle(int x, int y, int w, int h, Color color) {
    const int r = static_cast<int>(std::clamp(toSrgb(color.r), 0.0, 1.0) * 255);
    const int g = static_cast<int>(std::clamp(toSrgb(color.g), 0.0, 1.0) * 255);
    const int b = static_cast<int>(std::clamp(toSrgb(color.b), 0.0, 1.0) * 255);
    const int left = static_cast<int>(std::lround(uiOffsetX + x * uiScale));
    const int top = static_cast<int>(std::lround(uiOffsetY + y * uiScale));
    const int right = static_cast<int>(std::lround(uiOffsetX + (x + w) * uiScale));
    const int bottom = static_cast<int>(std::lround(uiOffsetY + (y + h) * uiScale));
    for (int py = std::max(0, top); py < std::min(height, bottom); ++py) {
        for (int px = std::max(0, left); px < std::min(width, right); ++px) {
            const std::size_t pixel = static_cast<std::size_t>(py) * width + px;
            const std::size_t offset = pixel * 3;
            rgb[offset] = static_cast<unsigned char>(r);
            rgb[offset + 1] = static_cast<unsigned char>(g);
            rgb[offset + 2] = static_cast<unsigned char>(b);
            if (!alpha.empty()) alpha[pixel] = 255;
        }
    }
}

void NativeFrame::blendRectangle(int x, int y, int w, int h, Color color, double opacity) {
    const int r = static_cast<int>(std::clamp(toSrgb(color.r), 0.0, 1.0) * 255);
    const int g = static_cast<int>(std::clamp(toSrgb(color.g), 0.0, 1.0) * 255);
    const int b = static_cast<int>(std::clamp(toSrgb(color.b), 0.0, 1.0) * 255);
    const int alpha = static_cast<int>(std::clamp(opacity, 0.0, 1.0) * 255);
    const int left = static_cast<int>(std::lround(uiOffsetX + x * uiScale));
    const int top = static_cast<int>(std::lround(uiOffsetY + y * uiScale));
    const int right = static_cast<int>(std::lround(uiOffsetX + (x + w) * uiScale));
    const int bottom = static_cast<int>(std::lround(uiOffsetY + (y + h) * uiScale));
    for (int py = std::max(0, top); py < std::min(height, bottom); ++py) {
        for (int px = std::max(0, left); px < std::min(width, right); ++px) {
            const std::size_t pixel = static_cast<std::size_t>(py) * width + px;
            const std::size_t offset = pixel * 3;
            if (this->alpha.empty()) {
                rgb[offset] = static_cast<unsigned char>((r * alpha + rgb[offset] * (255 - alpha)) / 255);
                rgb[offset + 1] = static_cast<unsigned char>((g * alpha + rgb[offset + 1] * (255 - alpha)) / 255);
                rgb[offset + 2] = static_cast<unsigned char>((b * alpha + rgb[offset + 2] * (255 - alpha)) / 255);
            } else {
                const int oldAlpha = this->alpha[pixel];
                const int weight = alpha * 255 + oldAlpha * (255 - alpha);
                if (!weight) continue;
                rgb[offset] = static_cast<unsigned char>((r * alpha * 255 + rgb[offset] * oldAlpha * (255 - alpha) + weight / 2) / weight);
                rgb[offset + 1] = static_cast<unsigned char>((g * alpha * 255 + rgb[offset + 1] * oldAlpha * (255 - alpha) + weight / 2) / weight);
                rgb[offset + 2] = static_cast<unsigned char>((b * alpha * 255 + rgb[offset + 2] * oldAlpha * (255 - alpha) + weight / 2) / weight);
                this->alpha[pixel] = static_cast<unsigned char>((weight + 127) / 255);
            }
        }
    }
}

void Camera::basis() const {
    if (basisReady) return;
    forward = normalized(target - position);
    right = normalized(cross(forward, up));
    vertical = normalized(cross(right, forward));
    basisReady = true;
}

Vec3 Camera::view(Vec3 point) const {
    basis();
    const Vec3 relative = point - position;
    return {dot(relative, right), dot(relative, vertical), dot(relative, forward)};
}

Vec3 Camera::viewDirection(Vec3 direction) const {
    basis();
    return {dot(direction, right), dot(direction, vertical), dot(direction, forward)};
}

std::array<double, 16> Camera::viewMatrix() const {
    basis();
    return {right.x, vertical.x, forward.x, 0,
            right.y, vertical.y, forward.y, 0,
            right.z, vertical.z, forward.z, 0,
            -dot(right, position), -dot(vertical, position), -dot(forward, position), 1};
}

double Camera::focal() const {
    return height / (2.0 * std::tan(fovDegrees * 3.141592653589793 / 360.0));
}

RenderVertex cameraVertex(const Camera& camera, Vec3 point, Vec3 normal, Vec3 source, Vec2 uv) {
    if (camera.worldSpaceGpu) return {point, normal, source, uv};
    return {camera.view(point), camera.viewDirection(normal), source, uv};
}

Rasterizer::Rasterizer(int width, int height, Color background, double focal, bool gpu, GpuScene recycled)
    : width_(width), height_(height), focal_(focal), gpu_(gpu), scene_(std::move(recycled)) {
    for (GpuBatch& batch : scene_.batches) batch.vertices.clear();
    scene_.width = width;
    scene_.height = height;
    scene_.focal = focal;
    scene_.background = background;
    if (!gpu_) {
        depth_.resize(static_cast<std::size_t>(width) * height, 1e100);
        pixels_.resize(static_cast<std::size_t>(width) * height, background);
    }
}

void Rasterizer::setCamera(const Camera& camera) {
    if (gpu_) scene_.viewMatrix = camera.viewMatrix();
}

void Rasterizer::setObject(std::uint32_t id, Vec2 pivot, float yaw, float localY) {
    objectId_ = id;
    objectPivot_ = pivot;
    objectYaw_ = yaw;
    objectLocalY_ = localY;
}

void Rasterizer::triangle(const std::array<RenderVertex, 3>& input, const Material& material) {
    if (gpu_) {
        const bool skinned = input[0].skinned || input[1].skinned || input[2].skinned;
        std::size_t index = activeBatch_;
        bool reuse = index < scene_.batches.size();
        if (reuse) {
            const GpuBatch& candidate = scene_.batches[index];
            reuse = candidate.objectId == objectId_ && candidate.skinned == skinned &&
                sameMaterial(candidate.material, material);
        }
        if (!reuse) {
            auto found = std::find_if(scene_.batches.begin(), scene_.batches.end(), [&](const GpuBatch& candidate) {
                return candidate.objectId == objectId_ && candidate.skinned == skinned &&
                    sameMaterial(candidate.material, material);
            });
            if (found == scene_.batches.end()) {
                scene_.batches.push_back({});
                GpuBatch& created = scene_.batches.back();
                created.material = material;
                created.objectId = objectId_;
                created.pivotX = static_cast<float>(objectPivot_.x);
                created.pivotZ = static_cast<float>(objectPivot_.y);
                created.yaw = objectYaw_;
                created.skinned = skinned;
                created.vertices.reserve(12288);
                index = scene_.batches.size() - 1;
            } else {
                index = static_cast<std::size_t>(std::distance(scene_.batches.begin(), found));
            }
        }
        activeBatch_ = index;
        GpuBatch& batch = scene_.batches[index];
        batch.pivotX = static_cast<float>(objectPivot_.x);
        batch.pivotZ = static_cast<float>(objectPivot_.y);
        batch.yaw = objectYaw_;
        batch.localY = objectLocalY_;
        for (const RenderVertex& vertex : input) {
            batch.vertices.push_back({static_cast<float>(vertex.view.x), static_cast<float>(vertex.view.y),
                static_cast<float>(vertex.view.z), static_cast<float>(vertex.normal.x),
                static_cast<float>(vertex.normal.y), static_cast<float>(vertex.normal.z),
                static_cast<float>(vertex.uv.x), static_cast<float>(vertex.uv.y), vertex.bones, vertex.weights});
        }
        return;
    }
    constexpr double near = 0.035;
    std::array<RenderVertex, 4> polygon;
    std::size_t polygonSize = 0;
    for (int i = 0; i < 3; ++i) {
        const RenderVertex& a = input[i];
        const RenderVertex& b = input[(i + 1) % 3];
        const bool insideA = a.view.z >= near, insideB = b.view.z >= near;
        if (insideA) polygon[polygonSize++] = a;
        if (insideA != insideB) polygon[polygonSize++] = interpolate(a, b, (near - a.view.z) / (b.view.z - a.view.z));
    }
    static const Vec3 key = normalized(Vec3{-0.45, 0.78, 0.55});
    static const Vec3 fill = normalized(Vec3{0.65, 0.25, 0.35});
    const double old = std::clamp((material.age - 24) / 58, 0.0, 1.0);
    const double ageBlend = old * old * (3 - 2 * old);
    for (std::size_t face = 1; face + 1 < polygonSize; ++face) {
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
        if ((material.kind == Material::Kind::Skin || material.kind == Material::Kind::Cloth) && area > 0) continue;
        const double inverseArea = 1.0 / area;
        const int minX = std::max(0, static_cast<int>(std::floor(std::min({p[0].x, p[1].x, p[2].x}))));
        const int maxX = std::min(width_ - 1, static_cast<int>(std::ceil(std::max({p[0].x, p[1].x, p[2].x}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({p[0].y, p[1].y, p[2].y}))));
        const int maxY = std::min(height_ - 1, static_cast<int>(std::ceil(std::max({p[0].y, p[1].y, p[2].y}))));
        if (minX > maxX || minY > maxY) continue;
        Vec3 tangent{1, 0, 0}, bitangent{0, 1, 0};
        if (material.kind == Material::Kind::Skin && material.normalYoung) {
            const Vec3 e1 = p[1].view - p[0].view, e2 = p[2].view - p[0].view;
            const double du1 = p[1].uv.x - p[0].uv.x, dv1 = p[1].uv.y - p[0].uv.y;
            const double du2 = p[2].uv.x - p[0].uv.x, dv2 = p[2].uv.y - p[0].uv.y;
            const double denominator = du1 * dv2 - du2 * dv1;
            if (std::abs(denominator) > 1e-9) {
                tangent = normalized((e1 * dv2 - e2 * dv1) / denominator);
                bitangent = normalized((e2 * du1 - e1 * du2) / denominator);
            }
        }
        double constantDiffuse = 0.0;
        const bool constantLighting = material.constantLighting && material.specular <= 0.0;
        if (constantLighting) {
            Vec3 normal = p[0].normal;
            if (dot(normal, p[0].view) > 0) normal = normal * -1.0;
            constantDiffuse = 0.38 + 0.78 * std::max(0.0, dot(normal, key)) +
                              0.24 * std::max(0.0, dot(normal, fill));
        }
        const double step0X = (p[1].y - p[2].y) * inverseArea;
        const double step1X = (p[2].y - p[0].y) * inverseArea;
        for (int y = minY; y <= maxY; ++y) {
            const double py = y + 0.5;
            const double px = minX + 0.5;
            double w0 = ((p[1].x - px) * (p[2].y - py) - (p[1].y - py) * (p[2].x - px)) * inverseArea;
            double w1 = ((p[2].x - px) * (p[0].y - py) - (p[2].y - py) * (p[0].x - px)) * inverseArea;
            for (int x = minX; x <= maxX; ++x, w0 += step0X, w1 += step1X) {
                const double w2 = 1 - w0 - w1;
                if (w0 < -1e-7 || w1 < -1e-7 || w2 < -1e-7) continue;
                const double inverse = w0 * p[0].inverseDepth + w1 * p[1].inverseDepth + w2 * p[2].inverseDepth;
                if (inverse <= 1e-9) continue;
                const double depth = 1 / inverse;
                const std::size_t pixel = static_cast<std::size_t>(y) * width_ + x;
                if (depth >= depth_[pixel]) continue;
                const double a = w0 * p[0].inverseDepth * depth;
                const double b = w1 * p[1].inverseDepth * depth;
                const double c = w2 * p[2].inverseDepth * depth;
                const Vec2 uv{p[0].uv.x * a + p[1].uv.x * b + p[2].uv.x * c,
                              p[0].uv.y * a + p[1].uv.y * b + p[2].uv.y * c};
                if (material.kind == Material::Kind::Hair && alpha(material.texture, uv.x, uv.y) < 0.32) continue;
                Vec3 view, normal;
                if (!constantLighting) {
                    view = p[0].view * a + p[1].view * b + p[2].view * c;
                    normal = normalized(p[0].normal * a + p[1].normal * b + p[2].normal * c);
                }
                Color base = material.color;
                if (material.kind == Material::Kind::Floor) {
                    if (material.texture) {
                        base = linear(sample(material.texture, uv.x, uv.y));
                    } else {
                        const double sx = p[0].source.x * a + p[1].source.x * b + p[2].source.x * c;
                        const double sz = p[0].source.z * a + p[1].source.z * b + p[2].source.z * c;
                        const int tile = (static_cast<int>(std::floor(sx)) + static_cast<int>(std::floor(sz))) & 1;
                        base = tile ? material.second : material.color;
                    }
                } else if (material.kind == Material::Kind::Skin) {
                    Color detail;
                    if (material.texture) {
                        detail = linear(sample(material.texture, uv.x, uv.y));
                    } else {
                        detail = mix(linear(sample(material.young, uv.x, uv.y)),
                                     linear(sample(material.old, uv.x, uv.y)), ageBlend * 0.94);
                    }
                    base = {base.r * detail.r, base.g * detail.g, base.b * detail.b};
                    if (material.normalYoung) {
                        const Color nm = material.normalOld ? mix(sample(material.normalYoung, uv.x, uv.y),
                            sample(material.normalOld, uv.x, uv.y), ageBlend) : sample(material.normalYoung, uv.x, uv.y);
                        normal = normalized(normal + tangent * ((nm.r * 2 - 1) * 0.32) +
                                            bitangent * ((nm.g * 2 - 1) * 0.32));
                    }
                } else if (material.kind == Material::Kind::Hair || material.kind == Material::Kind::Textured) {
                    const Color detail = linear(sample(material.texture, uv.x, uv.y));
                    base = {base.r * detail.r, base.g * detail.g, base.b * detail.b};
                } else if (material.kind == Material::Kind::Cloth) {
                    const int weave = (static_cast<int>(std::floor(uv.x * 640)) +
                                       static_cast<int>(std::floor(uv.y * 640))) & 3;
                    base = base * (weave == 0 ? 1.055 : weave == 1 ? 0.955 : 1.0);
                }
                if (material.unlit) {
                    pixels_[pixel] = base;
                    depth_[pixel] = depth;
                    continue;
                }
                if (constantLighting) {
                    pixels_[pixel] = base * constantDiffuse;
                    depth_[pixel] = depth;
                    continue;
                }
                if (dot(normal, view) > 0) normal = normal * -1.0;
                const double diffuse = 0.38 + 0.78 * std::max(0.0, dot(normal, key)) +
                                       0.24 * std::max(0.0, dot(normal, fill));
                double specular = 0.0;
                if (material.specular > 0.0) {
                    const Vec3 toward = normalized(view * -1.0);
                    specular = specularPower42(std::max(0.0, dot(normal, normalized(key + toward)))) * material.specular;
                }
                pixels_[pixel] = base * diffuse + Color{specular, specular, specular};
                depth_[pixel] = depth;
            }
        }
    }
}

NativeFrame Rasterizer::finish() const {
    NativeFrame frame;
    frame.width = width_;
    frame.height = height_;
    if (gpu_) {
        if (renderGpuScene(scene_, frame)) return frame;
        frame.rgb.clear();
        return frame;
    }
    frame.rgb.resize(pixels_.size() * 3);
    constexpr int size = 4096;
    constexpr double range = 4.0;
    static const auto output = [] {
        std::array<unsigned char, size> values{};
        for (int i = 0; i < size; ++i) {
            const double x = range * i / (size - 1);
            const double tone = std::clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
            values[i] = static_cast<unsigned char>(std::clamp(toSrgb(tone), 0.0, 1.0) * 255);
        }
        return values;
    }();
    const auto encode = [](double value) {
        if (value <= 0.0) return static_cast<unsigned char>(0);
        if (value >= range) return static_cast<unsigned char>(255);
        return output[static_cast<int>(value * ((size - 1) / range) + 0.5)];
    };
    const auto convert = [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            frame.rgb[i * 3] = encode(pixels_[i].r);
            frame.rgb[i * 3 + 1] = encode(pixels_[i].g);
            frame.rgb[i * 3 + 2] = encode(pixels_[i].b);
        }
    };
    if (pixels_.size() > 100000) {
        const std::size_t middle = pixels_.size() / 2;
        std::thread worker(convert, middle, pixels_.size());
        convert(0, middle);
        worker.join();
    } else {
        convert(0, pixels_.size());
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
    Material flat = material;
    flat.constantLighting = true;
    for (int face = 0; face < 6; ++face) {
        const RenderVertex a = cameraVertex(camera, points[faces[face][0]], normals[face], points[faces[face][0]], {0, 0});
        const RenderVertex b = cameraVertex(camera, points[faces[face][1]], normals[face], points[faces[face][1]], {1, 0});
        const RenderVertex c = cameraVertex(camera, points[faces[face][2]], normals[face], points[faces[face][2]], {1, 1});
        const RenderVertex d = cameraVertex(camera, points[faces[face][3]], normals[face], points[faces[face][3]], {0, 1});
        rasterizer.triangle({a, b, c}, flat);
        rasterizer.triangle({a, c, d}, flat);
    }
}

void drawEllipsoid(Rasterizer& rasterizer, const Camera& camera, Vec3 center, Vec3 radii,
                   const Material& material, int segments, int rings) {
    constexpr double pi = 3.141592653589793;
    segments = std::max(3, segments);
    rings = std::max(2, rings);
    const std::uint64_t key = (static_cast<std::uint64_t>(segments) << 32) | static_cast<unsigned>(rings);
    static std::map<std::uint64_t, std::vector<Vec3>> cache;
    auto found = cache.find(key);
    if (found == cache.end()) {
        std::vector<Vec3> unit;
        unit.reserve(static_cast<std::size_t>(rings + 1) * (segments + 1));
        for (int ring = 0; ring <= rings; ++ring) for (int segment = 0; segment <= segments; ++segment) {
            const double t = pi * ring / rings, p = 2 * pi * segment / segments;
            unit.push_back({std::sin(t) * std::cos(p), std::cos(t), std::sin(t) * std::sin(p)});
        }
        found = cache.emplace(key, std::move(unit)).first;
    }
    const std::vector<Vec3>& unit = found->second;
    std::vector<RenderVertex> vertices(unit.size());
    for (std::size_t i = 0; i < unit.size(); ++i) {
        const Vec3 point = center + Vec3{unit[i].x * radii.x, unit[i].y * radii.y, unit[i].z * radii.z};
        const Vec3 normal = normalized(Vec3{unit[i].x / radii.x, unit[i].y / radii.y, unit[i].z / radii.z});
        const int ring = static_cast<int>(i) / (segments + 1);
        const int segment = static_cast<int>(i) % (segments + 1);
        vertices[i] = cameraVertex(camera, point, normal, point,
            {segment / static_cast<double>(segments), ring / static_cast<double>(rings)});
    }
    auto index = [&](int ring, int segment) { return static_cast<std::size_t>(ring * (segments + 1) + segment); };
    for (int ring = 0; ring < rings; ++ring) for (int segment = 0; segment < segments; ++segment) {
        const RenderVertex& a = vertices[index(ring, segment)];
        const RenderVertex& b = vertices[index(ring + 1, segment)];
        const RenderVertex& c = vertices[index(ring + 1, segment + 1)];
        const RenderVertex& d = vertices[index(ring, segment + 1)];
        rasterizer.triangle({a, b, c}, material);
        rasterizer.triangle({a, c, d}, material);
    }
}

} // namespace gtc::native
