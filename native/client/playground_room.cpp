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
constexpr int kMaxContacts = 4;
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

} // namespace

Vec3 PlaygroundRoom::outwardNormalAt(const Body& body, Vec3 point) {
    Vec3 result{0, 1, 0};
    double nearest = 1e30;
    for (std::size_t edge = 0; edge < body.count; ++edge) {
        const Vec3 a = body.position[edge];
        const Vec3 b = body.position[(edge + 1) % body.count];
        const Vec3 side = b - a;
        const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
        if (!(sideLength > 1e-12)) continue;
        const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        const double gap = std::abs(dot(point - a, outward));
        if (gap < nearest) { nearest = gap; result = outward; }
    }
    return result;
}

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
        body.contacts = 0;
    }
    // A new ball, at the same place it starts in. The scene is not rebuilt by a
    // reset, so the two drawing handles it was given at build time have to be
    // kept: clearing them to zero puts the ball's transform on the ground batch
    // and leaves the ball itself where the last scene was built.
    const std::size_t ballFace = ball_.faceBatch, ballMark = ball_.markBatch;
    ball_ = Ball{};
    ball_.faceBatch = ballFace;
    ball_.markBatch = ballMark;
    ball_.position = ball_.previous = ball_.shown = Vec3{3.10, ball_.radius + 1.60, 0};
    ball_.angle = ball_.previousAngle = 0.0;
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
        for (std::size_t i = 0; i < body.count; ++i) body.velocity[i] = {};
    } else if (length(Vec3{hit.x - ball_.position.x, hit.y - ball_.position.y, 0}) <= ball_.radius) {
        dragTarget_ = DragTarget::Ball;
        ball_.frozen = false;
        ball_.velocity = {};
        ball_.spin = 0.0;
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
    if (dragTarget_ == DragTarget::Ball) {
        // Being carried is not being simulated, so the ball goes where the mouse
        // goes -- but not into a solid or through the floor. A spot the ball does
        // not fit in is not a spot it can be put down, and it stays where it was
        // until the mouse finds one it does.
        const Vec3 wanted{std::clamp(hit.x, -5.0, 5.0), std::max(ball_.radius, hit.y), 0};
        if (!ballPlacementClear(wanted)) return;
        ball_.position = wanted;
        ball_.previous = ball_.shown = ball_.position;
        ball_.velocity = {};
        ball_.spin = 0.0;
        return;
    }
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
// The point meets a body. The position has already been put at the contact by
// the walk; this is the momentum exchange there, and the friction of sliding
// along the face it met.
void PlaygroundRoom::hitBody(std::size_t index, Vec3 normal, double into) {
    Body& body = bodies_[index];
    ++body.contacts;
    // Which corner carries the blow: the nearest one. The body is its points, so
    // the momentum goes into those points and the fixed distances pass it on.
    std::size_t struck = 0;
    double nearest = 1e30;
    for (std::size_t i = 0; i < body.count; ++i) {
        const double distance = length(point_ - body.position[i]);
        if (distance < nearest) { nearest = distance; struck = i; }
    }
    // A body on the plane cannot be pushed down into it -- the plane only
    // answers the part of a blow that drives into it. The rest of that impulse
    // runs along the plane and the body must take it. A dot dropped straight
    // down onto the triangle's 45 degree face arrives with all its momentum
    // vertical and the face turns that into a push along its normal, so the
    // triangle is shoved sideways. Letting the floor answer the whole blow is
    // what used to swallow such a hit and leave the triangle untouched.
    bool supported = false;
    for (std::size_t i = 0; i < body.count; ++i) supported = supported || body.position[i].y <= kSurfaceY + 1e-9;
    const bool held = dragTarget_ == DragTarget::Body;
    const Vec3 answer = held ? Vec3{} : (supported ? Vec3{normal.x, 0, 0} : normal);
    const bool canMove = length(answer) > 1e-9;
    const Vec3 cornerVelocity = canMove ? body.velocity[struck] : Vec3{};
    const double closing = dot(velocity_ - cornerVelocity, normal);
    // The struck corner takes half of what the body receives and the other
    // corners share the rest, so the body gains exactly the momentum the point
    // loses and is set turning by the uneven share.
    constexpr double kStruckShare = 0.5;
    const double otherShare = (1.0 - kStruckShare) / static_cast<double>(body.count - 1);
    if (closing < 0.0) {
        // The contact takes exactly the approach speed and gives none of it back,
        // which is what the plane does to the point: nothing bounces in this room.
        const double cornerInverseMass = canMove ? kStruckShare / kMass : 0.0;
        const double impulse = -closing / (1.0 / kMass + cornerInverseMass);
        velocity_ = velocity_ + normal * (impulse / kMass);
        if (canMove) {
            for (std::size_t i = 0; i < body.count; ++i) {
                const double share = i == struck ? kStruckShare : otherShare;
                body.velocity[i] = body.velocity[i] - answer * (impulse * share / kMass);
            }
            body.frozen = false;
        }
    }
    (void)into;
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
    // A point that ended the last tick inside a body is put back on its
    // boundary. The sweep cannot do that itself -- it reports a crossing and
    // stops -- so a point already inside would be nudged out by a hair each tick
    // and creep along the face it is inside instead of sliding down it.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (!pushOutOfPolygon(point_, body.position.data(), body.count)) continue;
        restingBody_ = int(index);
        resting_ = true;
        supportNormal_ = outwardNormalAt(body, point_);
    }
    // Forces: gravity is m*g downwards, always. What is holding the point up
    // answers only the part of that force which drives into it; whatever is left
    // over still acts, and on a slope that is a real force down the slope. The
    // surface holds the point only if static friction can answer what is left --
    // mu_s * N -- and on a 45 degree face it cannot, which is why a point on a
    // slope slides instead of creeping to a stop.
    Vec3 force{0.0, -kMass * kGravity, 0};
    heldStill_ = false;
    if (dragTarget_ == DragTarget::Point) {
        force = Vec3{};
        heldStill_ = true;
    } else if (resting_) {
        const Vec3 normal = supportNormal_;
        const double into = dot(force, normal);       // negative when pressing in
        const Vec3 along = force - normal * into;     // what the surface cannot answer
        const double carried = -into;                 // N, how hard it is pressed
        // Static friction holds a surface only against something that is not
        // already moving. A point with sideways speed is sliding, and sliding is
        // kinetic friction's business however weakly it is being pushed.
        const Vec3 sliding = velocity_ - normal * dot(velocity_, normal);
        if (length(along) <= kPointStaticFriction * carried && length(sliding) < 1e-9) {
            force = Vec3{};
            heldStill_ = true;
        } else {
            force = along;                            // it slides
        }
    }
    velocity_ = velocity_ + force * (kTick / kMass);
    traceFrom_ = point_;
    resting_ = false;
    restingBody_ = -1;
    restingOnBall_ = false;
    slideSpan_ = 0.0;
    // The tick's travel, walked from contact to contact: find where the segment
    // meets something, move to it, answer it, and carry on with the time that is
    // left. However much time is left and however fast the point is going, it
    // cannot pass through anything, and it never loses speed it did not spend on
    // a contact.
    double remaining = 1.0;
    for (int step = 0; step < kMaxContacts && remaining > 1e-12; ++step) {
        const Vec3 next = point_ + velocity_ * (kTick * remaining);
        // Whichever surface is met first: the plane, or an edge of a body.
        double first = 2.0;
        std::size_t hitIndex = kBodyCount;
        Vec3 normal{0, 1, 0};
        bool wall = false;
        bool ballHit = false;
        if (next.y < kSurfaceY && std::abs(next.y - point_.y) > 1e-12) {
            first = std::clamp((kSurfaceY - point_.y) / (next.y - point_.y), 0.0, 1.0);
            wall = true;
        }
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            double along = 2.0;
            Vec3 facing{0, 1, 0};
            if (!segmentHitsPolygon(point_, next, body.position.data(), body.count, along, facing)) continue;
            // A point lying on a face and moving along it is touching, not
            // arriving. Answering that as a contact would stop the walk dead at
            // the very start, every tick, and the point would stutter downhill.
            if (along <= 1e-9 && dot(velocity_, facing) >= 0.0) continue;
            if (along < first) {
                first = along;
                hitIndex = index;
                normal = facing;
                wall = false;
            }
        }
        // The ball is one more thing in the way: a point with a radius, so the
        // point meets it when their surfaces touch.
        {
            double along = 2.0;
            Vec3 facing{0, 1, 0};
            if (segmentHitsCircle(point_, next, ball_.position, kPointRadius + ball_.radius, along, facing) &&
                !(along <= 1e-9 && dot(velocity_, facing) >= 0.0) && along < first) {
                first = along;
                normal = facing;
                ballHit = true;
                wall = false;
            }
        }
        if (!(first <= 1.0)) {
            point_ = next;
            break;
        }
        point_ = point_ + (next - point_) * first;
        remaining *= 1.0 - first;
        if (ballHit) {
            // Same rule as a body: what drives into the ball is answered by both
            // sides, and what runs along it is left alone.
            pointAgainstBall(normal);
            resting_ = true;
            restingBody_ = -1;
            restingOnBall_ = true;
            supportNormal_ = normal;
            slideSpan_ += kTick * remaining;
            if (!(length(velocity_) > 1e-12)) break;
            continue;
        }
        if (wall) {
            // The plane answers only what drives into it. What is running along
            // it is the point's kinetic energy and friction is what takes that.
            if (velocity_.y < 0.0) velocity_.y = 0.0;
            resting_ = true;
            restingBody_ = -1;
            supportNormal_ = {0, 1, 0};
            slideSpan_ += kTick * remaining;
            continue;
        }
        // The same rule for a body: only the part of the speed that drives into
        // the face is answered here.
        const Vec3 along = velocity_ - normal * dot(velocity_, normal);
        const double into = dot(velocity_, normal);
        if (into < 0.0) {
            hitBody(hitIndex, normal, into);
        }
        velocity_ = along;
        resting_ = true;
        restingBody_ = int(hitIndex);
        supportNormal_ = normal;
        slideSpan_ += kTick * remaining;
        if (!(length(velocity_) > 1e-12)) break;
    }
    // Still touching something when the tick ends means it was touching all
    // along: the walk only reports an arrival, and a point that is already lying
    // on the floor never arrives at it. Without this it kept its sideways speed
    // forever, because nothing ever told friction there was a surface.
    if (slideSpan_ <= 0.0) {
        if (point_.y <= kSurfaceY + 1e-9) {
            resting_ = true;
            restingBody_ = -1;
            supportNormal_ = {0, 1, 0};
            slideSpan_ = kTick;
        } else {
            for (std::size_t index = 0; index < kBodyCount; ++index) {
                const Body& body = bodies_[index];
                Vec3 normal{0, 1, 0};
                double nearest = 1e30;
                for (std::size_t edge = 0; edge < body.count; ++edge) {
                    const Vec3 a = body.position[edge];
                    const Vec3 b = body.position[(edge + 1) % body.count];
                    const Vec3 side = b - a;
                    const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
                    if (!(sideLength > 1e-12)) continue;
                    const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
                    const double gap = std::abs(dot(point_ - a, outward));
                    if (gap < nearest) { nearest = gap; normal = outward; }
                }
                // Only a face that can carry weight counts as standing on it.
                if (nearest > 1e-6 || normal.y < 0.5) continue;
                resting_ = true;
                restingBody_ = int(index);
                supportNormal_ = normal;
                slideSpan_ = kTick;
                break;
            }
            // The ball is a surface too, and for the same reason: a dot lying on
            // it never arrives at it, so nothing else would tell it there is
            // something underneath. Its own shape is a circle, so the test is the
            // distance between the two middles.
            if (!resting_) {
                const Vec3 gap = point_ - ball_.position;
                const double distance = length(gap);
                const double touching = kPointRadius + ball_.radius;
                if (distance <= touching + 1e-9 && distance > 0.0 && gap.y > 0.0) {
                    resting_ = true;
                    restingBody_ = -1;
                    restingOnBall_ = true;
                    supportNormal_ = gap / distance;
                    slideSpan_ = kTick;
                }
            }
        }
    }
    // Nothing may be left inside a body: a contact is found just before it, and
    // the walk stops there, but rounding can leave a point a hair deep.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        pushOutOfPolygon(point_, bodies_[index].position.data(), bodies_[index].count);
    }
    // Kinetic friction over the part of the tick the point spent on something.
    // The line that is drawn is the travel that was actually walked this tick.
    traceTo_ = point_;
    // Kinetic friction, once, for the part of the tick the point was actually
    // touching something, and only across the surface -- never into it, which the
    // contact has already answered. It slows sliding; it does not decide whether
    // there is sliding, because that is static friction's job above, done with
    // the forces. Doing both jobs with a leftover velocity is what used to catch
    // a point on a slope and hold it there, creeping.
    if (slideSpan_ > 0.0 && !heldStill_ && dragTarget_ != DragTarget::Point) {
        const Vec3 normal = supportNormal_;
        const Vec3 across = velocity_ - normal * dot(velocity_, normal);
        const double speed = length(across);
        const double pressed = kGravity * std::abs(normal.y);
        const double slide = kPointKineticFriction * pressed * slideSpan_;
        if (speed > 1e-12) {
            const double keep = speed <= slide ? 0.0 : (speed - slide) / speed;
            const Vec3 lost = across * (1.0 - keep);
            velocity_ = velocity_ - lost;
            // Friction has two ends: the surface rubs the point, so the point
            // rubs the surface, and the momentum does not vanish. It goes in
            // horizontally, the only way a body on the plane can go.
            if (restingBody_ >= 0 && dragTarget_ != DragTarget::Body && std::abs(lost.x) > 1e-12) {
                Body& body = bodies_[std::size_t(restingBody_)];
                constexpr double kStruckShare = 0.5;
                const double otherShare = (1.0 - kStruckShare) / static_cast<double>(body.count - 1);
                for (std::size_t i = 0; i < body.count; ++i) {
                    const double share = i == 0 ? kStruckShare : otherShare;
                    body.velocity[i] = body.velocity[i] - Vec3{lost.x, 0, 0} * share;
                }
                body.frozen = false;
            }
        }
    }
    slideSpan_ = 0.0;
    if (dragTarget_ != DragTarget::Point && resting_ && !(length(velocity_) > kRestSpeed) && heldStill_) {
        // No speed and no net force: this point cannot do anything next tick.
        // Freezing also ends the interpolation, so what is drawn is the resting
        // position and not some fraction of the way down the last tick.
        pointFrozen_ = true;
        velocity_ = {};
        previous_ = point_;
        shown_ = point_;
    }
    // A frozen body is not stepped at all. It has no velocity and no force to
    // resolve, and stepping it anyway let gravity and the floor clamp nibble at
    // it tick after tick, which drifted a body that was supposed to be asleep.
    // The bodies are stepped first, so that when the ball is swept nothing has to
    // be predicted: each body's own travel this tick is known exactly, and the
    // ball's is tested against it. That is what finds a body dropped onto a ball
    // lying still -- a ball that is not going anywhere has no travel of its own to
    // find anything with.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (body.frozen) {
            // A body that is not stepped still has to own its last position, or
            // the first tick it is woken the travel it is measured over runs back
            // to wherever it froze, and the ball answers that instead of the
            // motion that just happened.
            body.previous = body.position;
            for (std::size_t i = 0; i < body.count; ++i) body.velocity[i] = {};
        } else {
            tickBody(index);
        }
    }
    if (!ball_.frozen || ballNeighboursMoving()) tickBall();
    else { ball_.previous = ball_.position; ball_.previousAngle = ball_.angle; }
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
        // The same hitscan the point uses, once per corner: the first thing the
        // corner's step reaches is where that corner stops, however far the tick
        // would have carried it. The floor is one of those things; the ball is the
        // other. A corner is a point, so it reaches the ball when it is one ball
        // radius from the ball's middle.
        double along = 2.0;
        bool onFloor = false;
        if (to.y < kSurfaceY) {
            along = std::clamp((kSurfaceY - from.y) / (to.y - from.y), 0.0, 1.0);
            onFloor = true;
        }
        {
            double ballAlong = 2.0;
            Vec3 facing{0, 1, 0};
            if (segmentHitsCircle(from, to, ball_.position, ball_.radius, ballAlong, facing)) {
                // Starting within reach and going further in is not a journey this
                // tick: the corner is already at the ball and cannot advance. That
                // is what stops a body standing on a ball from sinking into it tick
                // after tick, without any pass that puts it back afterwards.
                if (ballAlong <= 1e-9 && dot(to - from, facing) <= 0.0) ballAlong = 0.0;
                if (ballAlong < along) {
                    along = ballAlong;
                    onFloor = false;
                }
            }
        }
        if (along <= 1.0) {
            body.position[i] = from + (to - from) * along;
            if (onFloor) body.position[i].y = kSurfaceY;
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
    // The ball is the other thing a corner cannot be inside. Lifting a corner out of
    // it is a correction in exactly the way the floor's lift is: the body did not
    // travel, the solve put it back, and that has to be taken out of the velocity
    // again or a cube landing on a ball kicks itself off it.
    std::array<Vec3, kMaxCorners> ballLift{};
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
        // And out of the ball, the same way. Two things can be through it: a corner,
        // which is a point inside the ball's reach, and a face, which is the whole
        // side of a body crossing the ball's surface while both its corners are
        // outside it -- a cube tipping over onto a ball does exactly that, and
        // clamping corners alone left the face free to cut through.
        double deepestBall = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            const Vec3 gap = body.position[i] - ball_.position;
            const double distance = length(gap);
            if (!(distance > 1e-12) || distance >= ball_.radius) continue;
            const Vec3 out = gap / distance;
            const double amount = ball_.radius - distance;
            deepestBall = std::max(deepestBall, amount);
            ballLift[i] += out * amount;
            body.position[i] = ball_.position + out * ball_.radius;
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
        if (worstDistance <= kDistanceTolerance && deepest <= kDistanceTolerance &&
            deepestBall <= kDistanceTolerance)
            break;
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
        // Take the corrections back out: what is left is what the corner was
        // actually doing. The floor's is upwards, the ball's is whatever way it had
        // to go to get back on the ball's surface.
        velocity.y -= lift[i] / kTick;
        velocity = velocity - ballLift[i] * (1.0 / kTick);
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
        quiet = quiet && length(body.position[i] - body.previous[i]) < kRestSpeed * kTick;
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
    if (quiet && !bodyHeld(index)) {
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
        (point_.y <= kSurfaceY || restingBody_ >= 0 || restingOnBall_)) {
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
        // What the ball arrived with, taken before anything in the room moved this
        // tick. A bounce is the answer to arriving at something; a push it gets
        // part-way through the tick is not an arrival, and must not be bounced back.
        ball_.arrival = ball_.velocity;
        // A point parked on a body is only held up by that body. Take it away --
        // drag it, or let it slide -- and the support is gone, so the point goes
        // back to being a falling body.
        if (pointFrozen_ && restingBody_ >= 0 && !supportedBy(std::size_t(restingBody_))) {
            pointFrozen_ = false;
            resting_ = false;
            restingBody_ = -1;
        }
        // The same for a ball: take it away and the dot has nothing under it.
        if (pointFrozen_ && restingOnBall_) {
            const double distance = length(point_ - ball_.position);
            if (distance > kPointRadius + ball_.radius + 1e-9) {
                pointFrozen_ = false;
                resting_ = false;
                restingOnBall_ = false;
            }
        }
        // A frozen body has nothing to do: no force, no motion, no work.
        if (!pointFrozen_) {
            tick();
        } else {
            for (std::size_t index = 0; index < kBodyCount; ++index) {
                Body& body = bodies_[index];
                if (body.frozen) {
                    body.previous = body.position;
                    for (std::size_t i = 0; i < body.count; ++i) body.velocity[i] = {};
                } else {
                    tickBody(index);
                }
            }
            if (!ball_.frozen || ballNeighboursMoving()) tickBall();
            else { ball_.previous = ball_.position; ball_.previousAngle = ball_.angle; }
        }
        freezeCheck();
        ++ticks;
    }
}

} // namespace gtc::native
