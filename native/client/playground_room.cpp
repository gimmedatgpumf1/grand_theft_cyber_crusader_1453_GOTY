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
constexpr double kCornerRadius = 0.045;
constexpr double kEdgeHalfWidth = 0.008;
constexpr double kHalfSize = 0.35;    // the cube is 0.70 m on a side
constexpr double kStartAngle = 0.30;  // radians, so a corner leads the landing
constexpr double kPointGrabRadius = 0.14;  // how close the mouse has to be to take it
constexpr double kGrabMargin = 0.06;       // and to take the cube by its edge
// The distance solve runs until every distance is back to its rest length,
// not for a chosen number of passes. The bound is only a guard against a
// numerical corner case looping forever.
constexpr int kMaxDistancePasses = 256;
constexpr double kDistanceTolerance = 1e-9;  // metres
// Below this a corner is not moving in any way that matters: the distance solve
// leaves a few millimetres of wobble on a resting square, and a body doing that
// is asleep rather than moving. Two ticks of it and the body is frozen.
constexpr double kSleepSpeed = 0.08;  // m/s
constexpr int kSleepTicks = 2;
// How much of the closing speed comes back along the contact normal, and how
// much sliding the plane resists. Both are properties of the two surfaces.
constexpr double kRestitution = 0.25;
constexpr double kFriction = 0.5;
constexpr std::uint32_t kGroundId = 1, kSurfaceId = 2, kPointId = 3, kTraceId = 4, kGhostId = 100;
constexpr std::uint32_t kCubeFaceId = 200, kCubeEdgeId = 210, kCubeCornerId = 220;
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

// The unit square (0,0)-(1,1) mapped onto three corners of the cube.
std::array<double, 16> corners(Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 u = b - a, v = c - a;
    return {u.x, u.y, 0, 0, v.x, v.y, 0, 0, 0, 0, 1, 0, a.x, a.y, a.z, 1};
}

std::size_t findBatch(const GpuScene& scene, std::uint32_t objectId) {
    for (std::size_t index = 0; index < scene.batches.size(); ++index) {
        if (scene.batches[index].objectId == objectId) return index;
    }
    return 0;
}

// The six distances that are held: four edges and two diagonals. The diagonals
// are what keeps the cube square instead of letting it fold into a diamond.
constexpr std::array<std::array<int, 2>, 6> kPairs{{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 2}, {1, 3}}};

// Where a segment first meets the square, and the outward normal of the edge it
// meets. The square is convex and its corners are in order, so each edge can be
// tested on its own and the earliest crossing wins. A point already inside is
// reported as a hit at once, with the normal of the nearest edge.
bool segmentHitsSquare(Vec3 from, Vec3 to, const std::array<Vec3, 4>& square, double& hit,
                       Vec3& normal) {
    const auto cross2 = [](Vec3 a, Vec3 b) { return a.x * b.y - a.y * b.x; };
    const auto edgeNormal = [&](int edge) {
        const Vec3 a = square[std::size_t(edge)];
        const Vec3 b = square[std::size_t((edge + 1) % 4)];
        const Vec3 side = b - a;
        const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
        return sideLength > 1e-12 ? Vec3{side.y / sideLength, -side.x / sideLength, 0} : Vec3{};
    };
    // A segment walking from outside must cross an edge; the earliest crossing is
    // the hit. Crossing out of the square is not a hit.
    const Vec3 travel = to - from;
    double earliest = 2.0;
    int crossing = -1;
    for (int edge = 0; edge < 4; ++edge) {
        const Vec3 a = square[std::size_t(edge)];
        const Vec3 b = square[std::size_t((edge + 1) % 4)];
        const Vec3 side = b - a;
        const double denominator = cross2(travel, side);
        if (std::abs(denominator) < 1e-15) continue;
        const Vec3 gap = a - from;
        const double along = cross2(gap, side) / denominator;
        const double across = cross2(gap, travel) / denominator;
        if (along < 0.0 || along > 1.0 || across < 0.0 || across > 1.0) continue;
        // Only a crossing that goes from outside to inside counts.
        const Vec3 outward = edgeNormal(edge);
        if (!(dot(travel, outward) < 0.0)) continue;
        if (along < earliest) { earliest = along; crossing = edge; }
    }
    if (crossing >= 0) {
        hit = earliest;
        normal = edgeNormal(crossing);
        return true;
    }
    // Already inside: every edge must report it as behind itself. One edge
    // behind itself is not enough -- that is just a point somewhere outside.
    bool inside = true;
    int nearest = 0;
    double nearestDistance = -1e30;
    for (int edge = 0; edge < 4; ++edge) {
        const Vec3 outward = edgeNormal(edge);
        if (length(outward) < 0.5) return false;
        const double distance = dot(from - square[std::size_t(edge)], outward);
        if (distance > 0.0) inside = false;
        if (distance > nearestDistance) { nearestDistance = distance; nearest = edge; }
    }
    if (!inside) return false;
    hit = 0.0;
    normal = edgeNormal(nearest);
    return true;
}

// The middle of the four corners. The cube is only its corners, so this is
// worked out from them rather than stored and kept in step.
Vec3 centroid(const std::array<Vec3, 4>& corners) {
    return (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25;
}

} // namespace

bool PlaygroundRoom::reset(Vec3 start, Vec3 startVelocity) {
    point_ = previous_ = shown_ = start;
    velocity_ = startVelocity;
    traceFrom_ = traceTo_ = point_;
    ghostCount_ = ghostNext_ = 0;
    accumulator_ = 0;
    dragTarget_ = DragTarget::None;
    resting_ = false;
    haveTrace_ = false;
    pointFrozen_ = false;
    // The cube starts beside the point, turned so a corner leads: it lands on a
    // corner and has to be held up by the other three.
    const Vec3 center{1.20, 2.05, 0};
    const double c = std::cos(kStartAngle), s = std::sin(kStartAngle);
    const std::array<Vec2, 4> local{{{-kHalfSize, -kHalfSize}, {kHalfSize, -kHalfSize},
                                    {kHalfSize, kHalfSize}, {-kHalfSize, kHalfSize}}};
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec3 corner{center.x + local[i].x * c - local[i].y * s,
                          center.y + local[i].x * s + local[i].y * c, 0};
        cubePosition_[i] = cubePrevious_[i] = cubeShown_[i] = corner;
        cubeVelocity_[i] = {};
    }
    for (std::size_t i = 0; i < kPairs.size(); ++i) {
        cubeRest_[i] = length(cubePosition_[std::size_t(kPairs[i][1])] -
                              cubePosition_[std::size_t(kPairs[i][0])]);
    }
    cubeFrozen_ = false;
    return true;
}

void PlaygroundRoom::resetPoint(Vec3 start, Vec3 startVelocity) {
    point_ = previous_ = shown_ = start;
    velocity_ = startVelocity;
    traceFrom_ = traceTo_ = point_;
    resting_ = false;
    haveTrace_ = false;
    pointFrozen_ = false;
}

void PlaygroundRoom::setDragRay(Vec3 origin, Vec3 direction) {
    dragOrigin_ = {float(origin.x), float(origin.y), float(origin.z), 0};
    dragDirection_ = {float(direction.x), float(direction.y), float(direction.z), 0};
}

// Whichever body is under the ray is the one the mouse takes. The point and the
// cube are both picked by the same rule: the nearest one to where the ray meets
// the demo plane. Grabbing the cube moves all four of its corners together, so
// the distances it is made of are never asked to stretch.
void PlaygroundRoom::beginDrag() {
    const Vec3 origin{dragOrigin_[0], dragOrigin_[1], dragOrigin_[2]};
    const Vec3 direction{dragDirection_[0], dragDirection_[1], dragDirection_[2]};
    if (std::abs(direction.z) < 1e-9) return;
    const double distance = (0.0 - origin.z) / direction.z;
    if (!(distance > 0.0)) return;
    const Vec3 hit = origin + direction * distance;
    const double toPoint = length(Vec3{hit.x - point_.x, hit.y - point_.y, 0});
    double toCube = 1e9;
    for (const Vec3& corner : cubePosition_) {
        toCube = std::min(toCube, length(Vec3{hit.x - corner.x, hit.y - corner.y, 0}));
    }
    const Vec3 middle = centroid(cubePosition_);
    const double toMiddle = length(Vec3{hit.x - middle.x, hit.y - middle.y, 0});
    if (std::min(toCube, toMiddle) < kHalfSize + kGrabMargin && toCube < toPoint) {
        dragTarget_ = DragTarget::Cube;
        planeGrab_ = hit;
        grabOffset_ = Vec3{middle.x - hit.x, middle.y - hit.y, 0};
        cubeFrozen_ = false;
        for (std::size_t i = 0; i < 4; ++i) cubeVelocity_[i] = {};
    } else if (toPoint < kPointGrabRadius) {
        dragTarget_ = DragTarget::Point;
        pointFrozen_ = false;
    } else {
        dragTarget_ = DragTarget::None;
        return;
    }
    updateDrag();
}

// The mouse is a held force, not a physics push: while it holds the point, the
// point is placed where the ray meets the demo plane and gravity does nothing.
void PlaygroundRoom::updateDrag() {
    if (dragTarget_ == DragTarget::None) return;
    const Vec3 origin{dragOrigin_[0], dragOrigin_[1], dragOrigin_[2]};
    const Vec3 direction{dragDirection_[0], dragDirection_[1], dragDirection_[2]};
    if (std::abs(direction.z) < 1e-9) return;
    const double distance = (0.0 - origin.z) / direction.z;
    if (!(distance > 0.0)) return;
    Vec3 hit = origin + direction * distance;
    if (dragTarget_ == DragTarget::Cube) {
        // The cube follows the mouse by its centre, so every corner moves by the
        // same amount and no distance changes. A dragged body has no previous
        // tick to glide away from.
        const Vec3 middle = centroid(cubePosition_);
        const Vec3 center{std::clamp(hit.x + grabOffset_.x, -5.0, 5.0),
                          std::max(kSurfaceY, hit.y + grabOffset_.y), 0};
        const Vec3 move{center.x - middle.x, center.y - middle.y, 0};
        for (std::size_t i = 0; i < 4; ++i) {
            cubePosition_[i] += move;
            cubePrevious_[i] = cubePosition_[i];
            cubeShown_[i] = cubePosition_[i];
        }
        for (std::size_t i = 0; i < 4; ++i) cubeVelocity_[i] = {};
        return;
    }
    point_ = {std::clamp(hit.x, -5.0, 5.0), std::max(kSurfaceY, hit.y), 0};
    // A dragged point is not a simulated one: it belongs to the mouse and has
    // no previous tick to glide away from.
    previous_ = point_;
    velocity_ = {};
    resting_ = point_.y <= kSurfaceY;
    haveTrace_ = false;
}

// Ticks happen twenty times a second and frames happen as often as the screen
// allows, so a tick's worth of motion is spread over every frame that falls in
// it. The screen shows the point between the position it had at the previous
// tick and the position the last tick left it in, at the fraction of a tick the
// frame has reached. Nothing is moved by the renderer; this is only what is
// drawn, exactly as Minecraft spreads a 20 Hz tick over rendered frames.
void PlaygroundRoom::interpolate() {
    // Each body is interpolated on its own. A frozen body is drawn where it is,
    // but that must never stop a body that is still falling from being spread
    // over the frames: one shared switch here is exactly what made the point
    // snap from tick to tick again the moment the cube settled.
    const double fraction = !sceneReady_ ? 1.0 : std::clamp(accumulator_ / kTick, 0.0, 1.0);
    const double pointAlpha = pointFrozen_ || dragTarget_ == DragTarget::Point ? 1.0 : fraction;
    const double cubeAlpha = cubeFrozen_ || dragTarget_ == DragTarget::Cube ? 1.0 : fraction;
    shown_ = previous_ + (point_ - previous_) * pointAlpha;
    for (std::size_t i = 0; i < 4; ++i) {
        cubeShown_[i] = cubePrevious_[i] + (cubePosition_[i] - cubePrevious_[i]) * cubeAlpha;
    }
}

// The point meets the square. The hit is found by walking this tick's segment
// against the square's four edges, before the point is allowed to move, so a
// fast point cannot pass through it. The point carries momentum m*v; the corner
// it struck takes that momentum and the square gives back a push along the
// normal, so the point rebounds and the square is set turning.
void PlaygroundRoom::hitCube(Vec3 from, Vec3 to, double hit) {
    double along = hit;
    Vec3 normal{0, 1, 0};
    if (!segmentHitsSquare(from, to, cubePosition_, along, normal)) return;
    Vec3 contact = from + (to - from) * along;
    // A point that begins the tick already inside is put back outside along the
    // nearest face, so it can never sit inside the square collecting speed.
    if (along <= 0.0) contact = from - normal * 1e-4;
    point_ = contact;
    ++pointCubeContacts_;
    // Which corner carries the blow: the nearest one. The square is four points,
    // so the momentum goes into those points and the fixed distances pass it on.
    std::size_t struck = 0;
    double nearest = 1e30;
    for (std::size_t i = 0; i < 4; ++i) {
        const double distance = length(contact - cubePosition_[i]);
        if (distance < nearest) { nearest = distance; struck = i; }
    }
    const bool held = dragTarget_ == DragTarget::Cube;
    const Vec3 cornerVelocity = held ? Vec3{} : cubeVelocity_[struck];
    const double closing = dot(velocity_ - cornerVelocity, normal);
    if (closing < 0.0) {
        // Equal and opposite impulses. The struck corner takes half of what the
        // square receives and the other three share the rest, so the square as a
        // whole gains exactly the momentum the point loses and is set turning by
        // the uneven share. A held square cannot be pushed, so it takes none.
        constexpr double kStruckShare = 0.5;
        constexpr double kOtherShare = (1.0 - kStruckShare) / 3.0;
        const double cornerInverseMass = held ? 0.0 : kStruckShare / kMass;
        const double impulse = -(1.0 + kRestitution) * closing /
                               (1.0 / kMass + cornerInverseMass);
        velocity_ = velocity_ - normal * (impulse / kMass);
        if (!held) {
            for (std::size_t i = 0; i < 4; ++i) {
                const double share = i == struck ? kStruckShare : kOtherShare;
                cubeVelocity_[i] = cubeVelocity_[i] + normal * (impulse * share / kMass);
            }
            cubeFrozen_ = false;
        }
        // If the exchange still leaves the point heading into the square -- two
        // equal masses cannot both bounce apart -- the surface simply holds it.
        const double still = dot(velocity_, normal);
        if (still < 0.0) velocity_ = velocity_ - normal * still;
    }
    // Nothing may be left sitting exactly on the surface, or the next tick's
    // segment starts inside the square and the point never gets out.
    point_ = point_ - normal * 1e-6;
    resting_ = false;
    pointFrozen_ = false;
    traceTo_ = point_;
}

void PlaygroundRoom::tick() {
    previous_ = point_;
    const Vec3 from = point_;
    // Forces: gravity, m*g, points down in the air. On the plane the surface
    // pushes back with exactly that weight, so the net force is zero and the
    // point stays where it is instead of sinking.
    const bool supported = dragTarget_ == DragTarget::Point || resting_;
    const double force = supported ? 0.0 : -kMass * kGravity;
    const double acceleration = force / kMass;
    velocity_.y += acceleration * kTick;
    Vec3 to = from + velocity_ * kTick;
    traceFrom_ = from;
    // The hitscan checks the square as well as the plane, and whichever comes
    // first is where the point stops.
    double cubeHit = 2.0;
    Vec3 cubeNormal{0, 1, 0};
    const bool meetsCube = segmentHitsSquare(from, to, cubePosition_, cubeHit, cubeNormal);
    const bool meetsPlane = to.y < kSurfaceY;
    const double planeHit = meetsPlane ? std::clamp((kSurfaceY - from.y) / (to.y - from.y), 0.0, 1.0) : 2.0;
    if (meetsCube && cubeHit <= planeHit) {
        // The square is in the way first. The point is placed at the contact and
        // the momentum is exchanged there rather than at the end of the tick.
        point_ = from + (to - from) * cubeHit;
        hitCube(from, to, cubeHit);
        // Whatever travel was left after the hit belongs to the new velocity.
        to = point_;
        traceTo_ = point_;
    } else if (to.y < kSurfaceY) {
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
    if (dragTarget_ != DragTarget::Point && resting_ && length(velocity_) < 1e-9) {
        // No speed and no net force: this point cannot do anything next tick.
        // Freezing also ends the interpolation, so what is drawn is the resting
        // position and not some fraction of the way down the last tick.
        pointFrozen_ = true;
        velocity_ = {};
        previous_ = point_;
        shown_ = point_;
    }
    tickCube();
    haveTrace_ = length(traceTo_ - traceFrom_) > 1e-6;
    if (length(point_ - from) > 1e-9) {
        ghosts_[ghostNext_] = point_;
        ghostNext_ = (ghostNext_ + 1) % ghosts_.size();
        ghostCount_ = std::min(ghostCount_ + 1, ghosts_.size());
    }
}

// The cube is four points of equal mass, one at each corner. Every tick each
// corner is given gravity and asked to travel its own v/20, its own hitscan
// segment is traced against the plane, and then the six fixed distances are
// pushed back to what they were. A corner that moves therefore pulls and pushes
// the other three through those distances: that is the whole of the coupling.
void PlaygroundRoom::tickCube() {
    cubePrevious_ = cubePosition_;
    if (dragTarget_ == DragTarget::Cube) {
        // The mouse owns the cube: it is placed, not simulated, so nothing here
        // may move it or decide it has come to rest.
        for (std::size_t i = 0; i < 4; ++i) {
            cubeVelocity_[i] = {};
            cubePrevious_[i] = cubePosition_[i];
        }
        cubeFrozen_ = false;
        return;
    }
    std::array<bool, 4> onPlane{};
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec3 from = cubePosition_[i];
        cubeVelocity_[i].y -= kGravity * kTick;
        const Vec3 to = from + cubeVelocity_[i] * kTick;
        if (to.y < kSurfaceY) {
            // The same hitscan the point uses, once per corner: the crossing is
            // where that corner stops, however far the tick would have carried it.
            const double fraction = std::clamp((kSurfaceY - from.y) / (to.y - from.y), 0.0, 1.0);
            cubePosition_[i] = from + (to - from) * fraction;
            cubePosition_[i].y = kSurfaceY;
        } else {
            cubePosition_[i] = to;
        }
        onPlane[i] = cubePosition_[i].y <= kSurfaceY + 1e-12;
    }
    // What each corner was doing before the distances were pushed back. A
    // constraint does no work, so the solve is not allowed to hand a corner more
    // speed than it already had; without this the push-back and the plane turn
    // into a spring and the cube rattles for a second after it lands.
    std::array<double, 4> speedBefore{};
    for (std::size_t i = 0; i < 4; ++i) speedBefore[i] = length(cubeVelocity_[i]);
    // Each pass walks every fixed distance once and splits the error evenly
    // between its two corners. Interleaving the plane clamp keeps the solve from
    // pushing a corner through the floor to satisfy a distance. The loop ends
    // when no distance is out by more than a nanometre, so the result does not
    // depend on a pass count chosen by hand.
    for (int pass = 0; pass < kMaxDistancePasses; ++pass) {
        double worst = 0.0;
        for (std::size_t pair = 0; pair < kPairs.size(); ++pair) {
            const std::size_t a = std::size_t(kPairs[pair][0]), b = std::size_t(kPairs[pair][1]);
            const Vec3 delta = cubePosition_[b] - cubePosition_[a];
            const double current = length(delta);
            if (!(current > 1e-12)) continue;
            const Vec3 direction = delta / current;
            const double deviation = current - cubeRest_[pair];
            worst = std::max(worst, std::abs(deviation));
            const Vec3 correction = direction * (deviation * 0.5);
            cubePosition_[a] += correction;
            cubePosition_[b] -= correction;
        }
        if (worst <= kDistanceTolerance) break;
    }
    // The plane is applied once, after the distances are satisfied, instead of
    // inside the loop. Clamping inside the loop fights the distances: every
    // pass pushes the corners apart, the clamp pushes back, and the pair of them
    // pump energy in until the cube flies away.
    for (std::size_t i = 0; i < 4; ++i) {
        if (cubePosition_[i].y < kSurfaceY) {
            cubePosition_[i].y = kSurfaceY;
            onPlane[i] = true;
        }
    }
    for (std::size_t i = 0; i < 4; ++i) {
        Vec3 velocity = (cubePosition_[i] - cubePrevious_[i]) / kTick;
        if (onPlane[i]) {
            // The plane holds this corner: it cannot travel into the plane, and
            // friction takes away the sliding it would need to keep rocking.
            if (velocity.y < 0.0) velocity.y = 0.0;
            const double grip = kFriction * kGravity * kTick;
            velocity.x = std::abs(velocity.x) <= grip ? 0.0
                                                      : velocity.x - std::copysign(grip, velocity.x);
        }
        const double speed = length(velocity);
        if (speed > speedBefore[i] && speed > 1e-12) velocity = velocity * (speedBefore[i] / speed);
        cubeVelocity_[i] = velocity;
    }
    bool touching = false, quiet = true;
    for (std::size_t i = 0; i < 4; ++i) {
        touching = touching || onPlane[i];
        quiet = quiet && length(cubePosition_[i] - cubePrevious_[i]) / kTick < kSleepSpeed;
    }
    // Resting on the plane and no longer going anywhere: there is no force left
    // to resolve, so the cube is frozen and its ticks stop costing anything.
    cubeQuietTicks_ = touching && quiet ? cubeQuietTicks_ + 1 : 0;
    if (cubeQuietTicks_ >= kSleepTicks && dragTarget_ != DragTarget::Cube) {
        cubeFrozen_ = true;
        for (std::size_t i = 0; i < 4; ++i) {
            cubeVelocity_[i] = {};
            cubePrevious_[i] = cubePosition_[i];
            cubeShown_[i] = cubePosition_[i];
        }
    }
}

void PlaygroundRoom::freezeCheck() {
    if (!pointFrozen_ && dragTarget_ != DragTarget::Point && resting_ && length(velocity_) < 1e-9 && point_.y <= kSurfaceY) {
        pointFrozen_ = true;
        velocity_ = {};
    }
}

void PlaygroundRoom::advance(double seconds) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) return;
    if (dragTarget_ != DragTarget::None) updateDrag();
    // One fixed tick per 20 Hz interval, never more than five after a stall.
    accumulator_ = std::min(accumulator_ + seconds, 0.25);
    int ticks = 0;
    while (accumulator_ >= kTick && ticks < 5) {
        accumulator_ -= kTick;
        // A frozen body has nothing to do: no force, no motion, no work.
        if (!pointFrozen_) tick();
        else if (!cubeFrozen_ && dragTarget_ != DragTarget::Cube) tickCube();
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

    // The cube: a face, four edges, and the four corner points it is made of.
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
    const Vec3 traceStart = dragTarget_ == DragTarget::Point || !sceneReady_ ? traceFrom_ : previous_;
    scene_.batches[traceBatch_].modelMatrix =
        haveTrace_ ? segment(traceStart, traceTo_, kTraceHalfWidth) : translation({0, -1000, 0});
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        scene_.batches[ghostBatches_[i]].modelMatrix =
            i < ghostCount_ ? translation(ghosts_[i]) : translation({0, -1000, 0});
    }
    scene_.batches[cubeFaceBatch_].modelMatrix = corners(cubeShown_[0], cubeShown_[1], cubeShown_[3]);
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
