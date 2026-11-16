#pragma once

// The room's rigid-body and contact-patch state, separate from its solver API.
#include "native.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace gtc::native::playground_room {

template <std::size_t kMaxCorners, std::size_t kBodyCount, typename ForceKind>
// A solid: one rigid body, with a mass, a moment of inertia, a place, a turn,
// a speed and a spin. Its corners are the shape, not the state.
struct Body {
    std::size_t count = 0;
    // The shape about the body's own centre of mass, at zero turn. It never
    // changes: a body is not stretched, bent or repaired, ever.
    std::array<Vec3, kMaxCorners> local{};
    // Where it is and how far it has turned. These two are the whole of its
    // position, and the corners are computed from them on demand.
    Vec3 centre{}, previousCentre{};
    double angle = 0.0, previousAngle = 0.0;
    // What it is doing: the speed of its centre of mass, and its turn about
    // that centre in radians per second.
    Vec3 velocity{};
    double spin = 0.0;
    // What it arrived with, before this tick's forces changed it. A bounce is
    // the answer to an arrival: the weight it gains on the way down is what
    // carried it here, and bouncing that back would be bouncing it off nothing --
    // and it is not nothing at rest, where the whole of the speed is the tick's
    // own gravity, so a shape resting on the ball was bounced a little every
    // tick and stood humming a centimetre above it.
    Vec3 arrivalVelocity{};
    double arrivalSpin = 0.0;
    // What it is: the mass of the whole body and the moment of inertia about
    // its centre of mass, both from the shape as a uniform plate.
    double mass = 1.0, moment = 1.0;
    // The corners of this tick and of the last, and the ones the screen shows.
    // They are placeCorners() of the two above, kept so that the contacts, the
    // dot and the drawing can read them without recomputing.
    std::array<Vec3, kMaxCorners> position{}, previous{}, shown{};
    // This body's own friction, from its shape's material.
    double staticFriction = 0, kineticFriction = 0;
    bool frozen = false;
    // Woken during this tick, by a load arriving on it or by its support going
    // away. It was stepped, so its forces are the ones the solver wrote and not a
    // replay of its last waking tick -- counting both would draw its weight twice.
    bool woken = false;
    int contacts = 0;
    // Whether the ball is one of the things holding this body up this tick, and
    // whether it was last tick. A bounce is an *event*: it is the answer to
    // arriving at a surface, and the coefficient of restitution is what turns the
    // arrival round. A contact that goes on being a contact is not arriving any
    // more -- it is resting -- and asking it to give the arrival back again every
    // tick is a pump. It is easy to miss, because each tick's increment is small:
    // a weight settling onto the ball gains the tick's gravity, the ball gives
    // three quarters of it straight back, the body falls onto the ball again with
    // that much more, and the pair climb for ever. Whatever comes out of the
    // bounce then has to obey the grip, which turns it into a spin, which is the
    // squeeze the King saw.
    bool onBall = false, wasOnBall = false;
    // Whether this body is pinned at a point and can only turn about it. A
    // seesaw is a plank on an axle: the axle holds its middle in one place, so
    // the plank cannot be carried anywhere, and the one thing it can do is turn.
    // That is not a rule bolted onto the motion -- it is the constraint the axle
    // is, and it enters the physics in the one place every constraint does, as
    // how much the body gives when it is pushed: nothing along any direction,
    // and its usual share of a turn about the pin.
    //
    // Nothing else needs to know. Its weight acts at its middle, which is the pin,
    // so its weight gives no torque and it hangs in neutral balance wherever it is
    // left -- which is exactly what a real seesaw on its centre does, and why they
    // need a push. A load on one end is what turns it, and the axle answers the
    // difference, which is a force at a place that never moves and therefore does
    // no work.
    bool pivoted = false;
    // Where the axle is. It never moves, and a pinned body's centre is always here.
    Vec3 pivotPoint{};
    // When, within the tick, this body's velocity was turned around by a blow
    // off the ball: the fraction of the tick that had passed when the two met.
    //
    // A blow lands at an instant, not over a whole tick. Advancing a body for a
    // full tick at the speed the blow left it with is a body that travelled the
    // whole tick upwards and not one instant of it downwards -- it is lifted off
    // the surface it bounced on, by exactly one tick of its own rebound, every
    // single bounce. A cube dropped half a metre onto the ball came back to
    // forty-one per cent more height than its own rebound allows, which reads as
    // a trampoline and is not one: no energy is made, the parabola is simply
    // started a tick too early. See advanceBodies().
    // Written while a patch is being gathered, which is a reading of the room
    // and not a change to it: what it records is when the blow it is about to
    // ask for lands. The move that spends it happens later in the same tick.
    mutable double hitAt = 1.0;
    mutable Vec3 hitArrival{};
    // Earliest positive-clearance contact in the whole-tick prediction, retained
    // separately from the body's path-walk event. The rope contact walk needs the
    // pre-impact velocity at a partial-tick activation without moving the body's
    // already-predicted trajectory a second time.
    double ropeEventAt = 1.0;
    Vec3 ropeEventVelocity{};
    double ropeEventSpin = 0.0;
    // How many contact patches this body has been answered in since the room was
    // reset. It is a counter and not a flag so that a tick can tell, by comparing
    // with what it started from, whether the body met anything at all while it
    // was being stepped -- which is what decides whether its travel this tick was
    // a free fall or a supported one.
    int met = 0;
    // What held it up this tick, worked out from the contacts it ended the tick
    // with: which way the contact patch runs, and whether what it stands on is
    // itself asleep. A body whose weight does not run between its supports is
    // not carried by them -- it tips instead, which is what a shape hanging
    // over an edge does.
    bool touchingPlane = false, onBody = false, supportFrozen = false;
    // Which body is under this one, when one is: `supportFrozen` alone cannot
    // tell a stack that was solved together from a shape propped against
    // something that has already gone to sleep and never felt it (see
    // carriedBySolvedSupport()).
    std::size_t supportBody = kBodyCount;
    double supportLow = 1e30, supportHigh = -1e30;
    // The box its pose has stayed inside, and for how many ticks: a shape that
    // has not been anywhere for a second is standing still, whether the
    // arithmetic of its contacts leaves it a hair of a rock or not.
    Vec3 restLow{1e30, 1e30, 0}, restHigh{-1e30, -1e30, 0};
    double restAngleLow = 1e30, restAngleHigh = -1e30;
    int restTicks = 0;
    // The contact forces this body was actually given in its last simulated
    // tick, as impulses, one per place they acted. A body that falls asleep has
    // no tick to be solved in, and a shape at rest is still standing on
    // something -- so what it is standing on is replayed exactly as it was last
    // solved, not invented afterwards. This is what the arrows show while a body
    // sleeps, and it is why a body on a slope shows its friction and a body on
    // the level floor shows none.
    static constexpr std::size_t kMaxRestForces = 24;
    struct Applied {
        Vec3 at{}, impulse{};
        ForceKind kind = ForceKind::Contact;
    };
    std::array<Applied, kMaxRestForces> applied{};
    std::size_t appliedCount = 0;
    std::size_t faceBatch = 0;
    std::array<std::size_t, kMaxCorners> edgeBatches{}, cornerBatches{};
};

// One patch: a face of a body resting on a surface. Its two ends are the ends
// of the load that face carries, not two independent meetings -- a body whose
// weight is answered at one corner alone cannot be held there without being
// turned by it, and that is a body that rocks forever. The pressure between
// the ends is what lets a flat face stand still, and what makes it lift off at
// one end when its weight no longer runs between them.
//
// `other` is kBodyCount when the other side is the floor, which cannot move.
// The impulses are accumulated over the solves of one tick and written into
// the ledger once the tick has finished.
template <std::size_t kMaxPatchEnds, std::size_t kBodyCount, std::size_t kNoBall, typename ForceKind>
struct Manifold {
    std::size_t body = 0, other = kBodyCount;
    // Which ball the other side is, or kNoBall for a body. It is an index and not
    // a yes: a body resting on a ball is resting on *that* ball, and the answer it
    // gets is that ball's mass, that ball's speed and that ball's support.
    std::size_t ball = kNoBall;
    std::size_t ends = 0;
    std::size_t corner[kMaxPatchEnds] = {0, 0};
    Vec3 point[kMaxPatchEnds]{}, normal{};
    // How far each end still has to fall before it is on the surface. A patch
    // is answered against the distance it really has to travel, not against the
    // instant a tick happens to end, so an end that is a hair above the floor is
    // allowed to close exactly that hair over the tick -- and no more.
    double gap[kMaxPatchEnds]{};
    double normalImpulse[kMaxPatchEnds]{}, tangentImpulse[kMaxPatchEnds]{};
    // How fast this end has to be left *leaving* after the answer, in m/s. Zero
    // everywhere the room is inelastic, which is everywhere but the ball: the
    // ball is the one thing in the room with a bounciness, so a contact against
    // it is the one contact that has to give some of the approach back. It is
    // read once, from the speed the pair arrived at, and not again on each sweep,
    // or a contact solved twice would bounce twice.
    double rebound[kMaxPatchEnds]{};
    ForceKind kind = ForceKind::Contact;
    bool quiet = false;  // already resting, not an arrival: see gatherContacts()
};

template <typename Manifold, std::size_t kMaxPatchEnds, std::size_t kMaxTickManifolds, std::size_t kBallCount>
struct ManifoldSet {
    std::array<Manifold, kMaxTickManifolds> items{};
    std::size_t count = 0;
    // A patch that is found again is the same patch: each of its ends keeps what
    // it has already carried, so a tick that solves more than once over the same
    // meeting converges on one answer instead of answering it twice.
    void add(const Manifold& patch) {
        for (std::size_t i = 0; i < count; ++i) {
            Manifold& known = items[i];
            if (known.body != patch.body || known.other != patch.other || known.ball != patch.ball)
                continue;
            // An end is the same end when it is in the same *place*, which is
            // what it has always been: the place the load acts. Matching by
            // corner number looked equivalent and was not -- an end where two
            // edges cross is not any corner at all, so both ends of a shape
            // lying across another one matched the same corner, and each
            // inherited the whole of what that one had carried. The solver then
            // started every iteration believing a load was already there when
            // it was not, and answered it again: a shape rocking on another one
            // with an amplitude that grew instead of dying -- energy out of the
            // arithmetic, which is exactly what a warm start must never do.
            double carried[kMaxPatchEnds]{}, grip[kMaxPatchEnds]{};
            for (std::size_t k = 0; k < patch.ends; ++k) {
                for (std::size_t j = 0; j < known.ends; ++j) {
                    if (patch.ball < kBallCount && j != k) continue;
                    const Vec3 apart = known.point[j] - patch.point[k];
                    if (std::hypot(apart.x, apart.y) > 1e-6) continue;
                    carried[k] = known.normalImpulse[j];
                    grip[k] = known.tangentImpulse[j];
                }
            }
            known = patch;
            for (std::size_t k = 0; k < patch.ends; ++k) {
                known.normalImpulse[k] = carried[k];
                known.tangentImpulse[k] = grip[k];
            }
            return;
        }
        if (count < items.size()) items[count++] = patch;
    }
    void clear() { count = 0; }
};

}  // namespace gtc::native::playground_room
