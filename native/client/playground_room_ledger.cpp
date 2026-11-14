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
        entry.impulse = entry.impulse + impulse;
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
    const Vec3 taken = (wasSpeed - body.velocity) * body.mass;
    if (length(taken) > 1e-12) recordImpulse(body.centre, taken, ForceKind::Contact, index);
    const double takenMoment = body.moment * (wasSpin - body.spin) / kTick;
    if (std::abs(takenMoment) > 1e-9)
        addCouple(body.pivoted ? body.pivotPoint : body.centre, takenMoment, Vec3{0, 0, 0}, ForceKind::Contact,
                  index);
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
