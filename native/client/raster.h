#pragma once

#include "native.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gtc::native {

inline constexpr int kDefaultWindowWidth = 640;
inline constexpr int kDefaultWindowHeight = 480;

struct Color {
    double r = 0;
    double g = 0;
    double b = 0;
};

Color operator+(Color a, Color b);
Color operator*(Color a, double b);
Color colorHex(const char* value);
Color mix(Color a, Color b, double amount);

struct Texture {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

const Texture& loadTexture(const std::string& path);

struct NativeFrame {
    int width = kDefaultWindowWidth;
    int height = kDefaultWindowHeight;
    std::vector<unsigned char> rgb;
    std::vector<unsigned char> alpha;
    // A backend may hand back the picture already in the byte order the window
    // wants (BGRA, bottom-up rows) instead of RGB. The Vulkan readback then
    // becomes one move per pixel instead of a repack followed by a second
    // repack in the presenter -- two full passes over 1.45 M pixels that
    // cancelled each other out. Anything that draws over the frame asks for rgb
    // and gets it converted back first.
    std::vector<unsigned char> bgra;
    bool wantsBgra = false;
    bool hasBgra() const { return !bgra.empty(); }
    // True when the pixels this frame owns are in window order at four bytes a
    // pixel. Anything drawn over the frame writes there instead of into rgb,
    // which is the difference between one conversion a frame and two.
    bool canvasIsBgra() const {
        // `direct` counts: a backend that hands over its own mapping has the
        // pixels in window order already, they just are not copied yet.
        if (!rgb.empty()) return false;
        if (direct != nullptr) return true;
        return bgra.size() >= static_cast<std::size_t>(width) * height * 4U;
    }
    // Takes the pixels off the backend's mapping so they can be drawn over.
    // Drawing in place is safe: the mapping is the frame's until the backend is
    // asked for another one, and the window has already blitted it by then.
    void makeOverlayWritable();
    // Where a BGRA frame is written. Usually the backend's own mapping, which is
    // 5.8 MB of copying at this size that nothing was reading.
    unsigned char* canvasBgra() {
        return direct != nullptr ? const_cast<unsigned char*>(direct) : bgra.data();
    }
    // A backend whose readback already sits in host memory can point the frame
    // at those bytes instead of copying them out again. The pointer belongs to
    // the backend and is valid until the next frame is rendered, which is long
    // enough for the window to put it on screen -- and it saves 5.8 MB of host
    // copying per frame at this size.
    const unsigned char* direct = nullptr;
    // Fills rgb from bgra if a caller that needs RGB turns up. Cheap no-op
    // otherwise.
    // Set by a backend that has already put the picture on screen itself --
    // a swapchain hands the finished image to the compositor on the device. The
    // window must then not blit anything: there are no host pixels to blit, and
    // the whole point of the path is that none were ever made.
    bool presented = false;
    // Set by a caller that needs the pixels anyway, whatever the backend would
    // rather do with them. A screenshot is the one thing a presented frame
    // cannot serve.
    bool wantPixels = false;
    void syncToRgb();
    double uiScale = 1.0;
    double uiOffsetX = 0.0;
    double uiOffsetY = 0.0;

    bool save(const std::string& path) const;
    void logicalUi(int logicalWidth = kDefaultWindowWidth, int logicalHeight = kDefaultWindowHeight);
    void rectangle(int x, int y, int w, int h, Color color);
    void blendRectangle(int x, int y, int w, int h, Color color, double opacity);
};

// Measured where they are spent, for GTC_HUD_PROFILE: the HUD is drawn on the
// host every frame and it is the reason the picture comes back to the host.
extern double rectangleMilliseconds;
extern long rectanglePixels;

struct Material {
    enum class Kind { Solid, Skin, Hair, Floor, Puddle, Cloth, Textured } kind = Kind::Solid;
    Color color{0.5, 0.5, 0.5};
    Color second{0.5, 0.5, 0.5};
    const Texture* texture = nullptr;
    const Texture* young = nullptr;
    const Texture* old = nullptr;
    const Texture* normalYoung = nullptr;
    const Texture* normalOld = nullptr;
    double age = 25;
    double specular = 0.0;
    double opacity = 1.0;
    bool unlit = false;
    bool constantLighting = false;
};

struct Camera {
    Vec3 position;
    Vec3 target;
    Vec3 up{0, 1, 0};
    double fovDegrees = 50;
    int width = kDefaultWindowWidth;
    int height = kDefaultWindowHeight;
    bool worldSpaceGpu = false;

    Vec3 view(Vec3 point) const;
    Vec3 viewDirection(Vec3 direction) const;
    std::array<double, 16> viewMatrix() const;
    double focal() const;

private:
    void basis() const;
    mutable bool basisReady = false;
    mutable Vec3 forward;
    mutable Vec3 right;
    mutable Vec3 vertical;
};

struct RenderVertex {
    Vec3 view;
    Vec3 normal;
    Vec3 source;
    Vec2 uv;
};

RenderVertex cameraVertex(const Camera& camera, Vec3 point, Vec3 normal, Vec3 source, Vec2 uv = {});

struct GpuVertex {
    float x = 0, y = 0, z = 0;
    float nx = 0, ny = 0, nz = 0;
    float u = 0, v = 0;
};

// A scanned bone is an indexed mesh: 97,000 points and 195,000 triangles that
// name them. Unrolled into three vertices a triangle it is six copies of every
// point, six times the vertex traffic and six times the vertex shader work for
// the same picture -- and the vertex stage is where a frame full of bones
// spends its time.

struct GpuBatch {
    Material material;
    std::vector<GpuVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t objectId = 0;
    std::uint64_t revision = 0;
    float pivotX = 0, pivotZ = 0, yaw = 0;
    std::array<double, 16> modelMatrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

struct GpuScene {
    int width = kDefaultWindowWidth;
    int height = kDefaultWindowHeight;
    double focal = 640;
    std::uint64_t revision = 0;
    Color background;
    // True when the scene covers every pixel of the frame -- an interior with no
    // view of the outside. The colour attachment then does not have to be
    // cleared, which at 1568x929 is 5.8 MB of traffic a frame nobody sees.
    bool covered = false;
    std::array<double, 16> viewMatrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::vector<GpuBatch> batches;
};

using GpuRenderer = bool (*)(const GpuScene& scene, NativeFrame& frame);
void setGpuRenderer(GpuRenderer renderer);

// How many worker threads the client uses for whole-frame pixel work. Half the
// machine unless GTC_THREADS says otherwise.
std::size_t frameThreads();

// Reorders an RGB frame into the BGRA bytes a Windows device context blits, and
// splits the work across cores. `out` is resized to four bytes per pixel.
void rgbToBgra(const NativeFrame& frame, std::vector<unsigned char>& out);
bool renderGpuScene(const GpuScene& scene, NativeFrame& frame);

// Frame buffers live outside the rasterizer so a running client can keep the
// same few megabytes of memory between frames instead of faulting in a fresh
// sixteen megabyte block every time it draws.
struct RasterBuffer {
    std::vector<double> depth;
    std::vector<Color> pixels;
};

// One queued triangle: the geometry, and an index into the frame's material
// table. Copying the whole Material per triangle made submission the most
// expensive part of the software path, and almost every triangle shares a
// material with the one before it.
struct RasterTriangle {
    std::array<RenderVertex, 3> vertices;
    std::uint32_t material = 0;
};

class Rasterizer {
public:
    Rasterizer(int width, int height, Color background, double focal, bool gpu = false, GpuScene recycled = {},
               RasterBuffer* reusable = nullptr);
    ~Rasterizer();
    Rasterizer(const Rasterizer&) = delete;
    Rasterizer& operator=(const Rasterizer&) = delete;

    void triangle(const std::array<RenderVertex, 3>& vertices, const Material& material);
    // A triangle that is already in world space, for the GPU path: no view
    // transform, no RenderVertex with its doubles. A scanned skeleton emits
    // 195,000 of these per body and the packing was most of the cost.
    void worldTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& normal,
                       const Material& material);
    // A whole indexed mesh at once. The GPU path keeps the indices; the software
    // path unrolls them, because its rasterizer takes triangles and has no use
    // for a vertex cache.
    void mesh(const std::vector<Vec3>& points, const std::vector<std::array<int, 3>>& faces,
              const Material& material);
    bool gpu() const { return gpu_; }
    void setCamera(const Camera& camera);
    void setObject(std::uint32_t id, Vec2 pivot, float yaw);
    GpuScene takeGpuScene() { return std::move(scene_); }
    // Draws everything queued and hands back the frame. Non-const because the
    // drawing happens here: triangle() only records what to draw.
    NativeFrame finish();

private:
    int width_;
    int height_;
    double focal_;
    bool gpu_ = false;
    std::uint32_t objectId_ = 0;
    Vec2 objectPivot_{};
    float objectYaw_ = 0;
    std::size_t activeBatch_ = static_cast<std::size_t>(-1);
    // The caller hands in the same Material object for long runs of triangles --
    // a skeleton body draws all of its bones with one material. Recognising that
    // by address, instead of comparing every field of the struct for each of
    // 195,000 triangles, was most of the cost of building a scene.
    const Material* activeMaterial_ = nullptr;
    int activeObject_ = -1;
    std::size_t prepareBatch(const Material& material);
    // Kept so a triangle given in world space can still be projected when the
    // caller asked for the software path.
    Camera camera_{};
    GpuScene scene_;
    // Triangles wait here until finish(), so the frame can be split into bands
    // and drawn on every core instead of one.
    void rasterize(const std::array<RenderVertex, 3>& vertices, const Material& material, int yBegin, int yEnd);
    void flush();
public:
    // Draws everything queued here into another rasterizer's frame. Used when a
    // frame is built in halves on two threads and both halves have to land on
    // the same pixels.
    void drain(Rasterizer& target);
private:
    std::vector<RasterTriangle> pending_;
    std::vector<Material> materials_;
    std::vector<double> depth_;
    std::vector<Color> pixels_;
    RasterBuffer* reusable_ = nullptr;
};

// Joins scenes that were built independently -- on separate threads -- into one,
// in the order given, so the result is the same however the work was split.
GpuScene mergeScenes(std::vector<GpuScene> scenes);

void drawBox(Rasterizer& rasterizer, const Camera& camera, Vec3 center, Vec3 size,
             const Material& material);
void drawEllipsoid(Rasterizer& rasterizer, const Camera& camera, Vec3 center, Vec3 radii,
                   const Material& material, int segments = 16, int rings = 10);

void drawText(NativeFrame& frame, int x, int y, const std::string& text, Color color, int scale = 3);

// The same face, at a size in pixels and a position in pixels. The HUD-scale
// variant exists because the old overlay was laid out on a 640x480 grid; this one
// is for things that are positioned against the window itself, like the frame
// rate. Proportional, antialiased, and measured before it is drawn -- a centred
// box needs the width first.
int textWidthPixels(const std::string& text, int pixelHeight);
int textHeightPixels(int pixelHeight);
void drawTextPixels(NativeFrame& frame, int x, int y, const std::string& text, Color color, int pixelHeight);

} // namespace gtc::native
