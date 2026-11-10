#include "client/playground_room.h"
#include "client/playground_shapes.h"
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gtc::native {
using namespace playground_detail;
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
constexpr double kPointGrabRadius = 0.14;  // how close the mouse has to be to take it
constexpr double kGrabMargin = 0.06;       // and to take a body by its outline
// The distance solve runs until every distance is back to its rest length,
// not for a chosen number of passes. The bound is only a guard against a
// numerical corner case looping forever.
constexpr int kMaxDistancePasses = 256;
constexpr double kDistanceTolerance = 1e-9;  // metres
// A corner counts as touching the plane when the solve left it there, which is
// within a nanometre of it: the clamp puts it at exactly the plane's height.
constexpr double kContactTolerance = 1e-9;   // metres
// Below this a corner is not moving in any way that matters: the distance solve
// leaves a few millimetres of wobble on a resting body, and a body doing that
// is asleep rather than moving. Two ticks of it and the body is frozen.
constexpr double kSleepSpeed = 0.08;  // m/s
constexpr int kSleepTicks = 2;
// Friction, in the two forms it actually has. A surface holds a body still as
// long as mu_s * N is enough to answer the sideways force; that is static
// friction, and while it holds, nothing moves. Once the pull beats it, the body
// slides and mu_k * N slows it -- tick by tick, never in one instant.
// The point is smooth and keeps its sideways speed for a long way. The solids
// are gripped far harder, and how hard also depends on how much of them is
// touching the floor: the coefficients below are the solids' personal factors,
// and they are scaled by the length of the contact they actually have.
constexpr double kPointStaticFriction = 0.40;
constexpr double kPointKineticFriction = 0.30;
constexpr std::uint32_t kGroundId = 1, kSurfaceId = 2, kPointId = 3, kTraceId = 4, kGhostId = 100;
// Each body gets its own face, edge and corner markers: a body owns a hundred
// object ids, which is more than four corners will ever need.
constexpr std::uint32_t kBodyFaceId = 200, kBodySpan = 100, kBodyEdgeOffset = 10, kBodyCornerOffset = 20;
constexpr const char* kSky = "#101c22";

// Where a segment first meets a convex body, and the outward normal of the edge
// it meets. The corners are in order, so each edge can be tested on its own and
// the earliest crossing wins. A point already inside is reported as a hit at
// once, with the normal of the nearest edge.
bool segmentHitsPolygon(Vec3 from, Vec3 to, const Vec3* corners, std::size_t count, double& hit,
                        Vec3& normal) {
    const auto cross2 = [](Vec3 a, Vec3 b) { return a.x * b.y - a.y * b.x; };
    const auto edgeNormal = [&](std::size_t edge) {
        const Vec3 a = corners[edge];
        const Vec3 b = corners[(edge + 1) % count];
        const Vec3 side = b - a;
        const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
        return sideLength > 1e-12 ? Vec3{side.y / sideLength, -side.x / sideLength, 0} : Vec3{};
    };
    // A segment walking from outside must cross an edge; the earliest crossing is
    // the hit. Crossing out of the body is not a hit.
    const Vec3 travel = to - from;
    double earliest = 2.0;
    std::size_t crossing = count;
    for (std::size_t edge = 0; edge < count; ++edge) {
        const Vec3 a = corners[edge];
        const Vec3 b = corners[(edge + 1) % count];
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
    if (crossing < count) {
        hit = earliest;
        normal = edgeNormal(crossing);
        return true;
    }
    // Already inside: every edge must report it as behind itself. One edge
    // behind itself is not enough -- that is just a point somewhere outside.
    bool inside = true;
    std::size_t nearest = 0;
    double nearestDistance = -1e30;
    for (std::size_t edge = 0; edge < count; ++edge) {
        const Vec3 outward = edgeNormal(edge);
        if (length(outward) < 0.5) return false;
        const double distance = dot(from - corners[edge], outward);
        if (distance > 0.0) inside = false;
        if (distance > nearestDistance) { nearestDistance = distance; nearest = edge; }
    }
    if (!inside) return false;
    hit = 0.0;
    normal = edgeNormal(nearest);
    return true;
}

// Which way round a body is, from the direction of its first edge.
double edgeAngle(const Vec3* corners) {
    const Vec3 side = corners[1] - corners[0];
    return std::atan2(side.y, side.x);
}

// The middle of a body's corners. A body is only its corners, so this is worked
// out from them rather than stored and kept in step.
Vec3 centroid(const Vec3* corners, std::size_t count) {
    Vec3 sum{};
    for (std::size_t i = 0; i < count; ++i) sum += corners[i];
    return sum / static_cast<double>(count);
}

} // namespace

Vec3 PlaygroundRoom::bodyCorner(std::size_t body, std::size_t index) const {
    return body < kBodyCount && index < bodies_[body].count ? bodies_[body].shown[index] : Vec3{};
}

Vec3 PlaygroundRoom::bodySimulatedCorner(std::size_t body, std::size_t index) const {
    return body < kBodyCount && index < bodies_[body].count ? bodies_[body].position[index] : Vec3{};
}

Vec3 PlaygroundRoom::bodyCornerVelocity(std::size_t body, std::size_t index) const {
    return body < kBodyCount && index < bodies_[body].count ? bodies_[body].velocity[index] : Vec3{};
}

Vec3 PlaygroundRoom::bodyMiddle(std::size_t body) const {
    if (body >= kBodyCount) return Vec3{};
    return centroid(bodies_[body].position.data(), bodies_[body].count);
}

bool PlaygroundRoom::reset(Vec3 start, Vec3 startVelocity) {
    point_ = previous_ = shown_ = start;
    velocity_ = startVelocity;
    traceFrom_ = traceTo_ = point_;
    ghostCount_ = ghostNext_ = 0;
    accumulator_ = 0;
    dragTarget_ = DragTarget::None;
    restingBody_ = -1;
    resting_ = false;
    haveTrace_ = false;
    pointFrozen_ = false;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        const Shape& shape = kShapes[index];
        body.count = shape.count;
        body.pairCount = shape.pairCount;
        body.local = shape.local;
        body.pairs = shape.pairs;
        const double c = std::cos(shape.angle), s = std::sin(shape.angle);
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 local = shape.local[i];
            const Vec3 corner{shape.middle.x + local.x * c - local.y * s,
                              shape.middle.y + local.x * s + local.y * c, 0};
            body.position[i] = body.previous[i] = body.shown[i] = corner;
            body.velocity[i] = {};
        }
        for (std::size_t pair = 0; pair < body.pairCount; ++pair) {
            body.rest[pair] = length(body.position[std::size_t(body.pairs[pair][1])] -
                                     body.position[std::size_t(body.pairs[pair][0])]);
        }
        body.staticFriction = shape.staticFriction;
        body.kineticFriction = shape.kineticFriction;
        body.frozen = false;
        body.quietTicks = 0;
        body.contacts = 0;
    }
    return true;
}

void PlaygroundRoom::resetPoint(Vec3 start, Vec3 startVelocity) {
    point_ = previous_ = shown_ = start;
    velocity_ = startVelocity;
    traceFrom_ = traceTo_ = point_;
    resting_ = false;
    restingBody_ = -1;
    haveTrace_ = false;
    pointFrozen_ = false;
}

void PlaygroundRoom::setDragRay(Vec3 origin, Vec3 direction) {
    dragOrigin_ = {float(origin.x), float(origin.y), float(origin.z), 0};
    dragDirection_ = {float(direction.x), float(direction.y), float(direction.z), 0};
}

// Whichever body is under the click is the one the mouse takes, and only a click
// well inside a body takes it over the point sitting on it. Taking a body moves
// all of its corners together, so the distances it is made of are never asked to
// stretch.
void PlaygroundRoom::beginDrag() {
    const Vec3 origin{dragOrigin_[0], dragOrigin_[1], dragOrigin_[2]};
    const Vec3 direction{dragDirection_[0], dragDirection_[1], dragDirection_[2]};
    if (std::abs(direction.z) < 1e-9) return;
    const double distance = (0.0 - origin.z) / direction.z;
    if (!(distance > 0.0)) return;
    const Vec3 hit = origin + direction * distance;
    const double toPoint = length(Vec3{hit.x - point_.x, hit.y - point_.y, 0});
    // How far outside a body's outline the click is: negative inside it.
    const auto outlineDistance = [&](const Body& body) {
        double outermost = -1e30;
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 a = body.position[i];
            const Vec3 b = body.position[(i + 1) % body.count];
            const Vec3 side = b - a;
            const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
            if (!(sideLength > 1e-12)) return 1e30;
            const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
            outermost = std::max(outermost, dot(hit - a, outward));
        }
        return outermost;
    };
    std::size_t picked = kBodyCount;
    double pickedDistance = 1e30;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const double outline = outlineDistance(bodies_[index]);
        if (outline < pickedDistance) { pickedDistance = outline; picked = index; }
    }
    const bool pointNear = toPoint < kPointGrabRadius;
    const bool onBody = picked < kBodyCount && pickedDistance <= kGrabMargin;
    // A point resting on a face is still the point: a click near it must not be
    // swallowed by the body it is standing on.
    if (onBody && (!pointNear || -pickedDistance > kPointGrabRadius)) {
        Body& body = bodies_[picked];
        dragTarget_ = DragTarget::Body;
        dragBody_ = picked;
        planeGrab_ = hit;
        const Vec3 middle = centroid(body.position.data(), body.count);
        grabOffset_ = Vec3{middle.x - hit.x, middle.y - hit.y, 0};
        body.frozen = false;
        body.quietTicks = 0;
        for (std::size_t i = 0; i < body.count; ++i) body.velocity[i] = {};
    } else if (pointNear) {
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
    const Vec3 hit = origin + direction * distance;
    if (dragTarget_ == DragTarget::Body) {
        // A body follows the mouse by its middle, so every corner moves by the
        // same amount and no distance changes. A dragged body has no previous
        // tick to glide away from, and it must not be running its sleep test.
        Body& body = bodies_[dragBody_ < kBodyCount ? dragBody_ : 0];
        const Vec3 middle = centroid(body.position.data(), body.count);
        const Vec3 center{std::clamp(hit.x + grabOffset_.x, -5.0, 5.0),
                          std::max(kSurfaceY, hit.y + grabOffset_.y), 0};
        const Vec3 move{center.x - middle.x, center.y - middle.y, 0};
        for (std::size_t i = 0; i < body.count; ++i) {
            body.position[i] += move;
            body.previous[i] = body.position[i];
            body.shown[i] = body.position[i];
            body.velocity[i] = {};
        }
        return;
    }
    point_ = {std::clamp(hit.x, -5.0, 5.0), std::max(kSurfaceY, hit.y), 0};
    // A dragged point is not a simulated one: it belongs to the mouse and has
    // no previous tick to glide away from.
    previous_ = point_;
    velocity_ = {};
    resting_ = point_.y <= kSurfaceY;
    restingBody_ = -1;
    haveTrace_ = false;
}

// Ticks happen twenty times a second and frames happen as often as the screen
// allows, so a tick's worth of motion is spread over every frame that falls in
// it. The screen shows the point between the position it had at the previous
// tick and the position the last tick left it in, at the fraction of a tick the
// frame has reached. Nothing is moved by the renderer; this is only what is
// drawn, exactly as Minecraft spreads a 20 Hz tick over rendered frames.
void PlaygroundRoom::interpolate() {
    const double fraction = !sceneReady_ ? 1.0 : std::clamp(accumulator_ / kTick, 0.0, 1.0);
    const double pointAlpha = pointFrozen_ || dragTarget_ == DragTarget::Point ? 1.0 : fraction;
    shown_ = previous_ + (point_ - previous_) * pointAlpha;
    // Each body is interpolated on its own. A frozen body is drawn where it is,
    // but that must never stop a body that is still falling from being spread
    // over the frames: one shared switch here is exactly what made the point
    // snap from tick to tick again the moment the square settled.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        const double alpha = body.frozen || bodyHeld(index) ? 1.0 : fraction;
        interpolateBody(body, alpha);
    }
}

void PlaygroundRoom::interpolateBody(Body& body, double alpha) {
    // A body is drawn as it is. Interpolating its corners straight from tick to
    // tick pulls them along chords, so a turning body looks like it is being
    // squashed and stretched; interpolating where it is and how far round it has
    // turned, and rebuilding the corners from those, cannot.
    if (alpha >= 1.0) {
        body.shown = body.position;
        return;
    }
    const Vec3 from = centroid(body.previous.data(), body.count);
    const Vec3 to = centroid(body.position.data(), body.count);
    const double fromAngle = edgeAngle(body.previous.data());
    double turn = edgeAngle(body.position.data()) - fromAngle;
    while (turn > 3.14159265358979323846) turn -= 2.0 * 3.14159265358979323846;
    while (turn < -3.14159265358979323846) turn += 2.0 * 3.14159265358979323846;
    const Vec3 centre = from + (to - from) * alpha;
    const double angle = fromAngle + turn * alpha;
    const double c = std::cos(angle), s = std::sin(angle);
    for (std::size_t i = 0; i < body.count; ++i) {
        body.shown[i] = {centre.x + body.local[i].x * c - body.local[i].y * s,
                         centre.y + body.local[i].x * s + body.local[i].y * c, 0};
    }
}

// The point meets a body. The hit is found by walking this tick's segment
// against the body's edges, before the point is allowed to move, so a fast point
// cannot pass through it. The corner it struck takes the momentum and the body
// gives back a push along the normal, so the point is stopped and the body is
// set turning.
void PlaygroundRoom::hitBody(std::size_t index, Vec3 from, Vec3 to, double hit) {
    Body& body = bodies_[index];
    double along = hit;
    Vec3 normal{0, 1, 0};
    if (!segmentHitsPolygon(from, to, body.position.data(), body.count, along, normal)) return;
    // A point that begins the tick already inside is put back outside along the
    // nearest face, so it can never sit inside a body collecting speed. The
    // normal points out of the body, so getting out means moving along it.
    Vec3 contact = from + (to - from) * along;
    if (along <= 0.0) contact = from + normal * 1e-4;
    point_ = contact;
    ++body.contacts;
    restingBody_ = -1;
    // Which corner carries the blow: the nearest one. The body is its points, so
    // the momentum goes into those points and the fixed distances pass it on.
    std::size_t struck = 0;
    double nearest = 1e30;
    for (std::size_t i = 0; i < body.count; ++i) {
        const double distance = length(contact - body.position[i]);
        if (distance < nearest) { nearest = distance; struck = i; }
    }
    // A body standing on the plane cannot be pushed down into it: the plane
    // would have to give way, and it will not. So a blow from above is answered
    // by the plane, not by the body, and the body does not move at all. This is
    // also why the square used to hop: the impulse drove its corners into the
    // floor, the floor put them back, and the distance solve turned that
    // correction into a 16 mm jump.
    bool supported = false;
    for (std::size_t i = 0; i < body.count; ++i) supported = supported || body.position[i].y <= kSurfaceY + 1e-9;
    const bool held = dragTarget_ == DragTarget::Body;
    const bool blocked = held || (supported && normal.y > 0.5);
    const Vec3 cornerVelocity = blocked ? Vec3{} : body.velocity[struck];
    const double closing = dot(velocity_ - cornerVelocity, normal);
    if (closing < 0.0) {
        // The contact takes exactly the approach speed and gives none of it
        // back, which is what the plane does to the point. Two surfaces that
        // stop the same point differently is the reason it bounced on the square
        // and only sat down on the plane.
        // Equal and opposite impulses. The struck corner takes half of what the
        // body receives and the other corners share the rest, so the body as a
        // whole gains exactly the momentum the point loses and is set turning by
        // the uneven share. A held body cannot be pushed, so it takes none.
        constexpr double kStruckShare = 0.5;
        const double otherShare = (1.0 - kStruckShare) / static_cast<double>(body.count - 1);
        const double cornerInverseMass = blocked ? 0.0 : kStruckShare / kMass;
        const double impulse = -closing / (1.0 / kMass + cornerInverseMass);
        // The normal points out of the body, towards the point: the point is
        // pushed along it and the body is pushed the other way. Getting this
        // sign backwards drove the point into the surface instead of away from
        // it, which is why it never bounced and why the square was shoved into
        // the floor hard enough to hop.
        velocity_ = velocity_ + normal * (impulse / kMass);
        if (!blocked) {
            for (std::size_t i = 0; i < body.count; ++i) {
                const double share = i == struck ? kStruckShare : otherShare;
                body.velocity[i] = body.velocity[i] - normal * (impulse * share / kMass);
            }
            body.frozen = false;
        }
        // If the exchange still leaves the point heading into the body -- two
        // equal masses cannot both bounce apart -- the surface simply holds it.
        const double still = dot(velocity_, normal);
        if (still < 0.0) velocity_ = velocity_ - normal * still;
    }
    // Nothing may be left sitting exactly on the surface, or the next tick's
    // segment starts inside the body and the point never gets out.
    point_ = point_ - normal * 1e-6;
    resting_ = false;
    pointFrozen_ = false;
    // Sliding along the face it just met: the face holds it up across itself and
    // rubs it along itself. N across a tilted face is only the part of the weight
    // pressing into that face, so a 45 degree slope grips less than the floor
    // does, and the point keeps most of the speed it built falling.
    {
        const Vec3 across = velocity_ - normal * dot(velocity_, normal);
        const double speed = length(across);
        const double span = kTick * std::clamp(1.0 - along, 0.0, 1.0);
        const double pressed = kGravity * std::clamp(std::abs(normal.y), 0.0, 1.0);
        const double hold = kPointStaticFriction * pressed * span;
        const double slide = kPointKineticFriction * pressed * span;
        if (speed <= hold) velocity_ = velocity_ - across;
        else velocity_ = velocity_ - across * std::min(1.0, slide / speed);
    }
    // If the contact has taken all the speed there was, the body is holding the
    // point up exactly as the plane would, and it is allowed to stay there. The
    // same "slow enough to be at rest" rule the plane uses. Testing for an exact
    // zero is wrong: the impulse leaves a few billionths of a metre per second
    // behind, and that is not motion, it is rounding.
    if (length(velocity_) < kSleepSpeed && normal.y > 0.5) {
        double height = 0.0;
        if (bodyTopAt(index, point_.x, height)) {
            // Sit exactly on the face that stopped it, so the next tick starts
            // on the surface instead of inside it.
            velocity_ = {};
            resting_ = true;
            restingBody_ = int(index);
            supportNormalY_ = std::abs(normal.y);
            point_ = {point_.x, height, 0};
        }
    }
    traceTo_ = point_;
}

// The height of a body's top surface directly above a given x, taken from the
// two corners of whichever side is on top. False when that x is not over the
// body at all.
bool PlaygroundRoom::bodyTopAt(std::size_t index, double x, double& height) const {
    const Body& body = bodies_[index];
    for (std::size_t side = 0; side < body.count; ++side) {
        const Vec3 a = body.position[side];
        const Vec3 b = body.position[(side + 1) % body.count];
        const Vec3 along = b - a;
        const double span = along.x;
        if (std::abs(span) < 1e-12) continue;
        // A side is the top only if it faces upwards. Comparing heights against
        // the corners instead never works on a face that is the slightest bit
        // tilted, and the top of a settled body is exactly that.
        const double normalY = -along.x / std::sqrt(along.x * along.x + along.y * along.y);
        if (normalY < 0.5) continue;
        const double position = (x - a.x) / span;
        if (position < 0.0 || position > 1.0) continue;
        height = a.y + along.y * position;
        return true;
    }
    return false;
}

// Whether a body is still directly under the point, close enough to be holding
// it up. Used to decide whether a point resting on a body stays supported or has
// to start falling again.
bool PlaygroundRoom::supportedBy(std::size_t index) const {
    const Body& body = bodies_[index];
    bool onGround = false;
    for (std::size_t i = 0; i < body.count; ++i) onGround = onGround || body.position[i].y <= kSurfaceY + 1e-6;
    if (!onGround) return false;
    double height = 0.0;
    if (!bodyTopAt(index, point_.x, height)) return false;
    return std::abs(point_.y - height) < 1e-4;
}

void PlaygroundRoom::tick() {
    previous_ = point_;
    const Vec3 from = point_;
    // A point resting on a body stays there only while that body is still under
    // it. Move the body away and the point falls.
    if (restingBody_ >= 0 && dragTarget_ != DragTarget::Point &&
        !supportedBy(std::size_t(restingBody_))) {
        restingBody_ = -1;
        resting_ = false;
        pointFrozen_ = false;
    }
    // Forces: gravity, m*g, points down in the air. On the plane the surface
    // pushes back with exactly that weight, so the net force is zero and the
    // point stays where it is instead of sinking.
    const bool supported = dragTarget_ == DragTarget::Point || resting_;
    const double force = supported ? 0.0 : -kMass * kGravity;
    const double acceleration = force / kMass;
    velocity_.y += acceleration * kTick;
    Vec3 to = from + velocity_ * kTick;
    traceFrom_ = from;
    // The hitscan checks every body as well as the plane, and whichever comes
    // first is where the point stops.
    double bodyHit = 2.0;
    Vec3 bodyNormal{0, 1, 0};
    std::size_t hitIndex = kBodyCount;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        double along = 2.0;
        Vec3 normal{0, 1, 0};
        const Body& body = bodies_[index];
        if (segmentHitsPolygon(from, to, body.position.data(), body.count, along, normal) && along < bodyHit) {
            bodyHit = along;
            bodyNormal = normal;
            hitIndex = index;
        }
    }
    (void)bodyNormal;
    const bool meetsPlane = to.y < kSurfaceY;
    const double planeHit = meetsPlane ? std::clamp((kSurfaceY - from.y) / (to.y - from.y), 0.0, 1.0) : 2.0;
    if (hitIndex < kBodyCount && bodyHit <= planeHit) {
        // The body is in the way first. The point is placed at the contact and
        // the momentum is exchanged there rather than at the end of the tick.
        point_ = from + (to - from) * bodyHit;
        hitBody(hitIndex, from, to, bodyHit);
        // The contact has only used part of the tick, and what is left of it
        // belongs to the speed the contact left behind. Walking it is what lets
        // a point stopped by a tilted face run on down the face instead of being
        // parked at the contact and creeping; the walk still stops at anything
        // it runs into, so nothing is passed through.
        double remaining = 1.0 - bodyHit;
        for (int step = 0; step < 3 && remaining > 1e-9; ++step) {
            const Vec3 next = point_ + velocity_ * (kTick * remaining);
            double first = 2.0;
            Vec3 normal{0, 1, 0};
            for (std::size_t index = 0; index < kBodyCount; ++index) {
                const Body& other = bodies_[index];
                double along = 2.0;
                Vec3 facing{0, 1, 0};
                if (segmentHitsPolygon(point_, next, other.position.data(), other.count, along, facing) &&
                    along < first) {
                    first = along;
                    normal = facing;
                }
            }
            if (next.y < kSurfaceY && std::abs(next.y - point_.y) > 1e-12) {
                const double part = std::clamp((kSurfaceY - point_.y) / (next.y - point_.y), 0.0, 1.0);
                if (part < first) {
                    point_ = point_ + (next - point_) * part;
                    point_.y = kSurfaceY;
                    // The floor answers the part of the motion that is into it.
                    // What is running along it is the point's own kinetic
                    // energy and the floor does not get to delete it: friction
                    // takes it away, and friction takes time.
                    velocity_.y = 0.0;
                    resting_ = true;
                    restingBody_ = -1;
                    supportNormalY_ = 1.0;
                    slideSpan_ += kTick * remaining * (1.0 - part);
                    break;
                }
            }
            if (first > 1.0) {
                point_ = next;
                break;
            }
            point_ = point_ + (next - point_) * first;
            velocity_ = velocity_ - normal * dot(velocity_, normal);
            remaining *= 1.0 - first;
        }
        to = point_;
        traceTo_ = point_;
    } else if (to.y < kSurfaceY) {
        // Hitscan: walk the segment from here to where the speed would carry
        // the point and find where it meets the plane. The point stops at that
        // crossing. It cannot pass through, however far one tick takes it.
        const double fraction = std::clamp((kSurfaceY - from.y) / (to.y - from.y), 0.0, 1.0);
        point_ = from + (to - from) * fraction;
        point_.y = kSurfaceY;
        // Only the speed into the floor is answered here. Everything running
        // along the floor is left alone; that is the point's kinetic energy and
        // it is friction's job to take it, not the contact's.
        velocity_.y = 0.0;
        resting_ = true;
        restingBody_ = -1;
        supportNormalY_ = 1.0;
        slideSpan_ = kTick * (1.0 - fraction);
        // The line stops at the contact, so what is drawn is the trace that was
        // actually walked rather than the travel that was refused.
        traceTo_ = point_;
    } else if (resting_ && restingBody_ >= 0) {
        // Standing on a body with nothing pushing it: the body holds its weight,
        // and it keeps whatever sideways motion it has. Friction takes that.
        point_ = from + Vec3{velocity_.x, 0, 0} * kTick;
        velocity_.y = 0.0;
        slideSpan_ = kTick;
        traceTo_ = point_;
    } else {
        point_ = to;
        resting_ = point_.y <= kSurfaceY + 1e-9 && velocity_.y <= 0.0;
        if (resting_) {
            // Already down and staying down: it is in contact for the whole
            // tick, so friction acts for the whole tick.
            slideSpan_ = kTick;
            supportNormalY_ = 1.0;
        }
        traceTo_ = to;
    }
    // Friction on the point, applied for the part of the tick it was actually
    // touching something. N is the part of its weight the surface carries: all
    // of it on the floor or on a flat face, less across a tilted one. If mu_s * N
    // can answer the sideways motion it is held still; otherwise mu_k * N takes
    // speed away over time, tick after tick, which is the whole point.
    if (slideSpan_ > 0.0 && dragTarget_ != DragTarget::Point) {
        const double pressed = kGravity * std::clamp(std::abs(supportNormalY_), 0.0, 1.0);
        const double hold = kPointStaticFriction * pressed * slideSpan_;
        const double slide = kPointKineticFriction * pressed * slideSpan_;
        if (std::abs(velocity_.x) <= hold) {
            velocity_.x = 0.0;
        } else {
            const double lost = std::copysign(slide, velocity_.x);
            velocity_.x = std::abs(velocity_.x) <= slide ? 0.0 : velocity_.x - lost;
        }
    }
    slideSpan_ = 0.0;
    if (dragTarget_ != DragTarget::Point && resting_ && length(velocity_) < kSleepSpeed) {
        // No speed and no net force: this point cannot do anything next tick.
        // Freezing also ends the interpolation, so what is drawn is the resting
        // position and not some fraction of the way down the last tick.
        pointFrozen_ = true;
        velocity_ = {};
        previous_ = point_;
        shown_ = point_;
    }
    for (std::size_t index = 0; index < kBodyCount; ++index) tickBody(index);
    haveTrace_ = length(traceTo_ - traceFrom_) > 1e-6;
    if (length(point_ - from) > 1e-9) {
        ghosts_[ghostNext_] = point_;
        ghostNext_ = (ghostNext_ + 1) % ghosts_.size();
        ghostCount_ = std::min(ghostCount_ + 1, ghosts_.size());
    }
}

// A body is its corner points of equal mass. Every tick each corner is given
// gravity and asked to travel its own v/20, its own hitscan segment is traced
// against the plane, and then the fixed distances are pushed back to what they
// were. A corner that moves therefore pulls and pushes the others through those
// distances: that is the whole of the coupling.
void PlaygroundRoom::tickBody(std::size_t index) {
    Body& body = bodies_[index];
    body.previous = body.position;
    const std::size_t count = body.count;
    if (bodyHeld(index)) {
        // The mouse owns this body: it is placed, not simulated, so nothing here
        // may move it or decide it has come to rest.
        for (std::size_t i = 0; i < count; ++i) {
            body.velocity[i] = {};
            body.previous[i] = body.position[i];
        }
        body.frozen = false;
        return;
    }
    std::array<bool, kMaxCorners> onPlane{};
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 from = body.position[i];
        body.velocity[i].y -= kGravity * kTick;
        const Vec3 to = from + body.velocity[i] * kTick;
        if (to.y < kSurfaceY) {
            // The same hitscan the point uses, once per corner: the crossing is
            // where that corner stops, however far the tick would have carried it.
            const double fraction = std::clamp((kSurfaceY - from.y) / (to.y - from.y), 0.0, 1.0);
            body.position[i] = from + (to - from) * fraction;
            body.position[i].y = kSurfaceY;
        } else {
            body.position[i] = to;
        }
        onPlane[i] = body.position[i].y <= kSurfaceY + 1e-12;
    }
    // What each corner was doing before the distances were pushed back. A
    // constraint does no work, so the solve is not allowed to hand a corner more
    // speed than it already had; without this the push-back and the plane turn
    // into a spring and the body rattles for a second after it lands.
    std::array<double, kMaxCorners> speedBefore{};
    for (std::size_t i = 0; i < count; ++i) speedBefore[i] = length(body.velocity[i]);
    // The shape and the plane are solved together until both are satisfied. A
    // pass pushes every fixed distance back to its rest length, then puts any
    // corner the pass pushed through the plane back on it, and that repeats
    // until neither is out by more than a nanometre. Solving the distances and
    // then clamping once -- or clamping inside the distance loop without
    // checking the distances again -- is what leaves a body permanently out of
    // shape: it is the clamp that opens the distance error, so the distance
    // solve has to see the clamp.
    // How far the plane lifted each corner. Lifting a corner is a correction,
    // not motion: the body did not travel upwards, the floor put it back. That
    // lift has to be kept so it can be taken back out of the velocity, or a
    // corner pushed down by a blow turns into the whole body hopping upwards.
    // Which corners the plane is holding up. It holds their height and nothing
    // else -- a corner standing on the floor can still slide along it -- so a
    // fixed distance that wants to move such a corner downwards has to put that
    // motion into the corner at the other end instead. Sharing it equally is
    // what leaves a tall body balanced on a corner the floor cannot carry,
    // because the corner it leaned on never actually went anywhere.
    std::array<bool, kMaxCorners> held{};
    for (std::size_t i = 0; i < count; ++i) held[i] = onPlane[i];
    std::array<double, kMaxCorners> lift{};
    for (int pass = 0; pass < kMaxDistancePasses; ++pass) {
        double worstDistance = 0.0;
        // Both ways round every pass. Pushing the distances back in one fixed
        // order lets the order itself move the body: whatever the first pair
        // does, the next one is projected onto a slightly different shape, and
        // over a second that steady lean adds up. Sweeping forwards and then
        // backwards takes it out again, so no pair is ever favoured.
        for (int step = 0; step < int(body.pairCount) * 2; ++step) {
            const std::size_t pair = step < int(body.pairCount) ? std::size_t(step)
                                                                : std::size_t(int(body.pairCount) * 2 - 1 - step);
            const std::size_t a = std::size_t(body.pairs[pair][0]), b = std::size_t(body.pairs[pair][1]);
            const Vec3 delta = body.position[b] - body.position[a];
            const double current = length(delta);
            if (!(current > 1e-12)) continue;
            const Vec3 direction = delta / current;
            const double deviation = current - body.rest[pair];
            if (step < int(body.pairCount)) worstDistance = std::max(worstDistance, std::abs(deviation));
            // How much of the correction each end can take: half each across the
            // floor, but none of it downwards at a corner the plane is holding.
            const double acrossA = 0.5, acrossB = 0.5;
            const double downA = held[a] ? 0.0 : 0.5, downB = held[b] ? 0.0 : 0.5;
            const double reach = 2.0 * acrossA * direction.x * direction.x +
                                 (downA + downB) * direction.y * direction.y;
            if (!(reach > 1e-12)) continue;
            const double share = deviation / reach;
            body.position[a] += Vec3{direction.x * share * acrossA, direction.y * share * downA, 0};
            body.position[b] -= Vec3{direction.x * share * acrossB, direction.y * share * downB, 0};
        }
        double deepest = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (body.position[i].y < kSurfaceY) {
                const double amount = kSurfaceY - body.position[i].y;
                deepest = std::max(deepest, amount);
                lift[i] += amount;
                body.position[i].y = kSurfaceY;
                onPlane[i] = true;
                held[i] = true;
            }
        }
        if (worstDistance <= kDistanceTolerance && deepest <= kDistanceTolerance) break;
    }
    // How much of this body is lying on the floor: the length of the contact
    // between the corners that are actually on it. A longer contact grips
    // harder, so the square's 0.70 m footprint is the reference and the
    // triangle's 2.10 m footprint gets three times its friction for the same
    // coefficients.
    double leftmost = 1e30, rightmost = -1e30;
    bool touchingPlane = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (body.position[i].y > kSurfaceY + kContactTolerance) continue;
        touchingPlane = true;
        leftmost = std::min(leftmost, body.position[i].x);
        rightmost = std::max(rightmost, body.position[i].x);
    }
    const double footprint = touchingPlane ? std::max(0.0, rightmost - leftmost) / kCubeSide : 0.0;
    const double holdSpeed = body.staticFriction * footprint * kGravity * kTick;
    const double slideSpeed = body.kineticFriction * footprint * kGravity * kTick;

    // Which corners are touching the plane now, after the solve has finished
    // moving them. A clamp from an earlier pass is not a contact: the solve may
    // have lifted that corner back off the plane, and a corner in the air has to
    // keep falling instead of being held and rubbed by the floor.
    for (std::size_t i = 0; i < count; ++i) onPlane[i] = body.position[i].y <= kSurfaceY + kContactTolerance;
    for (std::size_t i = 0; i < count; ++i) {
        Vec3 velocity = (body.position[i] - body.previous[i]) / kTick;
        // Take the floor's correction back out: what is left is what the corner
        // was actually doing.
        velocity.y -= lift[i] / kTick;
        if (onPlane[i]) {
            // The plane holds this corner: it cannot travel into the plane.
            // Sideways is friction's business, and that is done for the body as
            // a whole just below.
            if (velocity.y < 0.0) velocity.y = 0.0;
        }
        const double speed = length(velocity);
        if (speed > speedBefore[i] && speed > 1e-12) velocity = velocity * (speedBefore[i] / speed);
        body.velocity[i] = velocity;
    }
    // Friction is a force at the contact patch, and the patch pushes the body,
    // not one corner of it. Taking the slide off each corner on its own does
    // nothing once the floor has pinned that corner, which is why the body used
    // to stop dead no matter what coefficients it had: the body's own sideways
    // motion is what mu_s has to hold and mu_k has to slow, and the fixed
    // distances carry that to every corner.
    if (touchingPlane) {
        Vec3 middleVelocity{};
        for (std::size_t i = 0; i < count; ++i) middleVelocity += body.velocity[i];
        middleVelocity = middleVelocity / static_cast<double>(count);
        const double sideways = middleVelocity.x;
        if (std::abs(sideways) <= holdSpeed) {
            // mu_s * N answers it: it does not move at all.
            for (std::size_t i = 0; i < count; ++i) body.velocity[i].x -= sideways;
        } else {
            // Sliding: mu_k * N takes the same speed off every corner.
            for (std::size_t i = 0; i < count; ++i) body.velocity[i].x -= std::copysign(slideSpeed, sideways);
        }
    }
    bool quiet = true;
    for (std::size_t i = 0; i < count; ++i) {
        quiet = quiet && length(body.position[i] - body.previous[i]) / kTick < kSleepSpeed;
    }
    // A body is carried by the plane only if its weight passes between the
    // corners that are actually touching it. Balanced on one corner it is not
    // held up at all -- it topples -- so it is not allowed to freeze there and
    // the solve is left to tip it onto the corners that can carry it. Without
    // this a body can be caught at the instant it is standing on a single
    // corner, and stop in mid-air.
    const Vec3 middle = centroid(body.position.data(), count);
    const bool carried = touchingPlane && middle.x >= leftmost - 1e-6 && middle.x <= rightmost + 1e-6;
    // Resting on the plane and no longer going anywhere: there is no force left
    // to resolve, so the body is frozen and its ticks stop costing anything.
    body.quietTicks = carried && quiet ? body.quietTicks + 1 : 0;
    if (body.quietTicks >= kSleepTicks && !bodyHeld(index)) {
        body.frozen = true;
        for (std::size_t i = 0; i < count; ++i) {
            body.velocity[i] = {};
            body.previous[i] = body.position[i];
            body.shown[i] = body.position[i];
        }
    }
}

void PlaygroundRoom::freezeCheck() {
    if (!pointFrozen_ && dragTarget_ != DragTarget::Point && resting_ && length(velocity_) < 1e-9 &&
        (point_.y <= kSurfaceY || restingBody_ >= 0)) {
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
        // A point parked on a body is only held up by that body. Take it away --
        // drag it, or let it slide -- and the support is gone, so the point goes
        // back to being a falling body.
        if (pointFrozen_ && restingBody_ >= 0 && !supportedBy(std::size_t(restingBody_))) {
            pointFrozen_ = false;
            resting_ = false;
            restingBody_ = -1;
        }
        // A frozen body has nothing to do: no force, no motion, no work.
        if (!pointFrozen_) {
            tick();
        } else {
            for (std::size_t index = 0; index < kBodyCount; ++index) {
                if (!bodies_[index].frozen) tickBody(index);
            }
        }
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

    // Each body: a face, one edge marker per side, and a point at every corner
    // it is made of. The geometry is the body's rest shape and never changes;
    // where it is comes from the batch matrix, every frame, with no upload.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        const std::uint32_t base = kBodyFaceId + static_cast<std::uint32_t>(index) * kBodySpan;
        face(points, faces, body.local.data(), body.count, -0.002);
        raster.setObject(base, {0, 0}, 0);
        raster.mesh(points, faces, flat("#2c5f74"));

        points.clear();
        faces.clear();
        quad(points, faces, 0, -1, 1, 1, 0.005);
        for (std::size_t i = 0; i < body.count; ++i) {
            raster.setObject(base + kBodyEdgeOffset + static_cast<std::uint32_t>(i), {0, 0}, 0);
            raster.mesh(points, faces, flat("#9fd8e8"));
        }
        disc(points, faces, kCornerRadius);
        for (std::size_t i = 0; i < body.count; ++i) {
            raster.setObject(base + kBodyCornerOffset + static_cast<std::uint32_t>(i), {0, 0}, 0);
            raster.mesh(points, faces, flat("#ff8a4c"));
        }
    }

    scene_ = raster.takeGpuScene();
    scene_.revision = 1;
    pointBatch_ = findBatch(scene_, kPointId);
    traceBatch_ = findBatch(scene_, kTraceId);
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        ghostBatches_[i] = findBatch(scene_, kGhostId + static_cast<std::uint32_t>(i));
    }
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        const std::uint32_t base = kBodyFaceId + static_cast<std::uint32_t>(index) * kBodySpan;
        body.faceBatch = findBatch(scene_, base);
        for (std::size_t i = 0; i < body.count; ++i) {
            body.edgeBatches[i] = findBatch(scene_, base + kBodyEdgeOffset + static_cast<std::uint32_t>(i));
            body.cornerBatches[i] = findBatch(scene_, base + kBodyCornerOffset + static_cast<std::uint32_t>(i));
        }
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
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        // The face is the rest shape placed where the drawn corners already are,
        // so what is filled in is exactly the outline the edges draw.
        const Vec3 middle = centroid(body.shown.data(), body.count);
        scene_.batches[body.faceBatch].modelMatrix = pose(middle, edgeAngle(body.shown.data()));
        for (std::size_t i = 0; i < body.count; ++i) {
            scene_.batches[body.edgeBatches[i]].modelMatrix =
                segment(body.shown[i], body.shown[(i + 1) % body.count], kEdgeHalfWidth);
            scene_.batches[body.cornerBatches[i]].modelMatrix = translation(body.shown[i]);
        }
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
