#include "client/raster.h"

#include "client/ui_layer.h"
#include "client/raster_color.h"
#include "client/threadpool.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <map>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

namespace gtc::native {

using namespace raster_detail;

namespace {


GpuRenderer activeGpuRenderer = nullptr;

// Reused across frames by whichever thread draws, so the software path stops
// reallocating a few megabytes of queue storage every single frame.
thread_local std::vector<RasterTriangle> pendingScratch;
thread_local std::vector<Material> materialScratch;

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

Color colorHex(std::uint32_t rgb) {
    return linear({((rgb >> 16) & 255U) / 255.0, ((rgb >> 8) & 255U) / 255.0,
                   (rgb & 255U) / 255.0});
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


void Camera::basis() const {
    if (basisReady) return;
    forward = normalized(target - position);
    Vec3 lateral = cross(forward, up);
    if (length(lateral) < 1e-10) {
        // At vertical gaze the tiny horizontal component still carries yaw.
        // Use it as the screen-up reference instead of normalizing a zero
        // cross product; this keeps both CPU projection and the GPU matrix valid.
        Vec3 horizontal{forward.x, 0, forward.z};
        if (length(horizontal) < 1e-12) horizontal = {0, 0, 1};
        horizontal = normalized(horizontal) * (forward.y < 0 ? 1.0 : -1.0);
        lateral = cross(forward, horizontal);
    }
    right = normalized(lateral);
    vertical = normalized(cross(right, forward));
    basisReady = true;
}

Vec3 Camera::view(Vec3 point) const {
    basis();
    const Vec3 relative = point - position;
    return {dot(relative, right), dot(relative, vertical), dot(relative, forward)};
}

Vec3 Camera::world(Vec3 viewPoint) const {
    basis();
    return position + right * viewPoint.x + vertical * viewPoint.y + forward * viewPoint.z;
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

Rasterizer::Rasterizer(int width, int height, Color background, double focal, bool gpu, GpuScene recycled,
                       RasterBuffer* reusable)
    : width_(width), height_(height), focal_(focal), gpu_(gpu), scene_(std::move(recycled)),
      reusable_(gpu ? nullptr : reusable) {
    for (GpuBatch& batch : scene_.batches) {
        batch.vertices.clear();
        batch.indices.clear();
    }
    scene_.width = width;
    scene_.height = height;
    scene_.focal = focal;
    scene_.background = background;
    if (gpu_) return;
    if (pendingScratch.capacity()) pending_.swap(pendingScratch);
    if (materialScratch.capacity()) materials_.swap(materialScratch);
    if (reusable_) {
        depth_.swap(reusable_->depth);
        pixels_.swap(reusable_->pixels);
    }
    const std::size_t count = static_cast<std::size_t>(width) * height;
    depth_.resize(count);
    pixels_.resize(count);
    std::fill(depth_.begin(), depth_.end(), 1e100);
    std::fill(pixels_.begin(), pixels_.end(), background);
}

Rasterizer::~Rasterizer() {
    // Hand the queue's storage back for the next frame.
    if (pending_.capacity() > pendingScratch.capacity()) {
        pending_.clear();
        pendingScratch.swap(pending_);
    }
    if (materials_.capacity() > materialScratch.capacity()) {
        materials_.clear();
        materialScratch.swap(materials_);
    }
    if (!reusable_) return;
    reusable_->depth.swap(depth_);
    reusable_->pixels.swap(pixels_);
}

void Rasterizer::setCamera(const Camera& camera) {
    camera_ = camera;
    if (gpu_) scene_.viewMatrix = camera.viewMatrix();
}

void Rasterizer::setObject(std::uint32_t id, Vec2 pivot, float yaw) {
    objectId_ = id;
    objectPivot_ = pivot;
    objectYaw_ = yaw;
}

// Shared by both triangle entry points: picks the batch this material belongs
// to, creating it if this is the first triangle to use it.
std::size_t Rasterizer::prepareBatch(const Material& material) {
    std::size_t index = activeBatch_;
    bool reuse = index < scene_.batches.size();
    const bool sameObjectAndMaterial = reuse && activeMaterial_ == &material && activeObject_ == objectId_;
    if (reuse && !sameObjectAndMaterial) {
        const GpuBatch& candidate = scene_.batches[index];
        reuse = candidate.objectId == objectId_ && sameMaterial(candidate.material, material);
    }
    if (!reuse) {
        auto found = std::find_if(scene_.batches.begin(), scene_.batches.end(), [&](const GpuBatch& candidate) {
            return candidate.objectId == objectId_ && sameMaterial(candidate.material, material);
        });
        if (found == scene_.batches.end()) {
            scene_.batches.push_back({});
            GpuBatch& created = scene_.batches.back();
            created.material = material;
            created.objectId = objectId_;
            created.pivotX = static_cast<float>(objectPivot_.x);
            created.pivotZ = static_cast<float>(objectPivot_.y);
            created.yaw = objectYaw_;
            created.vertices.reserve(12288);
            index = scene_.batches.size() - 1;
        } else {
            index = static_cast<std::size_t>(std::distance(scene_.batches.begin(), found));
        }
    }
    activeBatch_ = index;
    activeMaterial_ = &material;
    activeObject_ = objectId_;
    GpuBatch& batch = scene_.batches[index];
    if (!sameObjectAndMaterial || batch.pivotX != static_cast<float>(objectPivot_.x) ||
        batch.pivotZ != static_cast<float>(objectPivot_.y) || batch.yaw != objectYaw_) {
        batch.pivotX = static_cast<float>(objectPivot_.x);
        batch.pivotZ = static_cast<float>(objectPivot_.y);
        batch.yaw = objectYaw_;
    }
    return index;
}

void Rasterizer::worldTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& normal,
                               const Material& material) {
    if (!gpu_) {
        // The software path needs unit normals: its flat-shading test compares
        // dot products with 1.0 to decide whether a triangle can be shaded once.
        const Vec3 unit = normalized(normal);
        triangle({cameraVertex(camera_, a, unit, a), cameraVertex(camera_, b, unit, b),
                  cameraVertex(camera_, c, unit, c)}, material);
        return;
    }
    GpuBatch& batch = scene_.batches[prepareBatch(material)];
    const std::size_t base = batch.vertices.size();
    batch.vertices.resize(base + 3);
    GpuVertex* out = batch.vertices.data() + base;
    const Vec3* points[3] = {&a, &b, &c};
    for (int corner = 0; corner < 3; ++corner, ++out) {
        const Vec3& point = *points[corner];
        out->x = static_cast<float>(point.x);
        out->y = static_cast<float>(point.y);
        out->z = static_cast<float>(point.z);
        out->nx = static_cast<float>(normal.x);
        out->ny = static_cast<float>(normal.y);
        out->nz = static_cast<float>(normal.z);
        out->u = 0.0F;
        out->v = 0.0F;
    }
}

namespace {
// The face normal a triangle implies, and the bounds check every emitter here
// needs: a scanned mesh is data, and data can be wrong.
bool usableFace(const std::array<int, 3>& face, std::size_t points) {
    return face[0] >= 0 && face[1] >= 0 && face[2] >= 0 &&
        static_cast<std::size_t>(face[0]) < points && static_cast<std::size_t>(face[1]) < points &&
        static_cast<std::size_t>(face[2]) < points;
}
} // namespace

void Rasterizer::mesh(const std::vector<Vec3>& points, const std::vector<std::array<int, 3>>& faces,
                      const Material& material) {
    if (!gpu_) {
        for (const std::array<int, 3>& face : faces) {
            if (!usableFace(face, points.size())) continue;
            const Vec3& a = points[static_cast<std::size_t>(face[0])];
            const Vec3& b = points[static_cast<std::size_t>(face[1])];
            const Vec3& c = points[static_cast<std::size_t>(face[2])];
            worldTriangle(a, b, c, cross(b - a, c - a), material);
        }
        return;
    }
    GpuBatch& batch = scene_.batches[prepareBatch(material)];
    // A batch is either indexed or unrolled, never both: drawing an indexed
    // batch ignores vertices no index names, and drawing an unrolled one ignores
    // the indices. A batch that already holds unrolled triangles keeps taking
    // them, and this mesh is unrolled to match.
    if (!batch.vertices.empty() && batch.indices.empty()) {
        for (const std::array<int, 3>& face : faces) {
            if (!usableFace(face, points.size())) continue;
            const Vec3& a = points[static_cast<std::size_t>(face[0])];
            const Vec3& b = points[static_cast<std::size_t>(face[1])];
            const Vec3& c = points[static_cast<std::size_t>(face[2])];
            worldTriangle(a, b, c, cross(b - a, c - a), material);
        }
        return;
    }
    const std::uint32_t base = static_cast<std::uint32_t>(batch.vertices.size());
    batch.vertices.reserve(batch.vertices.size() + points.size());
    for (const Vec3& point : points) {
        GpuVertex vertex;
        vertex.x = static_cast<float>(point.x);
        vertex.y = static_cast<float>(point.y);
        vertex.z = static_cast<float>(point.z);
        // The normal of an indexed batch is derived per fragment from the
        // triangle that carries it, which is the face normal this used to hold
        // three copies of and now holds none of.
        batch.vertices.push_back(vertex);
    }
    batch.indices.reserve(batch.indices.size() + faces.size() * 3);
    for (const std::array<int, 3>& face : faces) {
        if (!usableFace(face, points.size())) continue;
        batch.indices.push_back(base + static_cast<std::uint32_t>(face[0]));
        batch.indices.push_back(base + static_cast<std::uint32_t>(face[1]));
        batch.indices.push_back(base + static_cast<std::uint32_t>(face[2]));
    }
    activeMaterial_ = &material;
    activeObject_ = objectId_;
}

void Rasterizer::triangle(const std::array<RenderVertex, 3>& input, const Material& material) {
    if (gpu_) {
        std::size_t index = activeBatch_;
        bool reuse = index < scene_.batches.size();
        // Fast path first: the same material object, on the same batch, is the
        // overwhelmingly common case inside a body and needs no comparison.
        const bool sameObjectAndMaterial = reuse && activeMaterial_ == &material &&
            activeObject_ == objectId_;
        if (reuse && !sameObjectAndMaterial) {
            const GpuBatch& candidate = scene_.batches[index];
            reuse = candidate.objectId == objectId_ && sameMaterial(candidate.material, material);
        }
        if (!reuse) {
            auto found = std::find_if(scene_.batches.begin(), scene_.batches.end(), [&](const GpuBatch& candidate) {
                return candidate.objectId == objectId_ && sameMaterial(candidate.material, material);
            });
            if (found == scene_.batches.end()) {
                scene_.batches.push_back({});
                GpuBatch& created = scene_.batches.back();
                created.material = material;
                created.objectId = objectId_;
                created.pivotX = static_cast<float>(objectPivot_.x);
                created.pivotZ = static_cast<float>(objectPivot_.y);
                created.yaw = objectYaw_;
                created.vertices.reserve(12288);
                index = scene_.batches.size() - 1;
            } else {
                index = static_cast<std::size_t>(std::distance(scene_.batches.begin(), found));
            }
        }
        activeBatch_ = index;
        GpuBatch& batch = scene_.batches[index];
        if (!sameObjectAndMaterial || batch.pivotX != static_cast<float>(objectPivot_.x) ||
            batch.pivotZ != static_cast<float>(objectPivot_.y) || batch.yaw != objectYaw_) {
            batch.pivotX = static_cast<float>(objectPivot_.x);
            batch.pivotZ = static_cast<float>(objectPivot_.y);
            batch.yaw = objectYaw_;
        }
        activeMaterial_ = &material;
        activeObject_ = objectId_;
        // Three vertices appended in one go rather than three separate growth
        // checks and iterator updates: this runs 195,000 times per body.
        const std::size_t base = batch.vertices.size();
        batch.vertices.resize(base + 3);
        GpuVertex* out = batch.vertices.data() + base;
        for (int corner = 0; corner < 3; ++corner, ++out) {
            const RenderVertex& vertex = input[corner];
            out->x = static_cast<float>(vertex.view.x);
            out->y = static_cast<float>(vertex.view.y);
            out->z = static_cast<float>(vertex.view.z);
            out->nx = static_cast<float>(vertex.normal.x);
            out->ny = static_cast<float>(vertex.normal.y);
            out->nz = static_cast<float>(vertex.normal.z);
            out->u = static_cast<float>(vertex.uv.x);
            out->v = static_cast<float>(vertex.uv.y);
        }
        return;
    }
    // Queued, not drawn: finish() splits the frame into horizontal bands and
    // draws them on separate cores. Each band owns its rows, so no lock is
    // needed and no pixel is written by two threads.
    std::uint32_t index = static_cast<std::uint32_t>(materials_.size());
    for (std::uint32_t candidate = 0; candidate < materials_.size(); ++candidate) {
        if (sameMaterial(materials_[candidate], material)) {
            index = candidate;
            break;
        }
    }
    if (index == materials_.size()) materials_.push_back(material);
    pending_.push_back({input, index});
}

void Rasterizer::rasterize(const std::array<RenderVertex, 3>& input, const Material& material,
                             int yBegin, int yEnd) {
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
        // The band this thread owns: a triangle that spans several bands is
        // drawn once per band, each time only over that band's rows.
        const int minY = std::max(yBegin, std::max(0, static_cast<int>(std::floor(std::min({p[0].y, p[1].y, p[2].y})))));
        const int maxY = std::min(yEnd - 1, std::min(height_ - 1, static_cast<int>(std::ceil(std::max({p[0].y, p[1].y, p[2].y})))));
        if (minX > maxX || minY > maxY || yBegin >= yEnd) continue;
        const Vec3 centroidView = (p[0].view + p[1].view + p[2].view) / 3.0;
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
        // Any triangle whose three normals agree shades identically across its
        // whole area, so the per-pixel normal interpolation, renormalisation and
        // two dot products can be folded into one value here. Walls, floor and
        // the box furniture are all of that kind, and they cover most pixels.
        const bool flatShaded = std::abs(dot(p[0].normal, p[1].normal) - 1.0) < 1e-6 &&
            std::abs(dot(p[0].normal, p[2].normal) - 1.0) < 1e-6;
        double constantDiffuse = 0.0;
        const bool constantLighting = (material.constantLighting || flatShaded) && material.specular <= 0.0;
        if (constantLighting) {
            Vec3 normal = p[0].normal;
            if (dot(normal, centroidView) > 0) normal = normal * -1.0;
            constantDiffuse = 0.38 + 0.78 * std::max(0.0, dot(normal, key)) +
                              0.24 * std::max(0.0, dot(normal, fill));
        }
        auto write = [&](std::size_t pixel, const Color& color, double depth) {
            if (material.opacity >= 0.999) {
                pixels_[pixel] = color;
                depth_[pixel] = depth;
            } else {
                pixels_[pixel] = pixels_[pixel] * (1.0 - material.opacity) + color * material.opacity;
            }
        };
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
                    // An authored surface may carry no atlas at all; in that
                    // case the skin tone is the whole colour, not a white
                    // detail map sampled out of an empty texture.
                    Color detail{1, 1, 1};
                    if (material.texture && !material.texture->pixels.empty()) {
                        detail = linear(sample(material.texture, uv.x, uv.y));
                    } else if (material.young && !material.young->pixels.empty()) {
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
                    write(pixel, base, depth);
                    continue;
                }
                if (constantLighting) {
                    write(pixel, base * constantDiffuse, depth);
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
                write(pixel, base * diffuse + Color{specular, specular, specular}, depth);
            }
        }
    }
}

void Rasterizer::drain(Rasterizer& target) {
    // Order matters for equal-depth ties, so the queued triangles are replayed
    // in the order they were submitted.
    for (const RasterTriangle& triangle : pending_) {
        target.rasterize(triangle.vertices, materials_[triangle.material], 0, target.height_);
    }
    pending_.clear();
    materials_.clear();
}

void Rasterizer::flush() {
    if (pending_.empty()) return;
    // How many bands: one per worker, but never more bands than rows.
    std::size_t bands = std::max<std::size_t>(1, std::min<std::size_t>(frameThreads(),
        static_cast<std::size_t>(std::max(1, height_))));
    if (bands == 1 || pending_.size() < 256) {
        for (const RasterTriangle& triangle : pending_) {
            rasterize(triangle.vertices, materials_[triangle.material], 0, height_);
        }
        pending_.clear();
        materials_.clear();
        return;
    }
    const auto drawBand = [&](std::size_t beginIndex, std::size_t endIndex) {
        for (std::size_t index = beginIndex; index < endIndex; ++index) {
            const int begin = height_ * static_cast<int>(index) / static_cast<int>(bands);
            const int end = height_ * static_cast<int>(index + 1) / static_cast<int>(bands);
            for (const RasterTriangle& triangle : pending_) {
                rasterize(triangle.vertices, materials_[triangle.material], begin, end);
            }
        }
    };
    FramePool& pool = framePool();
    pool.ranges(bands, bands, drawBand);
    pending_.clear();
    materials_.clear();
}

NativeFrame Rasterizer::finish() {
    NativeFrame frame;
    frame.width = width_;
    frame.height = height_;
    if (gpu_) {
        if (renderGpuScene(scene_, frame)) return frame;
        frame.rgb.clear();
        return frame;
    }
    static const bool profileRaster = std::getenv("GTC_RASTER_PROFILE") != nullptr;
    const auto rasterStart = std::chrono::steady_clock::now();
    flush();
    const auto rasterDone = std::chrono::steady_clock::now();
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
    // Every presented frame needs a coverage mask: the OpenGL presenter blends
    // the software image over the scene with it, and an empty mask there is not
    // a warning, it is a hard failure. The software path is fully opaque, so the
    // mask is simply solid.
    frame.alpha.assign(pixels_.size(), 255);
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
    if (profileRaster) {
        const auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        std::printf("  [cpu] triangles %.2f ms, encode %.2f ms\n",
                    ms(rasterStart, rasterDone), ms(rasterDone, std::chrono::steady_clock::now()));
    }
    return frame;
}

GpuScene mergeScenes(std::vector<GpuScene> scenes) {
    GpuScene merged;
    if (scenes.empty()) return merged;
    merged = std::move(scenes.front());
    for (std::size_t index = 1; index < scenes.size(); ++index) {
        for (GpuBatch& batch : scenes[index].batches) {
            if (batch.vertices.empty()) continue;
            merged.batches.push_back(std::move(batch));
        }
    }
    return merged;
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
