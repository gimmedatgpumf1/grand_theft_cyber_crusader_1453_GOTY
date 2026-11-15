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
#include <cstdio>
#include <cstdlib>

namespace gtc::native {
using namespace playground_detail;
using namespace playground_tick;
namespace {

constexpr double kPointGrabRadius = 0.14;  // how close the mouse has to be to take it
constexpr double kGrabMargin = 0.06;       // and to take a body by its outline

} // namespace

// Which ball a click on the plane is on: the one whose disc it is inside, and the
// nearer one if it is inside both. A ball is a circle of its own radius about its
// middle, and a click inside one is a click on it.
std::size_t PlaygroundRoom::ballUnderClick(Vec3 at) const {
    std::size_t picked = kBallCount;
    double closest = 1e30;
    for (std::size_t index = 0; index < kBallCount; ++index) {
        const Ball& ball = balls_[index];
        const double gap = length(Vec3{at.x - ball.position.x, at.y - ball.position.y, 0}) - ball.radius;
        if (gap > 0.0 || gap >= closest) continue;
        closest = gap;
        picked = index;
    }
    return picked;
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
    //
    // The distance to the outline itself, and not the largest of the distances to the
    // lines its faces lie on. Those lines run on for ever: a click a metre and a half
    // off a plank's left end is on the *outside* of every face's line but one, and one
    // face's line can be behind the click however far away it is -- so the click was
    // taken, the plank was caught by its middle at the far end of the room, and the
    // mouse then carried a shape it was nowhere near. A body is grabbed where the click
    // is on it or within a hand's width of it, and nowhere else.
    const auto outlineDistance = [&](const Body& body) {
        if (body.count < 3) return 1e30;
        return ball_shape::outlineGap(hit, body.position.data(), body.position.data(), body.count, 1.0).gap;
    };
    std::size_t picked = kBodyCount;
    double pickedDistance = 1e30;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        // A pinned body is taken as readily as any other, but it is taken *by its
        // turn* and not by its place: its place is the room's, not the hand's, and a
        // seesaw carried off its axle is not a seesaw any more -- it is a plank lying
        // where the cursor left it, and the tick that finds it away from its pin puts
        // it back, which is the teleport. The pin is the one point of this room
        // nothing may move. What a hand can still do with an axle held by the room is
        // turn the thing on it, which is what a hand does on a seesaw: it leans on an
        // end and the plank tips. So a click on it takes it, and updateDrag() asks the
        // axle, not the floor, where the far end may go.
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
        if (body.pivoted) {
            // What the hand has hold of, written in the body's own frame so that it
            // stays the same piece of plank however far it is turned: the arm from
            // the axle to the grab, turned back by the body's own angle.
            const Vec3 arm = hit - body.pivotPoint;
            const double c = std::cos(-body.angle), s = std::sin(-body.angle);
            grabOffset_ = Vec3{arm.x * c - arm.y * s, arm.x * s + arm.y * c, 0};
        } else {
            grabOffset_ = Vec3{body.centre.x - hit.x, body.centre.y - hit.y, 0};
        }
        body.frozen = false;
        body.velocity = {};
        body.spin = 0.0;
    } else if (ballUnderClick(hit) < kBallCount) {
        // Either ball is taken the same way. A ball on a rope is a ball: the mouse takes
        // it by its disc and the rope holds it while it is carried, which is what a hand
        // on a hanging ball would find.
        dragBall_ = ballUnderClick(hit);
        dragTarget_ = DragTarget::Ball;
        Ball& ball = balls_[dragBall_];
        ball.frozen = false;
        ball.velocity = {};
        ball.spin = 0.0;
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
    if (dragTarget_ == DragTarget::Ball && dragBall_ < kBallCount) {
        Ball& ball = balls_[dragBall_];
        // Being carried is not being simulated, so the ball goes where the mouse
        // goes -- but not into a solid or through the floor. A spot the ball does
        // not fit in is not a spot it can be put down, and it stays where it was
        // until the mouse finds one it does.
        Vec3 wanted{std::clamp(hit.x, -5.0, 5.0), std::max(ball.radius, hit.y), 0};
        // A ball on a rope is held by the rope while it is carried, exactly as it is
        // while it swings: the hand may take it anywhere inside the sphere the rope
        // allows, and the rope stops it at the sphere. It is the same constraint as the
        // tick's -- the same length, the same anchor -- and not a second rule about
        // dragging: the hand asks for a place, and the place it may have is the place
        // the rope permits.
        if (rope_.attached && dragBall_ == rope_.ball) {
            const Vec3 arm = wanted - rope_.anchor;
            const double reach = length(arm);
            if (reach > rope_.length) wanted = rope_.anchor + arm * (rope_.length / reach);
        }
        if (!ballPlacementClear(ball, wanted)) return;
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
            if (segmentHitsPolygon(ball.position, wanted, body.position.data(), body.count, along, normal))
                return;
        }
        carryStep_ = wanted - ball.position;
        handSpeed_ = carryStep_ * (1.0 / kTick);
        ball.position = wanted;
        ball.previous = ball.shown = ball.position;
        // Carried is not simulated: the hand's speed is the hand's, held outside the
        // ball, and it is handed over on release by endDrag().
        ball.velocity = {};
        ball.spin = 0.0;
        return;
    }
    if (dragTarget_ == DragTarget::Body) {
        // The mouse carries the body's centre of mass; how far it is turned stays
        // what it was, because the mouse is not twisting it. The corners follow
        // from the two, as they always do.
        Body& body = bodies_[dragBody_ < kBodyCount ? dragBody_ : 0];
        if (body.pivoted) {
            // Held on its axle, the body is not carried anywhere: the hand's pull
            // arrives at the axle as a moment and the axle is what the room refuses to
            // move. So the hand asks for a *turn* -- the turn that brings the piece of
            // the body it took hold of round to where the mouse is -- and the body
            // takes as much of it as the sweep allows, exactly as a carried body takes
            // as much of its step as the sweep allows. A click on the axle itself has
            // no arm to pull on and turns nothing.
            const Vec3 arm = hit - body.pivotPoint;
            if (length(Vec3{arm.x, arm.y, 0}) < kQuantum) return;
            const double wanted = std::atan2(arm.y, arm.x) - std::atan2(grabOffset_.y, grabOffset_.x);
            const double turned = bodyStepAllowed(dragBody_ < kBodyCount ? dragBody_ : 0, body.pivotPoint,
                                                  body.angle, body.pivotPoint, wanted, false);
            handSpeed_ = {};
            body.angle += (wanted - body.angle) * turned;
            placeCorners(body);
            if (!(turned > 0.0)) return;
            body.previousCentre = body.centre;
            body.previousAngle = body.angle;
            body.previous = body.shown = body.position;
            body.velocity = {};
            body.spin = 0.0;
            return;
        }
        const Vec3 centre{std::clamp(hit.x + grabOffset_.x, -5.0, 5.0),
                          std::max(kSurfaceY, hit.y + grabOffset_.y), 0};
        // A body, like the ball, cannot be put down inside a solid: a shape let go
        // of inside another one is the one state the simulation has no honest answer
        // to, because nothing in this room is ever driven out of anything.
        std::array<Vec3, kMaxCorners> wanted{};
        const Vec3 move = centre - body.centre;
        for (std::size_t i = 0; i < body.count; ++i) wanted[i] = body.position[i] + move;
        if (!bodyPlacementClear(dragBody_ < kBodyCount ? dragBody_ : 0, wanted)) return;
        // And the place it is put in is not the whole question: the hand may not carry
        // it *through* something to get there. The ball's drag has asked that question
        // for a while -- a ball that jumps from one side of a cube to the other in one
        // frame has been through the cube in a tick nobody saw -- and a shape being
        // dragged had only its destination checked, so it crossed a plank as easily as
        // the cursor did. It is the same sweep the step takes.
        const double along =
            bodyStepAllowed(dragBody_ < kBodyCount ? dragBody_ : 0, body.centre, body.angle, centre, body.angle, false);
        // Carried as far as it may go and no further. Refusing the whole step was a
        // cursor that outran the shape it was holding: the hand asked for a place past
        // a plank, was told no, and the shape stayed where it was while the mouse went
        // on without it -- a shape that looks stuck, and then jumps when the cursor
        // comes back. What the hand may carry it through is a solve, not a yes or no:
        // it stops against the plank, where a hand carrying something against a plank
        // would stop, and comes away with it when the cursor does.
        const Vec3 carried = body.centre;
        body.centre = body.centre + (centre - body.centre) * along;
        if (!(along > 0.0)) return;
        // The hand is carrying it, and the hand has a speed: the shape is placed rather
        // than stepped, so it is not written into the shape's own velocity, but the
        // face of it is really travelling and everything that meets it has to know.
        handSpeed_ = (body.centre - carried) * (1.0 / kTick);
        placeCorners(body);
        // A shape the hand carries is placed, so the shapes' own walk never sees it
        // come: nothing is swept, nothing is solved, and a ball it is pressed on would
        // never be touched at all. Measured: a triangle carried into the hanging ball
        // at a metre a second stopped dead against it and the ball recorded 0.0000 m/s
        // for forty ticks -- a ball that does not bounce away because the room never
        // told it anything had arrived. So the hand's own step is answered here, with
        // the same coefficient every other surface in the room answers with.
        for (Ball& ball : balls_) {
            Vec3 way{};
            Vec3 at{};
            const std::size_t shape = dragBody_ < kBodyCount ? dragBody_ : 0;
            double deep = bodyBallGap(ball, shape, way, at);
            if (!(deep < 0.0)) continue;
            const Vec3 normal = way * -1.0;                    // out of the shape, into the ball
            const double pressing = dot(handSpeed_, normal);    // the face's speed into the ball
            if (!(pressing > 0.0)) continue;
            // Out of the shape, and away at the room's own restitution -- the speed the
            // ball would have been given by that face if it had stepped into it.
            ball.position = ball.position - way * (deep - ball_shape::kSkin);
            const double along = dot(ball.velocity, normal);
            ball.velocity = ball.velocity + normal * (ball_shape::kBallRestitution * pressing - along);
            ball.previous = ball.shown = ball.position;
            ball.frozen = false;
        }
        body.previousCentre = body.centre;
        body.previousAngle = body.angle;
        body.previous = body.shown = body.position;
        body.velocity = {};
        body.spin = 0.0;
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
// Letting go. A ball that was carried at a speed leaves at that speed; a ball that was
// put down somewhere leaves from rest. Without this the hand could lift and place but
// never throw, and a ball on a rope could not be swung at all -- it was carried to the
// top of its arc and dropped, and a swing that begins from rest is not a swing. The rope
// is asked no special question here: the speed it is given is the room's, and the rope's
// own arc does whatever it does with it, exactly as it does for a ball dropped in.
void PlaygroundRoom::endDrag() {
    if (dragTarget_ == DragTarget::Ball && dragBall_ < kBallCount) {
        Ball& ball = balls_[dragBall_];
        Vec3 thrown = carryStep_ * (1.0 / kTick);
        // A mouse can cross the room between two frames. The hand's speed is the
        // hand's speed, but a frame that lost its place is not a swing: keeping the
        // throw under what the rope can carry leaves a sweep the room can follow
        // instead of a ball shot out of a cannon by a stutter.
        const double speed = length(thrown);
        constexpr double kThrowCeiling = 6.0;
        if (speed > kThrowCeiling) thrown = thrown * (kThrowCeiling / speed);
        ball.velocity = thrown;
    }
    carryStep_ = {};
    handSpeed_ = {};
    dragTarget_ = DragTarget::None;
}


// A shape's face as a ball meeting it reads it: the shape's own speed and turn, unless
// the hand is carrying it -- and then the hand's.
Vec3 PlaygroundRoom::bodyFaceSpeed(std::size_t body, Vec3 at) const {
    if (body >= kBodyCount) return {};
    if (bodyHeld(body)) return handSpeed_;
    return pointSpeed(bodies_[body], at);
}
} // namespace gtc::native
