// When a shape comes to rest, and when a load wakes it.
//
// Sit still and be still are two different statements, and this file is about the
// second one. A body's place is decided by the tick that steps it; whether that place
// has stopped changing is a question about a whole second of ticks, and whether the
// thing under it is really carrying it is a question about the load. Neither belongs
// in the file that gathers contacts, which is why they are here.

#include "client/playground_ball_shape.h"
#include "client/playground_pair.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {
using namespace playground_detail;
using namespace playground_tick;

namespace {
// How long a body's pose has to stay inside one small box before it is at rest,
// and how small that box is. A shape standing on its supports does not go
// anywhere; the last thing it does is settle, and settling is measured, not
// declared. The box is the width of the contact band over a second -- a millimetre
// a tick -- which is far below anything that can be seen at the scale of a body.
// How close to a surface counts as being on it. A micrometre: the walk stops at the
// touching instant to an arithmetic fraction of a step, and this is the width of the
// band in which the two are the same place.
constexpr double kOnSurface = kQuantum;
constexpr int kRestTicks = 20;
constexpr double kRestBox = 6e-3;     // m
constexpr double kRestTurn = 1.2e-2;  // rad
}  // namespace

// A body's rest, decided from its own motion and from what is under it. A shape
// whose weight does not run between the places it is supported at is not carried
// by them -- it tips, and that is what the physics does on its own, without a rule
// that says so.
void PlaygroundRoom::settleBodies() {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (body.count == 0) continue;
        if (bodyHeld(index)) continue;
        if (body.frozen) {
            // Asleep is asleep: no speed, no turn, and the tick that finds it so
            // leaves it exactly where it is.
            body.velocity = {};
            body.spin = 0.0;
            continue;
        }
        // A pinned body is held up by its pin, which is a thing that cannot fail: it is
        // always carried, so it can come to rest like anything else and wake when it is
        // pushed. Without this it would be a body that never sleeps, and a seesaw left
        // alone would be re-solved sixty times a second for ever.
        const bool carriedByPlane = body.pivoted ||
                                    (body.touchingPlane && body.centre.x >= body.supportLow - kContactBand &&
                                     body.centre.x <= body.supportHigh + kContactBand);
        // Being held up by another body counts only if that body is *carrying* it.
        // A shape asleep is treated as a surface, and a surface never feels anything:
        // a cube propped against a sleeping triangle is stopped by a wall of infinite
        // mass, held by friction that wall never paid for, creeps into a still state
        // and then goes to sleep -- and from then on the pair is two sleeping bodies
        // that cannot wake each other, with the cube standing on a slope at an angle
        // no friction could hold and the triangle's arrows showing its own weight and
        // nothing else. That is the construction that looks impossible and is.
        //
        // So the support has to be a solved one: the body under this one must have this
        // one's load in the forces it was left with. If it has not, this body is not
        // being held, it is only touching, and it stays awake -- and because it stays
        // awake, the body under it is woken too, and the pair is solved with both of
        // them from the next tick, which is when the load first becomes real.
        const bool carriedByBody = body.onBody && body.supportFrozen && carriedBySolvedSupport(index) &&
                                   body.centre.x >= body.supportLow - kContactBand &&
                                   body.centre.x <= body.supportHigh + kContactBand;
        // And a shape resting on a ball that has come to rest is standing on a
        // surface like any other. The ball is asleep, so it is not going to roll out
        // from under anything: it is a floor with a curve, and the load on it runs
        // down through it into the ground.
        //
        // It had no way of knowing that, and the cost was the last of the King's
        // corner. A triangle lying on its slope against a sleeping ball is held by the
        // floor at one corner and by the ball at the other; only the corner was ever
        // recorded as a support, so its weight was read as running outside what holds
        // it, it was called a body that is not carried, and it never spent the second
        // of stillness that puts a shape to sleep. So it was solved against the ball
        // for ever -- a contact, a push, the pair parting by 65 um, a tick of free
        // fall, the walk stopping it at the surface again, contact -- a two-tick limit
        // cycle that walked the triangle 49 um a tick in and out and kept the ball
        // awake to be crept along. Nothing was moving. A shape standing on something
        // that is standing still is standing still.
        Vec3 ballWay{}, ballAt{};
        // Every ball, because a shape standing on a ball is standing on that one: the
        // question is not "is there a ball asleep in the room", it is "is the ball
        // under me asleep".
        bool sleepingBallUnder = false;
        for (const Ball& ball : balls_) {
            if (!ball.frozen) continue;
            if (bodyBallGap(ball, index, ballWay, ballAt) > kContactBand) continue;
            if (!(ballWay.y > 0.5)) continue;
            sleepingBallUnder = true;
            break;
        }
        const bool carriedByBall = body.onBall && sleepingBallUnder &&
                                   body.centre.x >= body.supportLow - kContactBand &&
                                   body.centre.x <= body.supportHigh + kContactBand;
        const bool carried = carriedByPlane || carriedByBody || carriedByBall;
        // Nothing is at rest that is not being held up: a shape in the air is
        // falling, whatever its arithmetic says.
        if (!carried) {
            body.restTicks = 0;
            body.restLow = body.restHigh = body.centre;
            body.restAngleLow = body.restAngleHigh = body.angle;
            continue;
        }
        if (body.restTicks == 0) {
            body.restLow = body.restHigh = body.centre;
            body.restAngleLow = body.restAngleHigh = body.angle;
        }
        body.restLow = Vec3{std::min(body.restLow.x, body.centre.x), std::min(body.restLow.y, body.centre.y), 0};
        body.restHigh = Vec3{std::max(body.restHigh.x, body.centre.x), std::max(body.restHigh.y, body.centre.y), 0};
        body.restAngleLow = std::min(body.restAngleLow, body.angle);
        body.restAngleHigh = std::max(body.restAngleHigh, body.angle);
        ++body.restTicks;
        const bool anywhere = body.restHigh.x - body.restLow.x < kRestBox &&
                             body.restHigh.y - body.restLow.y < kRestBox &&
                             body.restAngleHigh - body.restAngleLow < kRestTurn;
        // And if it has stepped outside that box, it has moved, and a body that has
        // moved has not been still: the second it is being asked about starts again
        // from here. The box used to be left standing on the fall it was opened
        // from, so a body that came down and then stood perfectly still for ever was
        // measured against the width of its landing -- five centimetres across, never
        // once back inside -- and the count reached twenty, found itself outside a box
        // it had left a season ago, and started over. Measured: a cube whose place and
        // angle did not change in the twelfth decimal for three hundred ticks, whose
        // speed was exactly nothing, never once was called a body at rest, and every
        // arrow the room drew for it was drawn about a shape it believed to be moving.
        if (!anywhere) {
            body.restTicks = 1;  // this tick is the first of the new second
            body.restLow = body.restHigh = body.centre;
            body.restAngleLow = body.restAngleHigh = body.angle;
            continue;
        }
        // Asleep, or not yet: a body that has been inside one small box for a
        // second is standing still on what is holding it up.
        if (body.restTicks >= kRestTicks) {
            // Asleep where the physics left it. Nothing is moved to make it look
            // tidier: a body that is standing still is standing where it stands.
            body.frozen = true;
            // What it was still carrying, written down as the room's answer to it.
            //
            // A body falling asleep used to have its speed simply set to nothing, and
            // a body whose speed is set to nothing has had a force on it that was never
            // drawn: the arrows it was given over that tick came to less than the
            // momentum it actually lost, and the account was short by exactly the
            // piece that was wiped. Measured: a cube settling onto a spinning seesaw
            // had (-0.000163,-0.000189) m/s taken off it in the one tick it was put to
            // sleep -- 6.6e-4 N.s of momentum the room took and wrote nowhere -- and
            // the ledger could not be balanced for that tick at all.
            //
            // absorbRefusedStep() is the same answer the room already gives a body
            // whose step is refused, and it is the same thing happening: the body was
            // going to go somewhere and something it is standing on did not let it.
            // At the resolution the room works in that is a contact like any other,
            // written at the part of the body that was pressing.
            absorbRefusedStep(body, index, 0.0);
            body.shown = body.position;
        }
    }
}

// A body that is asleep is asleep on something. Take that something away -- drag
// the shape it leans on, roll the ball out from under it, open the floor -- and it
// is not at rest any more, it is a body with nothing holding it up, and it falls.
// Nothing here decides how a body should move: it only asks whether the surface it
// was resting on is still where it was, and if it is not, hands the body back to
// the simulation, which then does what gravity and its contacts say.
void PlaygroundRoom::wakeUnsupportedBodies() {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (!body.frozen || body.count == 0 || bodyHeld(index)) continue;
        bool touching = false;
        for (std::size_t i = 0; i < body.count && !touching; ++i)
            if (body.position[i].y <= kOnSurface) touching = true;
        for (std::size_t other = 0; other < kBodyCount && !touching; ++other) {
            if (other == index || bodies_[other].count == 0) continue;
            if (pairGap(body.position.data(), body.count, bodies_[other].position.data(), bodies_[other].count).gap <=
                kContactBand)
                touching = true;
        }
        Vec3 way{}, at{};
        for (const Ball& ball : balls_) {
            if (touching) break;
            if (bodyBallGap(ball, index, way, at) <= kContactBand) touching = true;
        }
        if (touching) continue;
        body.frozen = false;
        body.woken = true;
        body.restTicks = 0;
        body.restLow = body.restHigh = body.centre;
        body.restAngleLow = body.restAngleHigh = body.angle;
    }
}

// A body that is asleep is asleep on something, and it is also *under* something
// whenever anything is standing on it -- and a load is not a wall.
//
// Asleep, a body is treated as an immovable surface, which is right for the floor and
// for the room's own fixtures and wrong for another shape. The pair patch made against
// it is answered entirely on the other side: the shape leaning on it is stopped, and
// the load it is leaning with is thrown away, because as far as the arithmetic goes
// there is nothing there to feel it. That is how a cube can sit on a triangle's slope
// with the triangle's own arrows showing forty newtons of nothing but gravel: the cube
// weighs 39.2 N, the triangle under it carries 29.4 N, and the missing 39.2 N went
// nowhere at all. Worse than the arrows: the triangle could not tip. A shape at rest
// under a load is not at rest -- it is a shape carrying a load, and whether it can
// carry it is exactly the question a shape leaning on it is asking.
//
// So a sleeping body with something awake pressing on it is woken, and the solver
// answers the pair with both sides. Once both have settled they sleep again together,
// which is where it stops: two bodies asleep cannot wake each other, so this is not a
// pulse. It is one wake, on the tick a load arrives, and the load is in the ledger from
// then on because a body that goes to sleep carries what it was holding.
void PlaygroundRoom::wakeLoadedBodies(double band) {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (!body.frozen || body.count == 0 || bodyHeld(index)) continue;
        bool pressed = false;
        for (std::size_t other = 0; other < kBodyCount && !pressed; ++other) {
            if (other == index || bodies_[other].count == 0) continue;
            // Something asleep cannot press on anything: it has no speed and no weight
            // of its own to spend. Only an awake neighbour is a load arriving.
            if (bodies_[other].frozen) continue;
            if (pairGap(body.position.data(), body.count, bodies_[other].position.data(),
                        bodies_[other].count).gap <= band)
                pressed = true;
        }
        // The ball is not a reason to wake a shape, and must not be made one. Its own
        // sweep answers every body it meets, asleep or not, and it answers with the
        // ball's own bounciness; a body woken by a ball rolling up to it takes the
        // meeting into the pair solver instead, which is a resting contact and gives
        // nothing back -- a ball fired at a cube went from bouncing off it to sticking
        // to it and being dragged along, at exactly the wall's speed. So the ball is
        // left out here: it does not need the shape to be awake to be stopped by it.
        if (!pressed) continue;
        body.frozen = false;
        body.woken = true;
        body.restTicks = 0;
        body.restLow = body.restHigh = body.centre;
        body.restAngleLow = body.restAngleHigh = body.angle;
        // This wake happens after the tick's gravity pass, and a woken body is not
        // replayed either -- so without this the one tick a body is woken on would
        // be the one tick it is drawn with no weight at all. Its corners are pulled
        // here exactly as they would have been there, and its contacts are solved
        // afterwards in the same tick, so nothing is counted twice.
        applyBodyWeight(index);
    }
}

// One tick of every body: the forces, the contacts, and then the travel.
//
// There is no walking and no stopping short any more. A tick is one step: the air and
// the weight are given to the bodies as impulses, every contact the tick's own travel
// can reach is gathered and answered, and then each body is carried by the speed it
// is left with for exactly one tick. A contact is answered against the distance the
// end still has to travel, so a shape arriving at a surface lands *on* it rather than
// stopping a hair short of it; and because the band is the travel, nothing can arrive
// between the pose the contacts were gathered at and the pose the tick ends at.
//

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
    for (Ball& ball : balls_) ball.touchCount = 0;
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
        body.pivoted = shape.pivoted;
        body.pivotPoint = shape.middle;
        body.frozen = false;
        body.contacts = 0;
        placeCorners(body);
        body.previous = body.shown = body.position;
    }
    // A new ball, at the same place it starts in. The scene is not rebuilt by a
    // reset, so the two drawing handles it was given at build time have to be
    // kept: clearing them to zero puts the ball's transform on the ground batch
    // and leaves the ball itself where the last scene was built.
    //
    // Both balls, and both for the same reasons. The one on the rope is a ball: it
    // has the same two drawing handles, the same radius, the same mass and the same
    // tick, and resetting it is resetting a ball. It is not a second thing to be
    // written out here.
    for (std::size_t index = 0; index < kBallCount; ++index) {
        Ball& ball = balls_[index];
        const std::size_t face = ball.faceBatch, mark = ball.markBatch;
        ball = Ball{};
        ball.faceBatch = face;
        ball.markBatch = mark;
    }
    // The thrown ball, where the scene leaves it: up in the air, on the right.
    Ball& thrown = balls_[kThrowBall];
    thrown.position = thrown.previous = thrown.shown = Vec3{3.10, thrown.radius + 1.60, 0};
    thrown.angle = thrown.previousAngle = 0.0;
    // And the ball on the rope: hanging dead straight under its anchor, at rest. The
    // room resets to a room that is standing still. What makes it swing is a blow, and
    // the room has a ball for that -- the same tick, the same contact, the same ledger.
    // It keeps its two drawing handles like the other one, and the rope keeps its own.
    const std::size_t ropeBatch = rope_.ropeBatch, anchorBatch = rope_.anchorBatch;
    rope_ = Rope{};
    rope_.ropeBatch = ropeBatch;
    rope_.anchorBatch = anchorBatch;
    rope_.ball = kRopeBall;
    rope_.attached = true;
    Ball& hanging = balls_[kRopeBall];
    hanging.position = hanging.previous = hanging.shown = rope_.anchor + Vec3{0.0, -rope_.length, 0.0};
    hanging.angle = hanging.previousAngle = 0.0;
    // Hanging taut: the rope is carrying the whole of its weight and no more.
    rope_.taut = true;
    rope_.tension = hanging.mass * kGravity;
    restingOnBall_ = false;
    restingBall_ = kThrowBall;
    dragBall_ = kThrowBall;
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



}  // namespace gtc::native
