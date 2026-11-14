// Every impulse the room gives a body or the ball, and nothing else.
//
// These are the only doors into a solid's motion anywhere in the room, because an
// impulse at a place is the only thing a force can do to a rigid body: change the
// speed of its centre of mass by J/m and its turn by the moment of J about the
// centre, r x J / I. The corners are not touched here -- they are where the shape
// says they are, and they move because the body does.
//
// A push is not always taken whole. A shape standing on the floor cannot be driven
// into it, and a ball resting on something cannot be pushed through it either; what
// a support refuses is answered by the support, at the place the support holds, and
// it is written down there. That is the same rule for both of the room's solids, and
// it lives here so that it is one rule and not two.

#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {

// What of a push on a body actually becomes a step of that body. See the header.
//
// A pinned body is held by its axle, which is a thing that never moves and never
// fails: it answers the carrying part of any push, and the turn is the body's own.
// That is a different question from the floor's, and it is already answered by
// inverseMass() and by pushBody()'s own pin.
Vec3 PlaygroundRoom::bodyFree(const Body& body, Vec3 push) {
    if (body.pivoted) return push;
    if (!body.touchingPlane || push.y >= 0.0) return push;
    return Vec3{push.x, 0.0, push.z};
}

// One impulse, given to one body at one place. This is the only door into a
// body's motion anywhere in the room, because it is the only thing a force can do
// to a rigid body: change the speed of its centre of mass by J/m and its turn by
// the moment of J about the centre, r x J / I. The corners are not touched here --
// they are where the shape says they are, and they move because the body does.
void PlaygroundRoom::pushBody(Body& body, Vec3 at, Vec3 impulse, ForceKind kind) {
    if (!(length(impulse) > 0.0)) return;
    const Vec3 arm = at - body.centre;
    // A pinned body is not carried anywhere: the axle is what takes the push, and the
    // only thing left for the body to do with it is turn. The axle's own answer is not
    // written as a force on the body because it acts at a point that never moves, so
    // it does no work and changes nothing the body can report -- it is the room that
    // holds the pin, exactly as the floor holds a shape standing on it.
    // A shape standing on the floor cannot be driven into it, and the floor is what
    // answers the part of a push that would.
    //
    // It is the rule the ball already follows -- see ballFree() and
    // pushBallSupported(): a support takes the share of a blow that is into it, and
    // what is left is the body's. It matters for the shapes because the ball's own
    // contacts are solved *after* the shapes have had their tick, so a push the ball
    // gives a shape is never seen by the floor under it: the shape is handed a speed
    // into the floor which the next tick's solver quietly takes away again.
    //
    // In the King's corner -- a ball wedged between a cube's side and the triangle's
    // slope, both of them standing on the floor -- that speed was the whole of the
    // trouble. The ball pressed the slope, the slope was handed a receding surface
    // speed it could never have had (0.16 m/s at the contact, most of it from a turn
    // about a centre a metre and a half away), the ball's grip read the face as
    // running away from it and was dragged along, and the corner never settled: three
    // bodies holding a speed they never spent, twenty times a second, with the
    // momentum thrown away unrecorded on the next tick. That is the "infinite force"
    // -- a wall that answers with no force at all.
    //
    // So the part of the push that is into the floor never reaches the shape at all:
    // what a support refuses is not a force on the thing it refuses to move. The
    // shape keeps the rest, and the arrows are the rest -- the same rule the ball
    // follows in pushBallSupported(), and for the same reason: an arrow for a blow
    // that was refused would be an arrow with no momentum behind it.
    const Vec3 given = impulse;
    body.velocity = body.pivoted ? Vec3{} : body.velocity + given * (1.0 / body.mass);
    body.spin += (arm.x * given.y - arm.y * given.x) / body.moment;
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
            body.applied[slot].impulse = body.applied[slot].impulse + given;
        } else if (body.appliedCount < Body::kMaxRestForces) {
            body.applied[body.appliedCount++] = Body::Applied{at, given, kind};
        }
    }
    const std::size_t index = std::size_t(&body - bodies_.data());
    // What is written down is what was applied. A push a support refuses is not a
    // force on this body -- it never moved it -- and an arrow for it would be an
    // arrow with no momentum behind it: the ledger would read two blows where the
    // body felt one. The refusal is the support's business, and it is the same rule
    // pushBallSupported() has always followed for the ball.
    recordImpulse(at, given, kind, index);
}

// And the same for the ball: a blow at a place changes its speed along the line of
// the blow, and its turn by the moment of it, with the moment of a solid sphere.
void PlaygroundRoom::pushBall(Vec3 at, Vec3 impulse, ForceKind kind) {
    if (!(length(impulse) > 0.0)) return;
    const Vec3 arm = at - ball_.position;
    const double moment = 0.4 * ball_.mass * ball_.radius * ball_.radius;
    ball_.velocity = ball_.velocity + impulse * (1.0 / ball_.mass);
    ball_.spin += (arm.x * impulse.y - arm.y * impulse.x) / moment;
    ballBlows_ = ballBlows_ + impulse;
    ball_.frozen = false;
    // The ball is the room's other kind of solid, and it is its own entry: an impulse
    // on it is a force on the ball, not on whatever it is touching.
    recordImpulse(at, impulse, kind, kBallOwner);
}

// An impulse on a ball that is resting on something: the translation it is free to
// take and the turn of the whole impulse. See the note beside BallSupport.
void PlaygroundRoom::pushBallSupported(Vec3 at, Vec3 impulse, ForceKind kind, std::size_t except) {
    if (!(length(impulse) > 0.0)) return;
    const Vec3 arm = at - ball_.position;
    const double moment = 0.4 * ball_.mass * ball_.radius * ball_.radius;
    const Vec3 free = ballFree(impulse, except);
    ball_.velocity = ball_.velocity + free * (1.0 / ball_.mass);
    // The turn comes from the impulse the ball was actually given, not from the one
    // that was offered. The part a support refuses is refused whole: it is not a push
    // on the ball, and a push that never happened cannot turn it. Taking the moment of
    // the offered impulse spun a ball pinched against the floor -- a ball with no
    // speed to speak of, gathering twenty-five radians a second of turn out of nothing
    // -- and that turn is a surface speed the next contact answers, so the pair of
    // them pumped each other, round after round, until the ball left at eight metres a
    // second from standing still. A refused push is now nothing at all in both halves
    // of the ball's answer, which is the only way the two add up.
    ball_.spin += (arm.x * free.y - arm.y * free.x) / moment;
    ballBlows_ = ballBlows_ + free;
    ball_.frozen = false;
    // What is written down is what the ball felt, and that is the projected impulse:
    // the part the supports refused was refused by a surface, and it appears in the
    // ledger as that surface's answer -- which is what the account at the end of
    // tickBall works out. Recording the whole impulse here booked the ball as taking
    // a blow it never took, and the account was off by twenty-five newton seconds on
    // a cube dropped a metre onto it.
    recordImpulse(at, free, kind, kBallOwner);
}

// A point of the room meets a body. The same law as every other contact: the
// closing speed along the normal is what the two of them answer, the body's share
// of the answer is its inverse mass plus its lever about the centre, and the
// impulse is given at the place the dot touched -- so a dot landing on the far end
// of a shape turns it, which is what it always should have done.

}  // namespace gtc::native
