// Where the mouse's ray goes and what it takes hold of.
//
// This is the room's hand, not its physics: a drag decides *where* a body or the
// dot is being asked to be, and nothing else. Everything that follows from being
// moved -- what it collides with, what the floor makes of it, how much energy it
// ends up with -- is the solver's, exactly as it is for a body that nothing is
// holding.

#include "client/playground_room.h"
#include "client/playground_shapes.h"
#include "client/playground_ball_shape.h"
#include "client/playground_room_tick.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {
using namespace playground_detail;
using namespace playground_tick;
namespace {

constexpr double kPointGrabRadius = 0.14;  // how close the mouse has to be to take it
constexpr double kGrabMargin = 0.06;       // and to take a body by its outline

} // namespace

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
        grabOffset_ = Vec3{body.centre.x - hit.x, body.centre.y - hit.y, 0};
        body.frozen = false;
        body.velocity = {};
        body.spin = 0.0;
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
        // A spot the ball fits in is not enough: the hand may not carry it *through*
        // a shape to get there. Refusing only the place a shape occupies left the ball
        // jumping from one side of a cube to the other in a single frame, because every
        // spot along the way was fine and the one between them was not -- the ball
        // passed through the solid, invisibly, in a tick nobody saw.
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            if (body.count == 0) continue;
            double along = 2.0;
            Vec3 normal{0, 1, 0};
            if (segmentHitsPolygon(ball_.position, wanted, body.position.data(), body.count, along, normal))
                return;
        }
        ball_.position = wanted;
        ball_.previous = ball_.shown = ball_.position;
        ball_.velocity = {};
        ball_.spin = 0.0;
        return;
    }
    if (dragTarget_ == DragTarget::Body) {
        // The mouse carries the body's centre of mass; how far it is turned stays
        // what it was, because the mouse is not twisting it. The corners follow
        // from the two, as they always do.
        Body& body = bodies_[dragBody_ < kBodyCount ? dragBody_ : 0];
        const Vec3 centre{std::clamp(hit.x + grabOffset_.x, -5.0, 5.0),
                          std::max(kSurfaceY + kPointRadius, hit.y + grabOffset_.y), 0};
        // A body, like the ball, cannot be put down inside a solid: a shape let go
        // of inside another one is the one state the simulation has no honest answer
        // to, because nothing in this room is ever driven out of anything.
        std::array<Vec3, kMaxCorners> wanted{};
        const Vec3 move = centre - body.centre;
        for (std::size_t i = 0; i < body.count; ++i) wanted[i] = body.position[i] + move;
        if (!bodyPlacementClear(dragBody_ < kBodyCount ? dragBody_ : 0, wanted)) return;
        body.centre = centre;
        placeCorners(body);
        body.previousCentre = body.centre;
        body.previousAngle = body.angle;
        body.previous = body.shown = body.position;
        body.velocity = {};
        body.spin = 0.0;
        return;
    }
    point_ = {std::clamp(hit.x, -5.0, 5.0), std::max(kSurfaceY + kPointRadius, hit.y), 0};
    // A dragged point is not a simulated one: it belongs to the mouse and has
    // no previous tick to glide away from.
    previous_ = point_;
    velocity_ = {};
    resting_ = point_.y <= kSurfaceY + kPointRadius;
    restingBody_ = -1;
    haveTrace_ = false;
}
} // namespace gtc::native
