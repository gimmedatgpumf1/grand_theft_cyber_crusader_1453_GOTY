#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"
#include "client/playground_ball_shape.h"
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gtc::native {
using namespace playground_detail;
using namespace playground_tick;
namespace {

constexpr double kPointGrabRadius = 0.14;  // how close the mouse has to be to take it
constexpr double kGrabMargin = 0.06;       // and to take a body by its outline
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
// How far a sleeping dot may drift from the surface it is lying on and still be
// lying on it. A micrometre: the same "these two are the same place" the walk uses.
constexpr double kPointSupportBand = 1e-6;

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
    if (body >= kBodyCount || index >= bodies_[body].count) return Vec3{};
    return pointSpeed(bodies_[body], bodies_[body].position[index]);
}

Vec3 PlaygroundRoom::bodyMiddle(std::size_t body) const {
    if (body >= kBodyCount) return Vec3{};
    return bodies_[body].centre;
}

// Back to the room the demonstration starts in, and back to *only* that.
//
// A reset is not just where things are put -- it is everything the room was
// carrying. The forces written into the ledger belong to the tick they were solved
// in, so a reset that leaves them behind leaves last session's arrows hanging over
// the new room until a tick overwrites them; the recorded impulses belong to a body
// that was asleep, and the body is not asleep any more; the drag belongs to a mouse
// that is not holding anything; the ball's contact list belongs to a tick that no
// longer happened. All of it is cleared here, in one place, so that "the room after
// R" and "the room on the first frame" are the same room and not merely the same
// positions.
bool PlaygroundRoom::reset(Vec3 start, Vec3 startVelocity) {
    point_ = previous_ = shown_ = start;
    velocity_ = startVelocity;
    pointForce_ = {};
    traceFrom_ = traceTo_ = point_;
    ghostCount_ = ghostNext_ = 0;
    accumulator_ = 0;
    dragTarget_ = DragTarget::None;
    dragBody_ = kBodyCount;
    planeGrab_ = grabOffset_ = {};
    slideSpan_ = 0.0;
    heldStill_ = false;
    supportNormal_ = Vec3{0, 1, 0};
    ballTouchCount_ = 0;
    clearForces();
    impulseCount_ = 0;
    restingBody_ = -1;
    restingOnBall_ = false;
    resting_ = false;
    haveTrace_ = false;
    pointFrozen_ = false;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        const Shape& shape = kShapes[index];
        // The scene is not rebuilt by a reset, so the handles this body's face, edges
        // and corners were given when the scene was built have to be kept, for the
        // same reason the ball's two are kept below. Zeroing them was not a harmless
        // clearing: every body then wrote its transform onto batch zero -- the
        // ground -- and its own batches stayed frozen at the last frame drawn before
        // the reset, so the room one frame after R showed the shapes where they had
        // been abandoned while the simulation underneath them was already back at the
        // start. The picture and the physics disagreed, and everything that reads the
        // picture -- the mouse, the arrows, the eye -- was wrong with it.
        const std::size_t face = body.faceBatch;
        const auto edges = body.edgeBatches, corners = body.cornerBatches;
        body = Body{};
        body.faceBatch = face;
        body.edgeBatches = edges;
        body.cornerBatches = corners;
        body.count = shape.count;
        body.local = shape.local;
        // The shape's corners are given about the centre of area, which is where a
        // uniform plate balances: the body's centre of mass.
        body.centre = body.previousCentre = shape.middle;
        body.angle = body.previousAngle = shape.angle;
        body.mass = static_cast<double>(body.count) * kMass;
        body.moment = polygonMoment(body.local, body.count, body.mass);
        body.velocity = {};
        body.spin = 0.0;
        body.staticFriction = shape.staticFriction;
        body.kineticFriction = shape.kineticFriction;
        body.frozen = false;
        body.contacts = 0;
        placeCorners(body);
        body.previous = body.shown = body.position;
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


// One impulse, given to one body at one place. This is the only door into a
// body's motion anywhere in the room, because it is the only thing a force can do
// to a rigid body: change the speed of its centre of mass by J/m and its turn by
// the moment of J about the centre, r x J / I. The corners are not touched here --
// they are where the shape says they are, and they move because the body does.
void PlaygroundRoom::pushBody(Body& body, Vec3 at, Vec3 impulse, ForceKind kind) {
    if (!(length(impulse) > 0.0)) return;
    const Vec3 arm = at - body.centre;
    body.velocity = body.velocity + impulse * (1.0 / body.mass);
    body.spin += (arm.x * impulse.y - arm.y * impulse.x) / body.moment;
    // A body that was asleep has just been woken by this, and it starts its
    // settling again from where the blow found it: it is not at rest any more, it
    // is doing something, and it has to be measured doing it before it is called
    // still. Without this the tick that wakes a body would put it back to sleep on
    // its way out and swallow the blow.
    if (body.frozen) {
        body.restTicks = 0;
        body.restLow = body.restHigh = body.centre;
        body.restAngleLow = body.restAngleHigh = body.angle;
    }
    body.frozen = false;
    // Kept for the day this body falls asleep: a body at rest has no tick to be
    // solved in, and what is holding it up is what was solved for it here. The
    // solver gives a contact its answer in pieces, pass by pass, so what is kept is
    // the total at each place and of each kind -- one arrow per place, which is what
    // the eye reads and what the body actually feels.
    {
        std::size_t slot = body.appliedCount;
        for (std::size_t i = 0; i < body.appliedCount; ++i) {
            if (body.applied[i].kind != kind) continue;
            if (length(body.applied[i].at - at) > 1e-9) continue;
            slot = i;
            break;
        }
        if (slot < body.appliedCount) {
            body.applied[slot].impulse = body.applied[slot].impulse + impulse;
        } else if (body.appliedCount < Body::kMaxRestForces) {
            body.applied[body.appliedCount++] = Body::Applied{at, impulse, kind};
        }
    }
    recordImpulse(at, impulse, kind, std::size_t(&body - bodies_.data()));
}

// And the same for the ball: a blow at a place changes its speed along the line of
// the blow, and its turn by the moment of it, with the moment of a solid sphere.
void PlaygroundRoom::pushBall(Vec3 at, Vec3 impulse, ForceKind kind) {
    if (!(length(impulse) > 0.0)) return;
    const Vec3 arm = at - ball_.position;
    const double moment = 0.4 * ball_.mass * ball_.radius * ball_.radius;
    ball_.velocity = ball_.velocity + impulse * (1.0 / ball_.mass);
    ball_.spin += (arm.x * impulse.y - arm.y * impulse.x) / moment;
    ball_.frozen = false;
    // The ball is the room's other kind of solid, and it is its own entry: an impulse
    // on it is a force on the ball, not on whatever it is touching.
    recordImpulse(at, impulse, kind, kBallOwner);
}

// The ledger, written where the impulse is applied. A contact solved in many passes
// is one force at one place, so an impulse already at that place and of that kind
// is added to rather than grown as a second arrow.
void PlaygroundRoom::recordImpulse(Vec3 at, Vec3 impulse, ForceKind kind, std::size_t on) {
    for (std::size_t i = 0; i < impulseCount_; ++i) {
        RecordedImpulse& entry = impulses_[i];
        if (entry.kind != kind || entry.on != on) continue;
        if (length(entry.at - at) > 1e-6) continue;
        entry.impulse = entry.impulse + impulse;
        return;
    }
    if (impulseCount_ >= impulses_.size()) return;
    impulses_[impulseCount_++] = RecordedImpulse{at, impulse, kind, on};
}

// An impulse over a tick is a force, and this is where the tick's contacts become
// the arrows on screen: F = J/dt at the place J was given.
void PlaygroundRoom::writeImpulses() {
    for (std::size_t i = 0; i < impulseCount_; ++i) {
        const RecordedImpulse& entry = impulses_[i];
        if (!(length(entry.impulse) > 1e-9)) continue;
        addForce(entry.at, entry.impulse * (1.0 / kTick), entry.kind, nullptr, entry.on);
    }
    clearImpulses();
}

// A point of the room meets a body. The same law as every other contact: the
// closing speed along the normal is what the two of them answer, the body's share
// of the answer is its inverse mass plus its lever about the centre, and the
// impulse is given at the place the dot touched -- so a dot landing on the far end
// of a shape turns it, which is what it always should have done.
double PlaygroundRoom::answerPointOnBody(std::size_t index, Vec3 pointPosition, Vec3 pointVelocity,
                                         Vec3 normal) {
    Body& body = bodies_[index];
    if (bodyHeld(index)) return 0.0;
    const double closing = dot(pointVelocity - pointSpeed(body, pointPosition), normal);
    if (!(closing < 0.0)) return 0.0;
    const double give = 1.0 / kMass + giveAt(body, pointPosition, normal);
    if (!(give > 1e-12)) return 0.0;
    // The contact stops the approach and gives none of it back: nothing bounces in
    // this room.
    const double impulse = -closing / give;
    pushBody(body, pointPosition, normal * -impulse, ForceKind::Contact);
    return impulse;
}

// The point meets a body. The position has already been put at the contact by the
// walk; this is the momentum exchange there.
void PlaygroundRoom::hitBody(std::size_t index, Vec3 normal, double into) {
    velocity_ = velocity_ + normal * (answerPointOnBody(index, point_, velocity_, normal) / kMass);
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
    // The forces on the point, in the one accumulator: its weight first, m*g
    // newtons down like any other force, and then whatever is holding it up. There
    // is no separate rule for gravity here: what the surface cannot answer is left
    // in the sum and the integrator below turns the whole of it into speed.
    const Vec3 weight{0.0, -kMass * kGravity, 0.0};
    pointForce_ = {};
    addForce(point_, weight, ForceKind::Gravity, &pointForce_, kDotOwner);
    // And the air. The dot is a small sphere and it pushes through the same air the
    // ball does: F = half rho Cd A v^2, against its own motion. At the speeds it
    // reaches here it is a fraction of a newton, which is exactly what it should be
    // -- but it is a force in the ledger, drawn like the rest, and a dot falling in
    // a vacuum is a dot that is not in this room.
    {
        const double swift = length(velocity_);
        if (swift > 0.0) {
            const double area = ball_shape::kPi * kPointRadius * kPointRadius;
            const double drag = 0.5 * ball_shape::kAirDensity * ball_shape::kBallDragCoefficient * area * swift * swift;
            addForce(point_, velocity_ * (-drag / swift), ForceKind::Drag, &pointForce_, kDotOwner);
        }
    }
    heldStill_ = false;
    if (dragTarget_ == DragTarget::Point) {
        // The mouse holds it: the hold is the force that carries its weight.
        addForce(point_, weight * -1.0, ForceKind::Contact, &pointForce_, kDotOwner);
        heldStill_ = true;
    } else if (resting_) {
        // The surface answers in two parts, because it does: a normal force straight
        // out of it, and friction along it. They are written into the ledger apart,
        // so the arrow that holds a dot still on a slope can be seen to be friction
        // and the arrow that presses it into the slope can be seen to be the normal.
        const Vec3 normal = supportNormal_;
        const double into = dot(weight, normal);      // negative when pressing in
        const Vec3 along = weight - normal * into;    // what the surface cannot answer
        const double carried = std::max(0.0, -into);  // N, how hard it is pressed
        addForce(point_, normal * carried, ForceKind::Contact, &pointForce_, kDotOwner);
        // Static friction holds while the point is not moving along the surface and
        // the surface can answer what is pushing it sideways; past that it slips, and
        // then kinetic friction acts, against the slip, at mu_k times the load. That
        // is what makes a dot on a steep face accelerate down it instead of standing
        // on it, and it is a force like any other, not a rule about when to stop.
        const Vec3 slip = velocity_ - normal * dot(velocity_, normal);
        const double swift = length(slip);
        Vec3 friction{};
        if (swift > kRestSpeed) {
            // Coulomb, in the one form that does not hum. The surface rubs against
            // the slide with mu_k * N -- and it can never do more than take the
            // slide away, because friction stops a thing, it does not send it back.
            // Asking for the whole of mu_k * N when a centimetre a second is left
            // reverses the point, the next tick reverses it again, and a dot that
            // has come to rest sits there buzzing for ever. What friction gives is
            // whatever is needed to leave no slip, up to what the load can carry;
            // and if the load can carry it, the point holds and does not slide at
            // all, which is static friction, which is the same law.
            const double toStop = kMass * swift / kTick;  // N, what would stop it dead
            const double grip = std::min(toStop, (toStop <= kPointStaticFriction * carried)
                                                     ? toStop
                                                     : kPointKineticFriction * carried);
            friction = slip * (-grip / swift);
            if (toStop <= kPointStaticFriction * carried) heldStill_ = true;
        } else if (length(along) <= kPointStaticFriction * carried) {
            friction = along * -1.0;
            heldStill_ = true;
        } else if (length(along) > 0.0) {
            // Not moving yet, and not held: it is about to slide the way it is being
            // pushed, and until it does, the surface resists with all it has.
            friction = along * (-kPointStaticFriction * carried / length(along));
        }
        if (length(friction) > 0.0)
            addForce(point_, friction, ForceKind::Friction, &pointForce_, kDotOwner);
    }
    velocity_ = accelerated(velocity_, pointForce_, kMass);
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
        // The floor is a plane through kSurfaceY, and the dot's middle can come no
        // closer to it than the dot's own radius: the thing that touches the floor
        // is the underside of a ball, not the middle of one.
        const double floorY = kSurfaceY + kPointRadius;
        if (next.y < floorY && std::abs(next.y - point_.y) > 1e-12) {
            first = std::clamp((floorY - point_.y) / (next.y - point_.y), 0.0, 1.0);
            wall = true;
        }
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            // The dot is a ball of its own radius, not a bare point: the picture
            // draws it that size, the ball meets it at that size, and the bodies
            // must meet it at that size too. Swept as a circle, its middle stops a
            // radius off the face -- where the surface of it is -- instead of at
            // the face, which is what put half of it inside the floor and let it
            // slip exactly through the corner where a shape meets the ground.
            ball_shape::OutlineHit touch;
            if (!ball_shape::circleHitsBody(point_, next - point_, body.position.data(), body.count, kPointRadius,
                                            touch))
                continue;
            const double along = touch.along;
            const Vec3 facing = touch.normal;
            // Touching is not arriving, and neither is a course that would put the
            // dot a hair into the face by the end of the tick -- a face is never
            // exactly where the arithmetic thinks it is, and a settled shape is
            // square to a part in ten million. A dot running along that face at
            // five metres a second then closes at a fraction of a micrometre in
            // the whole tick, thinner than a surface. Answered as an arrival that
            // costs no time at all, the walk goes round, meets the same face in the
            // same place, and spends every step it has on it: the tick ends where
            // it began with all its speed still in it. That is the dot that stops
            // dead at the foot of the slope with four metres a second unused.
            // Below the thickness of a surface it is not arriving, it is lying on
            // it, and lying on it is the position pass's business.
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
        const double into = dot(velocity_, normal);
        if (into < 0.0) {
            // Where the two surfaces met: the body's lever is measured from the
            // place on it the dot is touching, which is a radius out from the dot's
            // middle, not the middle itself.
            hitBody(hitIndex, normal, into);
        }
        // And that exchange is the whole of the answer -- but it is an answer
        // about the two surfaces, not about the room.
        //
        // The floor cannot yield: all a dot driving into it can do is stop, and
        // flattening it onto the plane afterwards says exactly that. A body
        // yields. It takes part of the approach as motion of its own, and what the
        // dot keeps is the rest -- the two surfaces part company at nought. So
        // what is removed here is what still drives the dot into the face *relative
        // to the face moving away under it*, and nothing else. Flattening the dot
        // onto the face as if the face were a wall throws the rest away, and with
        // it momentum: the dot arrived at three and a half metres a second, the cube
        // left with half a metre a second, and everything between vanished into
        // nothing -- not into the cube, not into friction, not into heat.
        const Vec3 face = pointSpeed(bodies_[hitIndex], point_);
        const Vec3 relative = velocity_ - face;
        if (dot(relative, normal) < 0.0) velocity_ = face + (relative - normal * dot(relative, normal));
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
        if (point_.y <= kSurfaceY + kPointRadius + 1e-9) {
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
                // Only a face that can carry weight counts as standing on it, and
                // the middle of the dot stands a radius clear of it.
                if (std::abs(nearest - kPointRadius) > 1e-6 || normal.y < 0.5) continue;
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
    // Friction is not taken out of the speed here, and it never was meant to be.
    // It has already acted as what it is: a force along the surface, written into
    // the ledger at the top of this tick and turned into speed by the same
    // integrator that turns the weight into speed. What used to happen after the
    // walk -- the slide taken off the velocity by hand, against a normal force
    // written down as m*g*cos, whether or not that was the load the surface was
    // actually carrying, and whether or not the force above had already taken it --
    // charged the dot friction twice, the second time at a pressure nothing had
    // measured. There is one friction in this room and it is a force.
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
    // Every body is stepped together, because a step one of them takes is a step
    // the others have to be answered for at the same instant: the walk looks for
    // the first meeting of anything with anything, and that meeting is answered on
    // both sides at once.
    tickBodies();
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
// Whether the dot's own support has gone away underneath it.
//
// A dot goes to sleep on a floor, a shape or the ball, and while it sleeps nothing
// moves it. Take the thing it is lying on out from under it -- drag the shape, roll
// the ball away -- and it would hang in the air for ever: asleep, and wrong. So the
// surface it was resting on is asked, every tick, whether it is still there, and if
// it is not the dot wakes and falls, which is what it was about to do anyway.
bool PlaygroundRoom::pointSupportGone() const {
    if (restingOnBall_) {
        const Vec3 gap{point_.x - ball_.position.x, point_.y - ball_.position.y, 0.0};
        return length(gap) > ball_.radius + kPointSupportBand;
    }
    if (restingBody_ >= 0 && restingBody_ < int(kBodyCount)) {
        const Body& body = bodies_[std::size_t(restingBody_)];
        if (body.count == 0) return true;
        if (ball_shape::insideOutline(point_, body.position.data(), body.count)) return false;
        const ball_shape::OutlineGap gap =
            ball_shape::outlineGap(point_, body.position.data(), body.position.data(), body.count, kPointRadius);
        return gap.gap > kPointSupportBand;
    }
    return point_.y > kSurfaceY + kPointSupportBand;
}

// Whether any body's step this tick has arrived at the point.
//
// The dot is a point with a radius, and a body arriving at it is a face or a corner
// arriving at a circle -- the same question the ball's own hitscan answers, so it is
// asked here with the same geometry. What it is for is the one thing a sleeping point
// cannot do for itself: a point that is not stepped has no sweep, and a body that
// falls on it while it sleeps has nothing to stop it. The point wakes, and from then
// on its own tick -- which does have a sweep -- answers whatever met it.
bool PlaygroundRoom::pointReachedByBodies() const {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0) continue;
        // It has already overtaken the point: wherever the point is, a body's outline
        // around it is a meeting that has happened and has to be answered. A body that
        // has come to rest is not skipped here: it is asleep, but the point inside it
        // is still inside it, and a sleeping shape is exactly what a shape that landed
        // on a sleeping dot becomes -- skip it and the dot is left inside it for ever.
        if (ball_shape::insideOutline(point_, body.position.data(), body.count)) return true;
        if (body.frozen) continue;
        // And where the shape was, to where it is now: the piece of its outline the
        // step swept, and every corner of it through the same travel. A body falling a
        // quarter of a metre a tick is swept, not sampled.
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 before = body.previous[i], after = body.position[i];
            const Vec3 side = body.position[(i + 1) % body.count] - body.position[i];
            const Vec3 sideBefore = body.previous[(i + 1) % body.count] - body.previous[i];
            if (length(side) <= 0.0 && length(sideBefore) <= 0.0) continue;
            // A corner's travel, against the point's own radius.
            const Vec3 travel = after - before;
            const double lengthSquared = dot(travel, travel);
            const double along = lengthSquared > 1e-18 ? std::clamp(dot(point_ - before, travel) / lengthSquared, 0.0, 1.0) : 0.0;
            if (length(point_ - (before + travel * along)) <= kPointRadius) return true;
            // And the side swept through the tick, which is what catches a face coming
            // down flat across the point with neither of its ends anywhere near it.
            for (const double fraction : {0.0, 0.5, 1.0}) {
                const Vec3 a = body.previous[i] + travel * fraction;
                const Vec3 b = body.previous[(i + 1) % body.count] + travel * fraction;
                const Vec3 edge = b - a;
                const double edgeSquared = dot(edge, edge);
                const double place = edgeSquared > 1e-18 ? std::clamp(dot(point_ - a, edge) / edgeSquared, 0.0, 1.0) : 0.0;
                if (length(point_ - (a + edge * place)) <= kPointRadius) return true;
            }
        }
    }
    return false;
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
        // A resting point is still something a shape can be dropped on, and a point
        // that is not stepped runs no sweep: the shape would go through it and be
        // answered a tick later, from inside, which is the clipping. So the dot is
        // asked, every tick, whether a body has arrived at it -- and if one has, the
        // point wakes and its own tick answers it.
        if (pointFrozen_ && (pointReachedByBodies() || pointSupportGone())) {
            pointFrozen_ = false;
            resting_ = false;
            restingBody_ = -1;
            restingOnBall_ = false;
        }
        // The ledger belongs to one tick: every force written into it was applied
        // during that tick, and a stale arrow is worse than no arrow.
        clearForces();
        // What this tick is going to step, worked out before it starts stepping: an
        // object that is stepped writes its own forces, so an object that is not
        // stepped is the only one whose forces have to be written for it below. A
        // body that falls asleep at the end of its own tick has already said what
        // was acting on it, and adding a second, static, pair on top of that would
        // be counting the same weight twice.
        //
        // "Stepped" is not the same question as "awake now": a body that began the tick
        // asleep and was woken by a load arriving on it was stepped, and its forces
        // for this tick are the solver's. Asking only what it was when the tick began
        // wrote the replay on top of the solver's answer, and the triangle under a
        // cube was drawn carrying twice what holds it up.
        for (std::size_t index = 0; index < kBodyCount; ++index) bodies_[index].woken = false;
        std::array<bool, kBodyCount> stepped{};
        for (std::size_t index = 0; index < kBodyCount; ++index) stepped[index] = !bodies_[index].frozen;
        const bool pointStepped = !pointFrozen_;
        const bool ballStepped = !ball_.frozen || ballNeighboursMoving();
        if (!pointFrozen_) {
            tick();
        } else {
            // The dot is asleep, so the bodies and the ball are stepped on their
            // own. The dot's own handle is the only thing left out.
            for (std::size_t index = 0; index < kBodyCount; ++index) {
                Body& body = bodies_[index];
                if (body.frozen) {
                    body.previousCentre = body.centre;
                    body.previousAngle = body.angle;
                    body.previous = body.position;
                    body.velocity = {};
                    body.spin = 0.0;
                }
            }
            tickBodies();
            if (!ball_.frozen || ballNeighboursMoving()) tickBall();
            else { ball_.previous = ball_.position; ball_.previousAngle = ball_.angle; }
        }
        // Whatever a turn carried through the ball is put back out, once, after both
        // halves of the tick have moved. It is here rather than inside either of them
        // because it is about the state the tick ends in, whichever of the two put it
        // there -- and the ball's sweep cannot see a shape turning into it.
        separateBall();
        // Read after the tick, so it covers the bodies the tick woke as well as the
        // ones that were already awake when it began.
        for (std::size_t index = 0; index < kBodyCount; ++index) stepped[index] = stepped[index] || bodies_[index].woken;
        freezeCheck();
        // What is still acting, now that the tick has finished with everything. It
        // is written here, at the end, because that is when it is known which things
        // were stepped and which were not: a body that was woken by this tick's
        // contacts has already written the forces that woke it, and a body that is
        // still asleep has written nothing at all.
        recordStaticForces(stepped, pointStepped, ballStepped);
        ++ticks;
    }
}

void PlaygroundRoom::recordStaticForces(const std::array<bool, kBodyCount>& stepped, bool pointStepped,
                                        bool ballStepped) {
    // A body that has come to rest is still pulled down by its own weight and still
    // held up by what it is standing on. Its tick is what would have said so, and
    // it has none -- so if the ledger were left to the ticks alone, every settled
    // shape would be drawn as a shape with no forces on it at all, which is the one
    // thing a body at rest is not.
    //
    // What holds it up is not invented here either: it is the answer the solver
    // gave for this body in the last tick it was awake. A shape comes to rest over
    // twenty ticks inside one small box, so that last tick is a tick of a body
    // standing still, and its contacts are exactly the ones holding it: two loads
    // under a face, the load and the friction under a shape on a slope, the blow a
    // neighbour is leaning on it with. That is what is written back out, at the
    // places it was given, in newtons -- so a sleeping body's arrows are its own
    // solved forces, and not a share of them worked out afterwards. A body on the
    // level floor shows its two reactions and no friction; a body resting on a slope
    // shows the friction that is holding it up, because that friction was solved for
    // and it is in the ledger like any other force.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (!body.frozen || stepped[index] || body.count == 0) continue;
        const double piece = body.mass / static_cast<double>(body.count);
        for (std::size_t i = 0; i < body.count; ++i)
            addForce(body.position[i], Vec3{0.0, -piece * kGravity, 0.0}, ForceKind::Gravity, nullptr, index);
        for (std::size_t i = 0; i < body.appliedCount; ++i)
            addForce(body.applied[i].at, body.applied[i].impulse * (1.0 / kTick), body.applied[i].kind,
                     nullptr, index);
    }
    // The ball asleep on something is the same pair again: its weight, and the
    // surface carrying it. A ball at rest has no other forces -- no drag, because
    // it is not moving, and no blows, because nothing is arriving -- so those two
    // arrows are the whole of what is acting on it, and leaving them out was the
    // ledger showing an empty ball while it is very much being pressed into the
    // floor by its own weight. The surface answers at the point it presses, and a
    // ball held still on a slope is held by its normal force and its friction
    // together, whose sum is exactly its weight -- so one arrow of -weight at that
    // point is the true total, not an approximation of it.
    if (ball_.frozen && !ballStepped) {
        const Vec3 weight{0.0, -ball_.mass * kGravity, 0.0};
        const Vec3 at = ball_.position + ball_.supportNormal * (-ball_.radius);
        addForce(ball_.position, weight, ForceKind::Gravity, nullptr, kBallOwner);
        // Split the way the dot's is, and for the same reason: what the ground
        // answers is a normal and a friction, and replaying the weight as one arrow
        // squeezed the friction out of sight. A ball held still on anything that is
        // not level is held by both, and both are drawn.
        const Vec3 normal = ball_.supportNormal;
        const double into = dot(weight, normal);
        const double carried = std::max(0.0, -into);
        addForce(at, normal * carried, ForceKind::Contact, nullptr, kBallOwner);
        const Vec3 along = weight - normal * into;
        if (length(along) > 0.0) addForce(at, along * -1.0, ForceKind::Friction, nullptr, kBallOwner);
    }
    // The dot, asleep on something, is the same pair of forces at one point -- and
    // the point it is pressed at is the one its own support says.
    if (pointFrozen_ && !pointStepped) {
        const Vec3 weight{0.0, -kMass * kGravity, 0.0};
        addForce(point_, weight, ForceKind::Gravity, nullptr, kDotOwner);
        // The surface holds it in two parts, and it is drawn in two parts: the
        // normal that presses it out of the face, and the friction that stops it
        // running along the face. Replaying the whole of the weight back as one
        // arrow said the face was answering straight up, which on anything but a
        // level floor is simply not what is happening -- and it hid the friction
        // completely, because a friction that is part of another arrow cannot be
        // seen as the thing holding the dot up.
        const Vec3 normal = supportNormal_;
        const double into = dot(weight, normal);       // negative when pressing in
        const double carried = std::max(0.0, -into);
        addForce(point_, normal * carried, ForceKind::Contact, nullptr, kDotOwner);
        const Vec3 along = weight - normal * into;     // what the face cannot answer
        if (length(along) > 0.0) addForce(point_, along * -1.0, ForceKind::Friction, nullptr, kDotOwner);
    }
}

// The room's energy, counted from what the simulation actually holds: the speed
// of every corner, the dot, and the ball, and the height each of them is at.
//
// This is the check the forces answer to. Gravity moves energy from the heights
// into the speeds and a contact moves it back; neither may make any, and the
// total is the one number that says so. It is also the number N puts on screen,
// so a solver that invents motion cannot hide: the sum climbs.
void PlaygroundRoom::energyBudget(double& kinetic, double& potential) const {
    kinetic = 0.0;
    potential = 0.0;
    // The dot: one point of mass kMass.
    kinetic += 0.5 * kMass * dot(velocity_, velocity_);
    potential += kMass * kGravity * point_.y;
    // Every corner of every body is a point of the same mass, and a body that is
    // asleep has none of either.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0) continue;
        // Half the mass times the square of the speed of the centre of mass, plus
        // half the moment of inertia times the square of the turn: that is the
        // whole of a rigid body's kinetic energy, and no corner-by-corner sum of
        // speeds can add up to it.
        kinetic += 0.5 * body.mass * dot(body.velocity, body.velocity);
        kinetic += 0.5 * body.moment * body.spin * body.spin;
        potential += body.mass * kGravity * body.centre.y;
    }
    // The ball: its travel and its turn, and the same height rule.
    kinetic += 0.5 * ball_.mass * dot(ball_.velocity, ball_.velocity);
    // A solid sphere, which is the moment the ball's own contacts are solved with.
    const double moment = 0.4 * ball_.mass * ball_.radius * ball_.radius;
    kinetic += 0.5 * moment * ball_.spin * ball_.spin;
    potential += ball_.mass * kGravity * ball_.position.y;
}

double PlaygroundRoom::totalEnergy() const {
    double kinetic = 0.0, potential = 0.0;
    energyBudget(kinetic, potential);
    return kinetic + potential;
}

} // namespace gtc::native
