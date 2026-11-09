#include "client/playground_room.h"
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gtc::native {
namespace {

// One tick is 1/20 s and it is the only time step in this room. A velocity in
// m/s covers v/20 metres in a tick, because a tick is a twentieth of a second.
constexpr double kTick = 1.0 / 20.0;
constexpr double kMass = 1.0;      // kg
constexpr double kGravity = 9.8;   // m/s^2
constexpr double kSurfaceY = 0.0;  // the plane, y = 0
constexpr double kPointRadius = 0.07;
constexpr double kGhostRadius = 0.032;
constexpr double kTraceHalfWidth = 0.010;
constexpr std::uint32_t kGroundId = 1, kSurfaceId = 2, kPointId = 3, kTraceId = 4, kGhostId = 100;
constexpr const char* kSky = "#101c22";

// Batches carry a model matrix that the renderer reads every frame, so the
// point and its markers move without rebuilding or re-uploading any geometry.
std::array<double, 16> translation(Vec3 point) {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, point.x, point.y, point.z, 1};
}

// A unit quad laid along local +x, so one matrix turns it into this tick's
// hitscan segment.
std::array<double, 16> segment(Vec3 from, Vec3 to, double halfWidth) {
    const Vec3 delta = to - from;
    const double alongLength = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (!(alongLength > 1e-9)) return translation({0, -1000, 0});
    const Vec3 along{delta.x, delta.y, 0};
    const Vec3 across{-delta.y / alongLength * halfWidth, delta.x / alongLength * halfWidth, 0};
    return {along.x, along.y, 0, 0, across.x, across.y, 0, 0, 0, 0, 1, 0,
            from.x, from.y, from.z, 1};
}

// One flat colour, no shading: these are markers, not surfaces.
Material flat(const char* hex) {
    Material material;
    material.color = colorHex(hex);
    material.unlit = true;
    material.constantLighting = true;
    return material;
}

void quad(std::vector<Vec3>& points, std::vector<std::array<int, 3>>& faces,
          double x0, double y0, double x1, double y1, double z) {
    const int base = static_cast<int>(points.size());
    points.push_back({x0, y0, z});
    points.push_back({x1, y0, z});
    points.push_back({x1, y1, z});
    points.push_back({x0, y1, z});
    faces.push_back({base, base + 1, base + 2});
    faces.push_back({base, base + 2, base + 3});
}

void disc(std::vector<Vec3>& points, std::vector<std::array<int, 3>>& faces, double radius) {
    constexpr int kSegments = 32;
    points.clear();
    faces.clear();
    points.push_back({0, 0, 0});
    for (int i = 0; i < kSegments; ++i) {
        const double angle = 2.0 * 3.14159265358979323846 * double(i) / double(kSegments);
        points.push_back({radius * std::cos(angle), radius * std::sin(angle), 0});
    }
    for (int i = 0; i < kSegments; ++i) faces.push_back({0, 1 + i, 1 + (i + 1) % kSegments});
}

std::size_t findBatch(const GpuScene& scene, std::uint32_t objectId) {
    for (std::size_t index = 0; index < scene.batches.size(); ++index) {
        if (scene.batches[index].objectId == objectId) return index;
    }
    return 0;
}

} // namespace

bool PlaygroundRoom::reset(Vec3 start, Vec3 startVelocity) {
    point_ = start;
    velocity_ = startVelocity;
    traceFrom_ = traceTo_ = point_;
    ghostCount_ = ghostNext_ = 0;
    accumulator_ = 0;
    dragging_ = false;
    resting_ = false;
    haveTrace_ = false;
    return true;
}

void PlaygroundRoom::setDragRay(Vec3 origin, Vec3 direction) {
    dragOrigin_ = {float(origin.x), float(origin.y), float(origin.z), 0};
    dragDirection_ = {float(direction.x), float(direction.y), float(direction.z), 0};
}

void PlaygroundRoom::beginDrag() {
    dragging_ = true;
    updateDrag();
}

// The mouse is a held force, not a physics push: while it holds the point, the
// point is placed where the ray meets the demo plane and gravity does nothing.
void PlaygroundRoom::updateDrag() {
    if (!dragging_) return;
    const Vec3 origin{dragOrigin_[0], dragOrigin_[1], dragOrigin_[2]};
    const Vec3 direction{dragDirection_[0], dragDirection_[1], dragDirection_[2]};
    if (std::abs(direction.z) < 1e-9) return;
    const double distance = (0.0 - origin.z) / direction.z;
    if (!(distance > 0.0)) return;
    const Vec3 hit = origin + direction * distance;
    point_ = {std::clamp(hit.x, -5.0, 5.0), std::max(kSurfaceY, hit.y), 0};
    velocity_ = {};
    resting_ = point_.y <= kSurfaceY;
    haveTrace_ = false;
}

void PlaygroundRoom::tick() {
    const Vec3 from = point_;
    // Forces: gravity, m*g, points down in the air. On the plane the surface
    // pushes back with exactly that weight, so the net force is zero and the
    // point stays where it is instead of sinking.
    const bool supported = dragging_ || resting_;
    const double force = supported ? 0.0 : -kMass * kGravity;
    const double acceleration = force / kMass;
    velocity_.y += acceleration * kTick;
    const Vec3 to = from + velocity_ * kTick;
    traceFrom_ = from;
    if (to.y < kSurfaceY) {
        // Hitscan: walk the segment from here to where the speed would carry
        // the point and find where it meets the plane. The point stops at that
        // crossing. It cannot pass through, however far one tick takes it.
        const double fraction = std::clamp((kSurfaceY - from.y) / (to.y - from.y), 0.0, 1.0);
        point_ = from + (to - from) * fraction;
        point_.y = kSurfaceY;
        velocity_ = {};
        resting_ = true;
        // The line stops at the contact, so what is drawn is the trace that was
        // actually walked rather than the travel that was refused.
        traceTo_ = point_;
    } else {
        point_ = to;
        resting_ = point_.y <= kSurfaceY + 1e-9 && velocity_.y <= 0.0;
        traceTo_ = to;
    }
    haveTrace_ = length(traceTo_ - traceFrom_) > 1e-6;
    if (length(point_ - from) > 1e-9) {
        ghosts_[ghostNext_] = point_;
        ghostNext_ = (ghostNext_ + 1) % ghosts_.size();
        ghostCount_ = std::min(ghostCount_ + 1, ghosts_.size());
    }
}

void PlaygroundRoom::advance(double seconds) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) return;
    if (dragging_) updateDrag();
    // One fixed tick per 20 Hz interval, never more than five after a stall.
    accumulator_ = std::min(accumulator_ + seconds, 0.25);
    int ticks = 0;
    while (accumulator_ >= kTick && ticks < 5) {
        accumulator_ -= kTick;
        tick();
        ++ticks;
    }
}

void PlaygroundRoom::buildScene(const Camera& camera) {
    Rasterizer raster(camera.width, camera.height, colorHex(kSky), camera.focal(), true);
    raster.setCamera(camera);
    std::vector<Vec3> points;
    std::vector<std::array<int, 3>> faces;

    // The room behind the demo: ground below the plane, the plane itself on top.
    quad(points, faces, -6, -4, 6, 0, -0.02);
    raster.setObject(kGroundId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#22302f"));

    points.clear();
    faces.clear();
    quad(points, faces, -6, -0.06, 6, 0.0, -0.01);
    raster.setObject(kSurfaceId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#b9ccc4"));

    // One marker per past tick: the 20 Hz cadence made visible.
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        disc(points, faces, kGhostRadius);
        raster.setObject(kGhostId + static_cast<std::uint32_t>(i), {0, 0}, 0);
        raster.mesh(points, faces, flat("#5c716c"));
    }

    // This tick's hitscan segment, and the point itself in front of it.
    points.clear();
    faces.clear();
    quad(points, faces, 0, -1, 1, 1, 0.004);
    raster.setObject(kTraceId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#6fc6ff"));

    disc(points, faces, kPointRadius);
    raster.setObject(kPointId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#ffd24a"));

    scene_ = raster.takeGpuScene();
    scene_.revision = 1;
    pointBatch_ = findBatch(scene_, kPointId);
    traceBatch_ = findBatch(scene_, kTraceId);
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        ghostBatches_[i] = findBatch(scene_, kGhostId + static_cast<std::uint32_t>(i));
    }
}

void PlaygroundRoom::placeMarkers() {
    scene_.batches[pointBatch_].modelMatrix = translation(point_);
    scene_.batches[traceBatch_].modelMatrix =
        haveTrace_ ? segment(traceFrom_, traceTo_, kTraceHalfWidth) : translation({0, -1000, 0});
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        scene_.batches[ghostBatches_[i]].modelMatrix =
            i < ghostCount_ ? translation(ghosts_[i]) : translation({0, -1000, 0});
    }
}

bool PlaygroundRoom::render(NativeFrame& frame, const ViewState& view, const RoomFreecam& freecam) {
    const Camera camera = freecam.camera(view.width, view.height, view.gpu);
    if (!sceneReady_) {
        buildScene(camera);
        sceneReady_ = true;
    }
    placeMarkers();
    scene_.width = camera.width;
    scene_.height = camera.height;
    scene_.focal = camera.focal();
    scene_.viewMatrix = camera.viewMatrix();
    scene_.covered = false;
    const auto count = scene_.batches.size();
    frame.interfaceInFrame = false;
    if (frame.uiLayer && !frame.uiLayer->empty()) {
        appendUiBatches(*frame.uiLayer, camera, scene_);
        frame.interfaceInFrame = true;
    }
    frame.width = camera.width;
    frame.height = camera.height;
    frame.wantsBgra = true;
    frame.rgb.clear();
    const bool ok = renderGpuScene(scene_, frame);
    scene_.batches.resize(count);
    return ok;
}
} // namespace gtc::native
