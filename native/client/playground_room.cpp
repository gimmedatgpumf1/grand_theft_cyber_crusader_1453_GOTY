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
constexpr double kMass = 1.0;      // kg, one corner of the square
constexpr double kGravity = 9.8;   // m/s^2
constexpr double kSurfaceY = 0.0;  // the plane, y = 0
constexpr double kPointRadius = 0.07;
constexpr double kGhostRadius = 0.032;
constexpr double kTraceHalfWidth = 0.010;
constexpr double kCornerRadius = 0.045;
constexpr double kEdgeHalfWidth = 0.008;
constexpr double kHalfSize = 0.35;      // the square is 0.70 m on a side
constexpr double kBoxMass = 4.0;        // four one-kilogram corners
constexpr double kStartAngle = 0.30;    // radians, so it lands on a corner first
constexpr int kSolveIterations = 12;
constexpr int kImpactsPerTick = 6;
constexpr double kFriction = 0.55;
// A square of four one-kilogram corners about its own centre: m*(w^2+h^2)/12.
constexpr double kBoxInertia =
    kBoxMass * ((2 * kHalfSize) * (2 * kHalfSize) + (2 * kHalfSize) * (2 * kHalfSize)) / 12.0;
constexpr std::uint32_t kGroundId = 1, kSurfaceId = 2, kPointId = 3, kTraceId = 4, kGhostId = 100;
constexpr std::uint32_t kCubeFaceId = 200, kCubeEdgeId = 210, kCubeCornerId = 220;
constexpr const char* kSky = "#101c22";

// Batches carry a model matrix that the renderer reads every frame, so the
// bodies and their markers move without rebuilding or re-uploading any geometry.
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

// The unit square (0,0)-(1,1) mapped onto four corners. If the corners ever stop
// being a square this transform shears, which is the honest picture of what the
// solver actually produced.
std::array<double, 16> corners(Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 u = b - a, v = c - a;
    return {u.x, u.y, 0, 0, v.x, v.y, 0, 0, 0, 0, 1, 0, a.x, a.y, a.z, 1};
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

// A corner's offset from the centre of mass, turned by the body's angle. The
// corners are a property of the body, never a separate thing that can drift.
Vec2 offsetFromCenter(int corner, double angle) {
    const double localX = (corner == 1 || corner == 2) ? kHalfSize : -kHalfSize;
    const double localY = (corner == 2 || corner == 3) ? kHalfSize : -kHalfSize;
    const double c = std::cos(angle), s = std::sin(angle);
    return {localX * c - localY * s, localX * s + localY * c};
}

} // namespace

bool PlaygroundRoom::reset(Vec3 start, Vec3 startVelocity) {
    point_ = previous_ = shown_ = start;
    velocity_ = startVelocity;
    traceFrom_ = traceTo_ = point_;
    ghostCount_ = ghostNext_ = 0;
    accumulator_ = 0;
    dragging_ = false;
    resting_ = false;
    haveTrace_ = false;
    pointFrozen_ = false;
    // The square starts beside the point, turned so a corner leads: the landing
    // is then a corner strike that has to tip, not a flat slap.
    boxCenter_ = boxPreviousCenter_ = boxShownCenter_ = {1.15, 2.05, 0};
    boxAngle_ = boxPreviousAngle_ = boxShownAngle_ = kStartAngle;
    boxVelocity_ = {};
    boxSpin_ = 0;
    cubeShown_ = cornersAt(boxShownCenter_, boxShownAngle_);
    cubeFrozen_ = false;
    return true;
}

std::array<Vec3, 4> PlaygroundRoom::cornersAt(Vec3 center, double angle) const {
    std::array<Vec3, 4> corners{};
    for (int i = 0; i < 4; ++i) {
        const Vec2 offset = offsetFromCenter(i, angle);
        corners[std::size_t(i)] = {center.x + offset.x, center.y + offset.y, 0};
    }
    return corners;
}

std::array<Vec3, 4> PlaygroundRoom::cubeCorners() const {
    return cornersAt(boxCenter_, boxAngle_);
}

// How fast each corner is going: the body's own motion plus the turn about the
// centre. A corner has speed of its own even when the body is not moving.
std::array<Vec3, 4> PlaygroundRoom::cornerVelocity() const {
    std::array<Vec3, 4> velocity{};
    for (int i = 0; i < 4; ++i) {
        const Vec2 offset = offsetFromCenter(i, boxAngle_);
        velocity[std::size_t(i)] = {boxVelocity_.x - boxSpin_ * offset.y,
                                    boxVelocity_.y + boxSpin_ * offset.x, 0};
    }
    return velocity;
}

void PlaygroundRoom::setDragRay(Vec3 origin, Vec3 direction) {
    dragOrigin_ = {float(origin.x), float(origin.y), float(origin.z), 0};
    dragDirection_ = {float(direction.x), float(direction.y), float(direction.z), 0};
}

void PlaygroundRoom::beginDrag() {
    dragging_ = true;
    pointFrozen_ = false;
    updateDrag();
}

// The mouse is a held object, not a physics push: while it holds the point, the
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
    // A dragged point is not a simulated one: it belongs to the mouse and has
    // no previous tick to glide away from.
    previous_ = point_;
    velocity_ = {};
    resting_ = point_.y <= kSurfaceY;
    pointFrozen_ = false;
    haveTrace_ = false;
}

// Ticks happen twenty times a second and frames happen as often as the screen
// allows, so a tick's worth of motion is spread over every frame that falls in
// it. The screen shows each body between the position it had at the previous
// tick and the position the last tick left it in, at the fraction of a tick the
// frame has reached. Nothing is moved by the renderer; this is only what is
// drawn, exactly as Minecraft spreads a 20 Hz tick over rendered frames.
void PlaygroundRoom::interpolate() {
    const double alpha = dragging_ || !sceneReady_
                             ? 1.0
                             : std::clamp(accumulator_ / kTick, 0.0, 1.0);
    shown_ = previous_ + (point_ - previous_) * alpha;
    boxShownCenter_ = boxPreviousCenter_ + (boxCenter_ - boxPreviousCenter_) * alpha;
    boxShownAngle_ = boxPreviousAngle_ + (boxAngle_ - boxPreviousAngle_) * alpha;
    cubeShown_ = cornersAt(boxShownCenter_, boxShownAngle_);
}

void PlaygroundRoom::tick() {
    previous_ = point_;
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
    tickBox();
}

// The square is one body: a centre of mass, an angle, and a moment of inertia
// that says how hard it is to start it turning. Gravity acts at the centre, so
// it never spins the square on its own; a contact acts at a corner, so it does.
// The tick is split at the moment a corner reaches the plane, the contact is
// answered there, and the rest of the tick carries on from the new motion --
// which is why 100 km/s still cannot get a corner through the plane.
void PlaygroundRoom::tickBox() {
    boxPreviousCenter_ = boxCenter_;
    boxPreviousAngle_ = boxAngle_;
    double remaining = kTick;
    for (int impact = 0; impact < kImpactsPerTick; ++impact) {
        if (!(remaining > 1e-12)) break;
        // Forces first, then travel: F = m*a for the centre, torque for the turn.
        boxVelocity_.y -= kGravity * remaining;
        double hitTime = remaining;
        const auto corners = cornersAt(boxCenter_, boxAngle_);
        const auto velocity = cornerVelocity();
        bool hit = false;
        for (std::size_t i = 0; i < 4; ++i) {
            if (!(velocity[i].y < 0) || !(corners[i].y > kSurfaceY)) continue;
            const double time = corners[i].y / (-velocity[i].y);
            if (time < hitTime) { hitTime = time; hit = true; }
        }
        boxCenter_ = boxCenter_ + boxVelocity_ * hitTime;
        boxAngle_ += boxSpin_ * hitTime;
        remaining -= hitTime;
        // Only corners the plane is actually holding push back.
        std::array<bool, 4> touching{};
        const auto placed = cornersAt(boxCenter_, boxAngle_);
        for (std::size_t i = 0; i < 4; ++i) touching[i] = placed[i].y <= kSurfaceY + 1e-9;
        bool any = false;
        for (bool value : touching) any = any || value;
        if (!any) break;
        solveContacts(touching);
        // Nothing is allowed to rest below the plane, whatever the solve did.
        double deepest = 0;
        for (const Vec3& corner : cornersAt(boxCenter_, boxAngle_)) deepest = std::min(deepest, corner.y);
        if (deepest < kSurfaceY) boxCenter_.y += kSurfaceY - deepest;
        if (!hit) break;
    }
    // A body that has been clamped or nudged can end a tick fractionally under
    // the plane; put it back on top rather than let it accumulate.
    double deepest = 0;
    for (const Vec3& corner : cornersAt(boxCenter_, boxAngle_)) deepest = std::min(deepest, corner.y);
    if (deepest < kSurfaceY) boxCenter_.y += kSurfaceY - deepest;
}

// Coulomb contact at the corners the plane is touching. Each corner asks for an
// impulse that stops it approaching; impulses are pushed until every contacted
// corner is either separating or being pushed, and a corner cannot pull. The
// share each corner takes is what decides whether the square settles flat or
// tips: an edge-balanced square needs a negative push at the lifted corner to
// stay put, and since a corner can only push, the square turns instead.
void PlaygroundRoom::solveContacts(std::array<bool, 4> touching) {
    std::array<double, 4> normalImpulse{};
    const auto corners = cornersAt(boxCenter_, boxAngle_);
    for (int iteration = 0; iteration < kSolveIterations; ++iteration) {
        for (std::size_t i = 0; i < 4; ++i) {
            if (!touching[i]) continue;
            const Vec2 r{corners[i].x - boxCenter_.x, corners[i].y - boxCenter_.y};
            const double lever = r.x;  // r cross the upward normal
            const double k = 1.0 / kBoxMass + lever * lever / kBoxInertia;
            const double approach = boxVelocity_.y + boxSpin_ * lever;
            if (!(approach < 0.0)) continue;
            const double impulse = -approach / k;
            normalImpulse[i] += impulse;
            boxVelocity_.y += impulse / kBoxMass;
            boxSpin_ += lever * impulse / kBoxInertia;
        }
    }
    // Friction acts along the plane, limited by how hard the corner is pressed.
    for (std::size_t i = 0; i < 4; ++i) {
        if (!touching[i] || normalImpulse[i] <= 0.0) continue;
        const Vec2 r{corners[i].x - boxCenter_.x, corners[i].y - boxCenter_.y};
        const double lever = -r.y;  // r cross the along-plane direction
        const double k = 1.0 / kBoxMass + lever * lever / kBoxInertia;
        const double slide = boxVelocity_.x + boxSpin_ * lever;
        double impulse = -slide / k;
        const double limit = kFriction * normalImpulse[i];
        impulse = std::clamp(impulse, -limit, limit);
        boxVelocity_.x += impulse / kBoxMass;
        boxSpin_ += lever * impulse / kBoxInertia;
    }
}

// A body with no velocity and no net force cannot do anything next tick, so it
// is not stepped at all until something wakes it.
void PlaygroundRoom::freezeCheck() {
    if (!pointFrozen_ && !dragging_ && resting_ && length(velocity_) < 1e-9 && point_.y <= kSurfaceY) {
        pointFrozen_ = true;
        velocity_ = {};
    }
    if (!cubeFrozen_) {
        // Touching the plane, not moving, not turning: nothing will change next
        // tick, so the square is frozen and its ticks stop costing anything.
        bool touching = false;
        for (const Vec3& corner : cornersAt(boxCenter_, boxAngle_)) {
            touching = touching || std::abs(corner.y - kSurfaceY) < 1e-9;
        }
        cubeFrozen_ = touching && length(boxVelocity_) < 1e-9 && std::abs(boxSpin_) < 1e-9;
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
        // Nothing moves, so there is nothing to simulate. The tick still counts
        // as time passing: the accumulator has already paid for it.
        if (!pointFrozen_) tick();
        else if (!cubeFrozen_) tickBox();
        freezeCheck();
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

    // The square: a face, four edges and one disc per corner, the corners drawn
    // as points because that is what the body is made of.
    points.clear();
    faces.clear();
    quad(points, faces, 0, 0, 1, 1, 0.002);
    raster.setObject(kCubeFaceId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#2c5f74"));

    points.clear();
    faces.clear();
    quad(points, faces, 0, -1, 1, 1, 0.005);
    for (std::size_t i = 0; i < cubeEdgeBatches_.size(); ++i) {
        raster.setObject(kCubeEdgeId + static_cast<std::uint32_t>(i), {0, 0}, 0);
        raster.mesh(points, faces, flat("#9fd8e8"));
    }
    disc(points, faces, kCornerRadius);
    for (std::size_t i = 0; i < cubeCornerBatches_.size(); ++i) {
        raster.setObject(kCubeCornerId + static_cast<std::uint32_t>(i), {0, 0}, 0);
        raster.mesh(points, faces, flat("#ff8a4c"));
    }

    scene_ = raster.takeGpuScene();
    scene_.revision = 1;
    pointBatch_ = findBatch(scene_, kPointId);
    traceBatch_ = findBatch(scene_, kTraceId);
    cubeFaceBatch_ = findBatch(scene_, kCubeFaceId);
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        ghostBatches_[i] = findBatch(scene_, kGhostId + static_cast<std::uint32_t>(i));
    }
    for (std::size_t i = 0; i < cubeEdgeBatches_.size(); ++i) {
        cubeEdgeBatches_[i] = findBatch(scene_, kCubeEdgeId + static_cast<std::uint32_t>(i));
        cubeCornerBatches_[i] = findBatch(scene_, kCubeCornerId + static_cast<std::uint32_t>(i));
    }
}

void PlaygroundRoom::placeMarkers() {
    scene_.batches[pointBatch_].modelMatrix = translation(shown_);
    // The trace belongs to the tick that is being drawn: while the point is
    // still gliding through the last tick, the line runs from the previous
    // position to where that tick's travel was allowed to reach, so the point on
    // screen always sits on the line that was walked.
    const Vec3 traceStart = dragging_ || !sceneReady_ ? traceFrom_ : previous_;
    scene_.batches[traceBatch_].modelMatrix =
        haveTrace_ ? segment(traceStart, traceTo_, kTraceHalfWidth) : translation({0, -1000, 0});
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        scene_.batches[ghostBatches_[i]].modelMatrix =
            i < ghostCount_ ? translation(ghosts_[i]) : translation({0, -1000, 0});
    }
    scene_.batches[cubeFaceBatch_].modelMatrix =
        corners(cubeShown_[0], cubeShown_[1], cubeShown_[3]);
    for (std::size_t i = 0; i < cubeEdgeBatches_.size(); ++i) {
        scene_.batches[cubeEdgeBatches_[i]].modelMatrix =
            segment(cubeShown_[i], cubeShown_[(i + 1) % 4], kEdgeHalfWidth);
        scene_.batches[cubeCornerBatches_[i]].modelMatrix = translation(cubeShown_[i]);
    }
}

bool PlaygroundRoom::render(NativeFrame& frame, const ViewState& view, const RoomFreecam& freecam) {
    const Camera camera = freecam.camera(view.width, view.height, view.gpu);
    if (!sceneReady_) {
        buildScene(camera);
        sceneReady_ = true;
    }
    interpolate();
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
