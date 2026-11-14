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
#include <cstdio>
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
    extern int gtcAudit;
    const Vec3 wasSpeed = body.velocity;
    const double wasSpin = body.spin;
    body.velocity = wasSpeed * kept;
    if (gtcAudit) std::fprintf(stderr, "AUD absorb b%td kept %.6f (%+.6f,%+.6f)->(%+.6f,%+.6f)\n", &body - bodies_.data(), kept, wasSpeed.x, wasSpeed.y, body.velocity.x, body.velocity.y);
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

}  // namespace gtc::native
