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
using namespace playground_detail;
using namespace playground_tick;

// What of a push on a body actually becomes a step of that body.
//
// A shape that is standing on the floor cannot be driven into it: the floor is under
// it and the floor does not give, so the part of the push that runs downwards is the
// floor's to answer and is taken at the place it is given. This is the one rule for
// it -- pushBody() applies it and every compliance in the room is measured from it, so
// that the solver's model of what a shape will do and what the shape then does are the
// same model. Two models is a contact that never closes: the solver lays on impulse
// after impulse for a speed the shape never takes, and the shape next to it is handed
// all of it. That is the shape-against-shape pump.
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

// A side impact on a body standing on the floor is a coupled contact. The blow
// tends to turn one floor corner down; the floor's upward impulse changes the
// contact-point velocity during the impact, not one tick later. Solve the two
// normal impulses together, and retain the free-body answer if the floor is not
// actually needed. The floor response is frictionless here; its normal cannot pull.
double PlaygroundRoom::sideImpactImpulse(Body& body, Vec3 at, Vec3 normal, double ballShare, double closing,
                                         double rebound, double& bodyShare) {
    const double freeImpulse = std::max(0.0, (rebound - closing) / (ballShare + bodyShare));
    if (body.pivoted || std::abs(normal.y) > 1e-8) return freeImpulse;
    const Vec3 arm = at - body.centre;
    const double lever = arm.x * normal.y - arm.y * normal.x;
    double largestViolation = 0.0, chosenCoupling = 0.0, chosenCompliance = 0.0, chosenSpeed = 0.0;
    Vec3 support{};
    for (std::size_t i = 0; i < body.count; ++i) {
        const Vec3 corner = body.position[i];
        if (corner.y > playground_tick::kSurfaceY + playground_tick::kQuantum) continue;
        const double floorArm = corner.x - body.centre.x;
        const double coupling = normal.y / body.mass + lever * floorArm / body.moment;
        const double compliance = 1.0 / body.mass + floorArm * floorArm / body.moment;
        const double floorSpeed = pointSpeed(body, corner).y;
        const double violation = freeImpulse * coupling - floorSpeed;
        if (coupling > 0.0 && violation > largestViolation) {
            largestViolation = violation;
            chosenCoupling = coupling;
            chosenCompliance = compliance;
            chosenSpeed = floorSpeed;
            support = corner;
        }
    }
    if (!(largestViolation > 0.0)) return freeImpulse;
    const double share = bodyShare - chosenCoupling * chosenCoupling / chosenCompliance;
    const double divisor = ballShare + share;
    if (!(divisor > 1e-12)) return freeImpulse;
    const double impulse = std::max(0.0, (rebound - closing - chosenCoupling * chosenSpeed / chosenCompliance) / divisor);
    const double floorImpulse = std::max(0.0, (impulse * chosenCoupling - chosenSpeed) / chosenCompliance);
    if (!(floorImpulse > 0.0)) return freeImpulse;
    bodyShare = share;
    pushBody(body, support, Vec3{0, floorImpulse, 0}, ForceKind::Contact);
    return impulse;
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
    // It is the rule the ball already follows -- see ballFree(ball, ) and
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
    const Vec3 given = kind == ForceKind::Ball ? bodyFree(body, impulse) : impulse;
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
void PlaygroundRoom::pushBall(Ball& ball, Vec3 at, Vec3 impulse, ForceKind kind) {
    if (!(length(impulse) > 0.0)) return;
    const Vec3 arm = at - ball.position;
    const double moment = 0.4 * ball.mass * ball.radius * ball.radius;
    ball.velocity = ball.velocity + impulse * (1.0 / ball.mass);
    ball.spin += (arm.x * impulse.y - arm.y * impulse.x) / moment;
    ball.blows = ball.blows + impulse;
    ball.frozen = false;
    // The ball is the room's other kind of solid, and it is its own entry: an impulse
    // on it is a force on the ball, not on whatever it is touching.
    recordImpulse(at, impulse, kind, ownerOf(ball));
}

// An impulse on a ball that is resting on something: the translation it is free to
// take and the turn of the whole impulse. See the note beside BallSupport.
void PlaygroundRoom::pushBallSupported(Ball& ball, Vec3 at, Vec3 impulse, ForceKind kind, std::size_t except) {
    if (!(length(impulse) > 0.0)) return;
    const Vec3 arm = at - ball.position;
    const double moment = 0.4 * ball.mass * ball.radius * ball.radius;
    const Vec3 free = ballFree(ball, impulse, except);
    ball.velocity = ball.velocity + free * (1.0 / ball.mass);
    // The turn comes from the impulse the ball was actually given, not from the one
    // that was offered. The part a support refuses is refused whole: it is not a push
    // on the ball, and a push that never happened cannot turn it. Taking the moment of
    // the offered impulse spun a ball pinched against the floor -- a ball with no
    // speed to speak of, gathering twenty-five radians a second of turn out of nothing
    // -- and that turn is a surface speed the next contact answers, so the pair of
    // them pumped each other, round after round, until the ball left at eight metres a
    // second from standing still. A refused push is now nothing at all in both halves
    // of the ball's answer, which is the only way the two add up.
    ball.spin += (arm.x * free.y - arm.y * free.x) / moment;
    ball.blows = ball.blows + free;
    ball.frozen = false;
    // What is written down is what the ball felt, and that is the projected impulse:
    // the part the supports refused was refused by a surface, and it appears in the
    // ledger as that surface's answer -- which is what the account at the end of
    // tickBall works out. Recording the whole impulse here booked the ball as taking
    // a blow it never took, and the account was off by twenty-five newton seconds on
    // a cube dropped a metre onto it.
    recordImpulse(at, free, kind, ownerOf(ball));
}

// A point of the room meets a body. The same law as every other contact: the
// closing speed along the normal is what the two of them answer, the body's share
// of the answer is its inverse mass plus its lever about the centre, and the
// impulse is given at the place the dot touched -- so a dot landing on the far end
// of a shape turns it, which is what it always should have done.

// Gravity, and nothing else, before the walk: m*g newtons at every corner, which
// is the whole body's weight at its centre of mass and no torque at all. The
// corners are treated exactly as the material points they are -- each one gets the
// impulse its own weight delivers over the tick -- so the body is not told that it
// is heavy, it is pulled at every point and the sum of those pulls is what it
// feels.
void PlaygroundRoom::applyBodyForces() {
    const double share = kMass;  // every corner is a point of the body's mass
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        body.previousCentre = body.centre;
        body.previousAngle = body.angle;
        body.previous = body.position;
        body.touchingPlane = false;
        body.onBody = false;
        // What was against the ball last tick is an arrival this tick only if it was
        // not.
        body.wasOnBall = body.onBall;
        body.onBall = false;
        body.supportLow = 1e30;
        body.supportHigh = -1e30;
        body.supportFrozen = false;
        if (body.count == 0) continue;
        if (body.frozen || bodyHeld(index)) continue;
        // Only a body that is about to be solved for forgets what it was given last
        // tick; a sleeping body keeps it, because that is what it is standing on.
        body.appliedCount = 0;
        applyBodyWeight(index);
    }
}

// One body's own weight, corner by corner.
void PlaygroundRoom::applyBodyWeight(std::size_t index) {
    Body& body = bodies_[index];
    const double share = kMass;  // every corner is a point of the body's mass
    for (std::size_t i = 0; i < body.count; ++i) {
        const Vec3 weight{0.0, -share * kGravity, 0.0};
        addForce(body.position[i], weight, ForceKind::Gravity, nullptr, index);
        // The impulse the weight delivers over one tick, at that very corner.
        // Written out rather than short-circuited to g*dt: what a body feels is
        // a force at a place, and this is that force at that place.
        const Vec3 jog = weight * kTick;
        const Vec3 arm = body.position[i] - body.centre;
        // A pinned body's weight still acts, corner by corner, exactly like this -- but
        // it cannot carry the body anywhere, because the axle will not have it, and the
        // corners' moments about the middle cancel to nothing when the middle is the
        // centre of mass. So the weight of a seesaw does nothing at all, which is the
        // honest answer: a plank balanced on its own middle stays where it is put.
        if (body.pivoted) continue;
        body.velocity = body.velocity + jog * (1.0 / body.mass);
        body.spin += (arm.x * jog.y - arm.y * jog.x) / body.moment;
    }
    // The pin's own bearing, which is the only thing that can ever take a turn out
    // of a body whose centre of mass is its pivot. It is a couple at the axle, against
    // the turn, and it is worth mu * N * r where N is the weight the axle carries --
    // the plank's own, corner by corner, and nothing else, because the pin carries the
    // plank and the floor carries what stands on the plank. It can stop the turn and
    // it can never drive one: the same law as the ball's rolling resistance, and
    // applied the same way, so a plank left spinning comes to rest instead of turning
    // for ever.
    if (body.pivoted && body.spin != 0.0) {
        const double carried = body.mass * kGravity;
        const double couple = kAxleBearingFriction * carried * kAxleRadius;  // N.m
        const double take = couple * kTick / body.moment;                    // rad/s
        const double turn = std::copysign(std::min(std::abs(body.spin), take), body.spin);
        body.spin -= turn;
        if (body.spin == 0.0) body.spin = 0.0;
        addCouple(body.pivotPoint, couple, Vec3{0.0, 0.0, 0.0}, ForceKind::Rolling, index);
    }
}

// The air, on everything in the room that moves through it.
//
// This is a force in its own right and it is applied in a pass of its own, before
// the weight and independently of it -- not as something that happens to a body
// because it is falling. A shape sliding along the floor at speed is slowed by the
// air exactly as a shape dropping through it is, a shape turning is slowed by the
// air it turns through, a dot thrown sideways is slowed the same way, and so is the
// ball. Nothing that moves is exempt, and nothing has to be falling to feel it.
//
// The law is the flat-plate pressure law, which is what drag *is* for a body like
// these: each face of the outline takes the pressure of the air it is pushing into,
// and the pressure is the drag equation with the area that face actually presents --
//
//     F = half * rho * Cd * L * depth * v_n^2,     along the face's outward normal,
//
// applied at the middle of the face, where that pressure acts. v_n is the speed of
// the face through the air *along its own normal*, so a face edge-on to the flow is
// not dragged at all, and only the faces the air is being driven into take anything:
// the lee side of a shape is not pushed, it is a shadow.
//
// Two things follow from doing it this way, and both of them matter. The area is the
// face's own L * depth, so a square presents the one face it is driving into and not
// the sum of every side it happens to have -- asking each corner for half of each of
// its sides asked for four times the shape. And the force acts along the normal of
// the face, at the middle of the face, so a shape moving straight through the air
// takes a force through its middle and does not turn: a body is only turned by the
// air when the air is taking hold of it unevenly, which is a real effect -- a shape
// falling at an angle does turn, and that is why a leaf tumbles -- but a shape with
// no turn of its own must not be given one.
void PlaygroundRoom::applyAirDrag() {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        // A body the mouse is carrying is not being accelerated by anything: its
        // speed is put there by the hand that holds it, and the hand is the force.
        // The weight pass leaves such a body alone for the same reason, and the air
        // does too -- otherwise a shape being carried across the room is drawn with
        // a drag that is nowhere in the motion that actually happened.
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        // The whole shape's force first, and then the impulse that force delivers over
        // a tick -- which is not the force times the tick, for the same reason it is not
        // for the ball: a force that can turn a body round inside one tick is not a
        // force, and the air is always against the motion. The law is
        // dv/dt = -(c/m) v^2, whose exact answer over a tick is a speed reduced by the
        // factor 1/(1 + x) with x = |F| dt / (m |v|) -- a part in twenty at eight metres
        // a second, and the whole of the body's momentum at a hundred kilometres a
        // second, and never more than that. So every face's impulse takes that one
        // factor, and the arrows stay the forces the faces really feel.
        Vec3 total{};
        double fastest = length(body.velocity);
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 from = body.position[i];
            const Vec3 to = body.position[(i + 1) % body.count];
            const Vec3 along = to - from;
            const double span = length(along);
            if (!(span > 0.0)) continue;
            // The outline runs one way round, so this is the way out of the shape.
            const Vec3 out{along.y / span, -along.x / span, 0.0};
            const Vec3 at = from + along * 0.5;
            const double vn = dot(pointSpeed(body, at), out);
            fastest = std::max(fastest, length(pointSpeed(body, at)));
            // Only a face the air is being driven into is pressed; the one behind is
            // in the shape's shadow and the air there is not pushing it anywhere.
            if (!(vn < 0.0)) continue;
            total = total + out * (0.5 * ball_shape::kAirDensity * kPlateDragCoefficient * span * kRoomDepth * vn * vn);
        }
        // The tick's share of it. A body that is not moving is not being pressed by
        // anything, so there is nothing to share out.
        double share = 1.0;
        if (length(total) > 0.0 && fastest > 0.0)
            share = 1.0 / (1.0 + length(total) * kTick / (body.mass * fastest));
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 from = body.position[i];
            const Vec3 to = body.position[(i + 1) % body.count];
            const Vec3 along = to - from;
            const double span = length(along);
            if (!(span > 0.0)) continue;
            const Vec3 out{along.y / span, -along.x / span, 0.0};
            const Vec3 at = from + along * 0.5;
            const double vn = dot(pointSpeed(body, at), out);
            if (!(vn < 0.0)) continue;
            const double press = 0.5 * ball_shape::kAirDensity * kPlateDragCoefficient * span * kRoomDepth * vn * vn;
            const Vec3 air = out * press;
            addForce(at, air, ForceKind::Drag, nullptr, index);
            const Vec3 puff = air * (kTick * share);
            const Vec3 arm = at - body.centre;
            // A pinned body is carried by its axle and by nothing else: the air is
            // still a force on it, corner by corner, but the part of that force which
            // would take the middle somewhere is the axle's to refuse.
            body.velocity = body.velocity + puff * inverseMass(body);
            body.spin += (arm.x * puff.y - arm.y * puff.x) / body.moment;
        }
    }
}


}  // namespace gtc::native
