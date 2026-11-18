// The room's ledger of blows: every impulse applied anywhere in the room is
// written here, at the place it was applied, and turned into the arrows on screen
// at the end of the tick. It is a file of its own because it is one subject --
// what the room did to what, and where -- and because the room itself is close to
// the size a reader can hold in their head at once.
#include "client/playground_room.h"


#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"
#include "client/playground_ball_shape.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {
using namespace playground_detail;
using namespace playground_tick;

// The ledger, written where the impulse is applied. A contact solved in many passes
// is one force at one place, so an impulse already at that place and of that kind
// is added to rather than grown as a second arrow.
void PlaygroundRoom::recordImpulse(Vec3 at, Vec3 impulse, ForceKind kind, std::size_t on) {
    for (std::size_t i = 0; i < impulseCount_; ++i) {
        RecordedImpulse& entry = impulses_[i];
        if (entry.kind != kind || entry.on != on) continue;
        if (length(entry.at - at) > 1e-6) continue;
        // Two forces at one place are one arrow only when they are the *same* force.
        //
        // A contact solved in many passes is one contact: every pass adds along the
        // same line, and the arrows add up to it. Two different contacts that happen
        // to meet at the same point are not one force, and adding them is how a
        // friction arrow came to point at neither surface it was acting on. A cube
        // wedged into the corner where the floor and a slope meet has the floor
        // holding it one way and the slope another, at the same point; summed, the
        // pair read as one arrow pushing into the slope at forty-five degrees, which
        // is exactly the "friction pointing away from the triangle" the King found.
        // Measured in that scene: the floor's (0.00,0.00)+(-1.63,0.00) and the slope's,
        // added into a single (-33.08,+13.46) that is neither of them.
        //
        // So a merge is a merge only along one line. Anything else is a second force
        // at the same place, and it gets its own arrow.
        const Vec3 sum = entry.impulse + impulse;
        const double turned = length(Vec3{entry.impulse.x * impulse.y - entry.impulse.y * impulse.x, 0.0, 0.0});
        if (turned > 1e-4 * std::max(length(sum), 1e-12)) continue;
        entry.impulse = sum;
        return;
    }
    if (impulseCount_ >= impulses_.size()) return;
    impulses_[impulseCount_++] = RecordedImpulse{at, impulse, kind, on};
}

void PlaygroundRoom::absorbRefusedStep(Body& body, std::size_t index, double kept) {
    // Written as a "not" so that a step of no length at all, and a fraction that is
    // not a number, both leave the body exactly as it is.
    if (!(kept < 1.0)) return;
    kept = std::max(0.0, kept);
    const Vec3 wasSpeed = body.velocity;
    const double wasSpin = body.spin;
    body.velocity = wasSpeed * kept;
    body.spin = wasSpin * kept;
    // What the body lost, and what the room therefore gave it.
    //
    // A body that does not go somewhere does not have the momentum that would have
    // taken it, and the piece it does not have is a piece the thing in its way took.
    // That is a contact like any other -- the room's answer to an arrival, inelastic,
    // the same answer every other contact here gives -- and what is written down is
    // the impulse the *body* was given, which is the loss reversed. Written the other
    // way round, every refused step was drawn as a blow in the direction the body was
    // already going: a triangle held up by nothing showed its own weight again as a
    // contact pointing at the floor, which is the arrow the King asked about. A
    // contact can only push a body away from what it pressed on, and the ledger now
    // says so.
    const Vec3 taken = (wasSpeed - body.velocity) * body.mass;
    const Vec3 push = taken * -1.0;
    // And where it was given: at the part of the body that ran into the thing, which
    // is the part furthest along the way the momentum went -- not at the middle of the
    // body, where nothing was ever touched. A body held up at its lower corner is
    // held up at that corner; an arrow at its centre is a force on a point that met
    // nothing. A pinned body is the exception the pin makes: its middle is where it is
    // held, and there is no other place anything can meet it.
    Vec3 at = body.centre;
    if (body.pivoted) {
        at = body.pivotPoint;
    } else {
        double deepest = -1e30;
        for (std::size_t i = 0; i < body.count; ++i) {
            const double along = dot(body.position[i], taken);
            if (along > deepest) {
                deepest = along;
                at = body.position[i];
            }
        }
    }
    if (length(taken) > 1e-12) recordImpulse(at, push, ForceKind::Contact, index);
    // The turn that was refused is a couple at the same place, and it is written the
    // same way round: the moment the body was *given* is the moment it lost, reversed.
    const double takenMoment = body.moment * (body.spin - wasSpin) / kTick;
    if (std::abs(takenMoment) > 1e-9) addCouple(at, takenMoment, Vec3{0, 0, 0}, ForceKind::Contact, index);
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

void PlaygroundRoom::recordStaticForces(const std::array<bool, kBodyCount>& stepped, bool pointStepped,
                                        const std::array<bool, kBallCount>& ballStepped) {
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
    for (std::size_t index = 0; index < kBallCount; ++index) {
        const Ball& ball = balls_[index];
        if (!ball.frozen || ballStepped[index]) continue;
        const Vec3 weight{0.0, -ball.mass * kGravity, 0.0};
        const Vec3 at = ball.position + ball.supportNormal * (-ball.radius);
        addForce(ball.position, weight, ForceKind::Gravity, nullptr, ownerOf(ball));
        // Split the way the dot's is, and for the same reason: what the ground
        // answers is a normal and a friction, and replaying the weight as one arrow
        // squeezed the friction out of sight. A ball held still on anything that is
        // not level is held by both, and both are drawn.
        const Vec3 normal = ball.supportNormal;
        const double into = dot(weight, normal);
        const double carried = std::max(0.0, -into);
        addForce(at, normal * carried, ForceKind::Contact, nullptr, ownerOf(ball));
        const Vec3 along = weight - normal * into;
        if (length(along) > 0.0) addForce(at, along * -1.0, ForceKind::Friction, nullptr, ownerOf(ball));
    }
    // A pinned body's axle.
    //
    // A plank on an axle is pulled down by its weight and does not move, which can
    // only be true because the axle is pushing back -- and until that push is
    // written down, the picture shows a shape being dragged downwards with nothing
    // on the other end of it: four weights, no answer. It is not invented. A pin
    // holds the middle where it is, so the force the axle gives is exactly the one
    // that leaves nothing over: whatever the body felt this tick -- its own weight,
    // the shapes resting on it, the ball landing on it, the air -- the axle carries
    // the rest, at the pin, and it is drawn there.
    //
    // A moment is a different matter and is not made up here. A pin takes no moment
    // about itself, so it gives none: what turns a plank is its bearing's friction,
    // which is its own couple and is written where it acts.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (!body.pivoted || body.count == 0) continue;
        Vec3 left{};
        for (std::size_t i = 0; i < forceCount_; ++i) {
            const ForceSample& sample = forces_[i];
            if (sample.owner != index || sample.couple) continue;
            left = left + sample.force;
        }
        if (length(left) > 1e-9) addForce(body.pivotPoint, left * -1.0, ForceKind::Contact, nullptr, index);
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
    // The balls: their travel and their turn, and the same height rule. Both of them,
    // because both of them are balls -- the one on the rope has as much kinetic energy
    // as the one thrown, and the room's books would not balance if it were left out.
    for (const Ball& ball : balls_) {
        if (!ball.active) continue;  // not in the room
        kinetic += 0.5 * ball.mass * dot(ball.velocity, ball.velocity);
        // A solid sphere, which is the moment the ball's own contacts are solved with.
        const double moment = 0.4 * ball.mass * ball.radius * ball.radius;
        kinetic += 0.5 * moment * ball.spin * ball.spin;
        potential += ball.mass * kGravity * ball.position.y;
    }
}

double PlaygroundRoom::totalEnergy() const {
    double kinetic = 0.0, potential = 0.0;
    energyBudget(kinetic, potential);
    return kinetic + potential;
}


}  // namespace gtc::native
