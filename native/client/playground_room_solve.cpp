// The answer to a configuration of contacts, in one place.
//
// This is the half of a tick that decides what everything in the room is standing on
// and how hard: the patches, the load each of their ends carries, and the grip that
// holds a face along the surface it is on. It is here rather than beside the rest of
// the tick because it is long, and because it can be read on its own -- nothing below
// moves anything. An impulse is given, the body's speed and spin change by it, and the
// walk does the moving afterwards.
//
// The one thing that is not a velocity here is the gap. What an end is *allowed* to do
// about the distance it has left to the surface is a decision about how a rigid face
// meets a flat one, and it is where a resting pair is either left still or made to
// walk; every word of it is written where it is used.

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
// How many sweeps of the patches one tick is solved in. Sequential impulses
// converge quickly for a handful of contacts; this is the point at which a solve
// that is still moving is taken to be a corner case, not a solve.
constexpr int kSolvePasses = 32;
// How far apart two ends of one face may be, in height above the surface, and still
// be the same contact rather than a shape coming down on a corner. A millimetre on a
// shape seven hundred millimetres across is a seventh of a degree: it is the noise a
// resting pair sits in, and it is not a shape landing. Anything more is.
constexpr double kFaceFlat = 1e-3;// A sweep of the patches is over when nothing it can still see is closing faster
// than this. A millionth of a metre per second is a thousandth of a millimetre in
// a whole tick: slower than the arithmetic of the positions can even show, and
// small enough that what is left over cannot pump a stacked pair.
constexpr double kSolveTolerance = 1e-12;// A sweep of the patches is over when it has stopped changing the answer. Every
// impulse it applies is a change, so this is measured in newton seconds: a thousandth
// of a micronewton second is nothing next to a corner's weight over a tick (half a
// newton second), and it is far smaller than anything the eye or the integrator can
// see. It is a separate number from the one above because it answers a separate
// question: the sweep now stops when the *answer* has stopped moving rather than when
// the surfaces have stopped closing.
constexpr double kImpulseTolerance = 1e-12;}  // namespace

// The answer to a whole configuration of patches at once.
//
// One patch is solved as one load with a pressure that runs from one end to the
// other: two ends, two unknown impulses, and the requirement that the body is not
// driven into the surface at either of them. That is a two by two system, and it is
// solved as one -- which is the whole point. Two ends answered separately can each
// stop their own end and leave the body turning; two ends answered together stop
// the body where it stands, and a face that has landed flat stays flat instead of
// rocking from corner to corner. When the answer at an end would be a pull, the
// floor lets go there and the patch is carried by the other end alone: that is a
// body lifting off, decided by the load and not by a rule about corners.
//
// The load and the grip are swept in that order, and the normal is what the sweep
// ends on. They are not independent: grip is what the normal pressure can carry, and
// grip at the sole of a shape lands a moment on it that changes where the shape
// presses. Sweeping one and then the other converges on a state where the two agree,
// and ending on the normal means a tick can never finish with a corner still being
// driven into the floor -- hold and slip may be a hair from where they settle, but
// nothing goes into anything.
bool PlaygroundRoom::solveContacts(ManifoldSet& patches) {
    bool closingAtAll = false;
    // The largest change the last sweep made, in newton seconds. A sweep is over when
    // it has nothing left to say -- see kImpulseTolerance for why the question is
    // asked about the answer and not about how fast the surfaces are closing.
    double worst = 0.0;

    // A patch is one face meeting one surface, and it is answered as a whole.
    //
    // A face lying on the floor presses over both of its ends, and what it carries
    // is a pressure that runs from one end to the other. So its ends are solved
    // together, as one system, and the answer taken is the one that leaves neither
    // end with a way into the surface -- both of them at rest against it. That is
    // what a pressure between two ends can do and a single point cannot: an impulse
    // at one corner of a face is an impulse off the line the weight runs along, so
    // it turns the body; two of them, at the two corners, can carry the weight where
    // it runs and turn the body not at all. Answered one end at a time the end
    // solved last is the only one carrying anything, which is a shape standing on
    // one corner of its face -- it turns, it rocks, and it never lies down.
    //
    // The pressure can only push. When the answer at one end would be a pull, the
    // face is lifting there: that end carries nothing, gives back what it took, and
    // the load is answered again over the ends that are left. That is a body tipping
    // because its weight no longer runs between its supports, decided by the load
    // and not by a rule about corners. A patch whose ends both lift carries nothing.
    //
    // The grip comes after the load, against the load it has under it: a surface
    // holds a face along itself only as hard as it is pressing it, end by end. Load
    // and grip are swept in that order -- grip lands a moment on the body that
    // changes where it presses -- and the sweep ends on the load, so no tick
    // finishes with a corner being driven into the floor.
    const auto patchPass = [&](Manifold& patch) {
        const std::size_t count = patch.ends;
        Body* body = &bodies_[patch.body];
        Body* other = (!patch.ball && patch.other < kBodyCount) ? &bodies_[patch.other] : nullptr;
        // A body asleep is a surface, for the same reason the ball is: it has no speed
        // for a contact to change and nothing in the room can move it, so it may not
        // take a share of anything. Treated as movable it would be pushed by every
        // contact made against it, woken by the push, and then start the next tick on
        // a speed no force gave it -- which is a shape that buzzes itself awake once a
        // second for ever.
        const bool otherFixed = other != nullptr && (bodyHeld(patch.other) || other->frozen);
        // A ball that is asleep is a surface, in the same way a body that is asleep
        // is: it has no speed to speak of and nothing a contact can move. Its own
        // speed has to be left out of the answer, or a shape pressed onto a sleeping
        // ball is answered against motion that is not happening -- and its share of
        // what an impulse gives has to be left out too, because the impulse is not
        // going to move it anywhere.
        const bool ballFixed = patch.ball && ball_.frozen;
        const double ballMoment = 0.4 * ball_.mass * ball_.radius * ball_.radius;
        const Vec3 tangent{patch.normal.y, -patch.normal.x, 0.0};
        const auto lever = [](Vec3 arm, Vec3 axis) { return arm.x * axis.y - arm.y * axis.x; };
        // How fast a place on the body is going away from the other side of the
        // patch along a direction.
        const auto away = [&](Vec3 at, Vec3 direction) {
            Vec3 otherSpeed{};
            if (other != nullptr) {
                if (!otherFixed) otherSpeed = pointSpeed(*other, at);
            } else if (patch.ball) {
                if (!ballFixed) otherSpeed = ballPointSpeed(at);
            }
            return dot(pointSpeed(*body, at) - otherSpeed, direction);
        };
        // What the whole pair answers to an impulse: how fast the body's place `at`
        // is moved along `toward` by an impulse given at `pushAt` along `direction`.
        // The reciprocal of the mass plus the two levers, for the body and for
        // whatever is on the other side of the patch.
        const auto give = [&](Vec3 at, Vec3 toward, Vec3 pushAt, Vec3 direction) {
            double value = dot(toward, direction) / body->mass +
                           lever(at - body->centre, toward) * lever(pushAt - body->centre, direction) /
                               body->moment;
            if (other != nullptr) {
                if (!otherFixed)
                    value += dot(toward, direction) / other->mass +
                             lever(at - other->centre, toward) * lever(pushAt - other->centre, direction) /
                                 other->moment;
            } else if (patch.ball) {
                if (!ballFixed)
                    value += dot(toward, direction) / ball_.mass +
                             lever(at - ball_.position, toward) * lever(pushAt - ball_.position, direction) /
                                 ballMoment;
            }
            return value;
        };
        const auto pushOther = [&](Vec3 at, Vec3 impulse, ForceKind kind) {
            if (other != nullptr) {
                if (!otherFixed) pushBody(*other, at, impulse, kind);
            } else if (patch.ball) {
                if (!ballFixed) pushBall(at, impulse, kind);
            }
        };
        const auto pushEnd = [&](std::size_t k, Vec3 impulse, ForceKind kind) {
            if (impulse.x == 0.0 && impulse.y == 0.0 && impulse.z == 0.0) return;
            pushBody(*body, patch.point[k], impulse, kind);
            pushOther(patch.point[k], impulse * -1.0, kind);
        };
        // This body and its partner have now been answered at a patch: whatever
        // the patch's ends go on to do, the body has met something this tick, and
        // its travel is the solver's answer rather than a free fall.
        if (count > 0) {
            ++body->met;
            if (other != nullptr) ++other->met;
        }
        // What the ends were closing at before this answer, for the walk.
        for (std::size_t k = 0; k < count; ++k) {
            const double closing = away(patch.point[k], patch.normal);
            if (closing < -kRestSpeed) closingAtAll = true;
        }
        // The ends that are carrying, and what each of them has carried so far this
        // tick.
        bool live[kMaxPatchEnds] = {false, false};
        double carried[kMaxPatchEnds] = {};
        for (std::size_t k = 0; k < count; ++k) {
            live[k] = true;
            carried[k] = patch.normalImpulse[k];
        }
        for (std::size_t attempt = 0; attempt <= count; ++attempt) {
            // The system over the ends still carrying: what each is closing at, and
            // what the pair does when it is pressed at each of them.
            std::size_t order[kMaxPatchEnds] = {}, dofs = 0;
            double u[kMaxPatchEnds] = {}, g[kMaxPatchEnds][kMaxPatchEnds] = {};
            // How far the face may still come down this tick.
            //
            // A face is either on the surface or it is still on its way in, and the
            // two are answered differently, because they are different things.
            //
            // On its way in, each end may close exactly the distance it has left and
            // not a micron more: that is a shape arriving, and the answer leaves it
            // on the surface when the tick ends, rather than stopping short and being
            // answered a tick late from inside.
            //
            // Once any end of the face is on the surface, the face is standing on
            // something, and the ends that are on it may not close at all. That is
            // the whole of it: zero, for every end that is touching.
            //
            // Zero and not "each its own gap", which is what it used to be, and which
            // is a hinge. Two ends of one rigid face given two different distances to
            // close cannot both obey -- the only way a rigid body closes one end by a
            // tenth of a millimetre and the other by nothing is to turn, so the solver
            // turned it. That turn was not free: friction is real, an end that is
            // turning has a speed along the surface, and the grip answered it with an
            // impulse along the surface at each end. The two ends' loads are not equal,
            // so those two impulses do not cancel, and the pair was left with a small
            // net push sideways -- a cube and a triangle resting on each other walking
            // slowly along a level floor for ever, with no arrow on the screen pointing
            // that way, because no force was pushing them. Only a contact being asked
            // to hold a face that it had been told was turning. The numbers it turned
            // on were two corners of one shape a ten-thousandth of a millimetre either
            // side of the floor: noise, read as a lean, and then paid for by walking.
            //
            // Zero and not a recovery push, either. A corner that is *inside* the
            // floor is a state the physics never puts the room in -- it gets there
            // because the walk could not see it coming, or a hand dragged a shape into
            // another -- and it is the walk's business, not the velocity's: the shape
            // is moved out of it by its place, by separateBodies(), and nothing is
            // added to its speed. Giving it a separating speed instead meant every
            // resting shape was quietly lifted a fraction of a micron every tick,
            // which is the slow climb a stacked pair was showing: the same contact,
            // answered twice.
            // Is this face lying on the surface, or landing on a corner of it?
            //
            // The two look alike from one end and are nothing alike. A face lying on
            // something has its ends at the same height to within the thickness of a
            // hair -- a millimetre across a shape seven hundred of them wide -- and
            // whatever ails the pair of numbers, it is not that the shape is landing.
            // A face coming down on a corner has one end near the surface and the
            // other a long way above it, and that end is still falling.
            //
            // It matters because the two ends of one rigid face cannot close different
            // distances over one tick: the only way a rigid body does that is to turn,
            // and a face that is lying on something must not be turned by the noise in
            // two numbers. Lying, the face is answered as one: it has one clearance,
            // the deepest end's, and it may close that and nothing more. Landing, each
            // end may close the distance it has left, which is what a corner coming
            // down onto a floor is.
            double lowest = 1e30, highest = -1e30;
            for (std::size_t k = 0; k < count; ++k) {
                if (!live[k]) continue;
                lowest = std::min(lowest, patch.gap[k]);
                highest = std::max(highest, patch.gap[k]);
            }
            const bool lying = highest - lowest <= kFaceFlat;
            // Lying, the clearance the face may still close is the deepest end's, and
            // a face that is already touching may close nothing: a corner inside the
            // surface is a state the walk put it in, and the walk takes it out.
            const double faceClose = lying && lowest > 0.0 ? lowest * (1.0 / kTick) : 0.0;
            for (std::size_t k = 0; k < count; ++k) {
                if (!live[k]) continue;
                order[dofs] = k;
                const double bias =
                    lying ? faceClose : (patch.gap[k] > 0.0 ? patch.gap[k] * (1.0 / kTick) : 0.0);
                u[dofs] = away(patch.point[k], patch.normal) + bias;
                // Both halves of the matrix, because what one end is given at the
                // other end's place is what the other end is given at its own: the
                // answer is a push at one place and its reaction at another, and that
                // is symmetric. Filled on one side only -- which is what this was --
                // the system is not the room's, and its answer is not either: a face
                // pressed at two ends came away with one end carrying half again the
                // weight of the other, out of a coupling that was only asked one way.
                for (std::size_t j = 0; j <= dofs; ++j) {
                    const double coupled = give(patch.point[order[j]], patch.normal, patch.point[k], patch.normal);
                    g[j][dofs] = coupled;
                    g[dofs][j] = coupled;
                }
                ++dofs;
            }
            // The load each end needs to be left with no way into the surface.
            double delta[kMaxPatchEnds] = {};
            if (dofs == 1) {
                if (g[0][0] > 1e-18) delta[0] = -u[0] / g[0][0];
            } else if (dofs == 2) {
                const double det = g[0][0] * g[1][1] - g[0][1] * g[1][0];
                if (std::abs(det) > 1e-18) {
                    delta[0] = (-u[0] * g[1][1] + u[1] * g[0][1]) / det;
                    delta[1] = (-u[1] * g[0][0] + u[0] * g[1][0]) / det;
                }
            }
            bool lifting = false;
            for (std::size_t i = 0; i < dofs; ++i) {
                const std::size_t k = order[i];
                // An end is let go only when the load it would be left with is a
                // pull. An end that needs exactly nothing keeps its place: it is
                // still standing on something, and the pass after this one may well
                // find it closing again.
                if (carried[k] + delta[i] >= 0.0) continue;
                // A pull: the floor lets go here. What this end took is given back
                // and the rest of the patch is answered again without it.
                pushEnd(k, patch.normal * -carried[k], patch.kind);
                worst = std::max(worst, std::abs(carried[k]));
                carried[k] = 0.0;
                patch.normalImpulse[k] = 0.0;
                live[k] = false;
                lifting = true;
            }
            if (lifting) continue;
            for (std::size_t i = 0; i < dofs; ++i) {
                const std::size_t k = order[i];
                if (delta[i] == 0.0) continue;
                pushEnd(k, patch.normal * delta[i], patch.kind);
                worst = std::max(worst, std::abs(delta[i]));
                carried[k] += delta[i];
                patch.normalImpulse[k] = carried[k];
            }
            break;
        }
        // The grip, over both ends together, against the load they share.
        //
        // Coulomb, in the two forms it has, and it is the same law in both: the
        // surface holds along itself with whatever it takes to leave the face still
        // against it, up to what the load under it can hold. The coefficient is what
        // differs. An end that is already slipping is held by the sliding
        // coefficient, and it is held by that much *only against the slip* -- a
        // surface can stop a slide and can never drive one backwards, so an end that
        // is moving is never given more than it takes to bring it to rest.
        //
        // Both ends are answered as one system, and that is the whole of it.
        //
        // A face lying on a surface has two places it can be held and two ways it can
        // move along the surface: it can slide, and it can turn. Answering one end and
        // then the other is answering the same two things twice with the first answer
        // already in the way, and the sweep only approaches the right answer if it is
        // allowed to run long enough. It was not: the sweep stopped when the *load*
        // looked settled, which says nothing about the grip, so the grip was left
        // wherever the second end's impulse happened to put it.
        //
        // What was left was not noise. A body pressed down on two ends with slightly
        // different loads gets slightly different grips at them, and if those two
        // grips do not cancel exactly, what is left is a force along the surface that
        // nothing in the room is applying. A cube and a triangle resting on each other
        // walked slowly along a level floor for ever, in the same direction, with no
        // arrow on the screen pointing that way: they were being pushed by the
        // difference between two friction impulses that should have been a couple.
        //
        // Solved together they cannot do that. The unknowns are the two ends' grips,
        // the equations are that neither end is left slipping, and a rigid face has
        // exactly two ways to slide so the system is square: the answer is the one
        // that leaves the face still, and if it cannot be still -- the load is not
        // enough to hold it -- the end that is over its cone is pinned there and the
        // rest is answered again, exactly as an end that lifts is.
        const double staticMu =
            other != nullptr ? 0.5 * (body->staticFriction + other->staticFriction)
            : patch.ball   ? std::min(body->staticFriction, ball_shape::kBallStaticFriction)
                           : body->staticFriction;
        const double kineticMu =
            other != nullptr ? 0.5 * (body->kineticFriction + other->kineticFriction)
            : patch.ball   ? std::min(body->kineticFriction, ball_shape::kBallKineticFriction)
                           : body->kineticFriction;
        {
            bool carrying[kMaxPatchEnds] = {false, false};
            bool pinned[kMaxPatchEnds] = {false, false};
            double grip[kMaxPatchEnds] = {}, cone[kMaxPatchEnds] = {};
            double slipping[kMaxPatchEnds] = {};
            for (std::size_t k = 0; k < count; ++k) {
                grip[k] = patch.tangentImpulse[k];
                if (!(patch.normalImpulse[k] > 0.0)) {
                    // No pressure, nothing to grip with. Whatever this end had is
                    // given back, so the ledger and the motion agree.
                    if (grip[k] != 0.0) {
                        pushEnd(k, tangent * -grip[k], ForceKind::Friction);
                        worst = std::max(worst, std::abs(grip[k]));
                        grip[k] = 0.0;
                        patch.tangentImpulse[k] = 0.0;
                    }
                    continue;
                }
                carrying[k] = true;
                slipping[k] = away(patch.point[k], tangent);
                const double mu = std::abs(slipping[k]) > kRestSpeed ? kineticMu : staticMu;
                cone[k] = mu * patch.normalImpulse[k];
            }
            for (std::size_t attempt = 0; attempt <= count; ++attempt) {
                std::size_t order[kMaxPatchEnds] = {}, dofs = 0;
                double u[kMaxPatchEnds] = {}, g[kMaxPatchEnds][kMaxPatchEnds] = {};
                for (std::size_t k = 0; k < count; ++k) {
                    if (!carrying[k] || pinned[k]) continue;
                    order[dofs] = k;
                    u[dofs] = away(patch.point[k], tangent);
                    for (std::size_t j = 0; j <= dofs; ++j) {
                        const double coupled = give(patch.point[order[j]], tangent, patch.point[k], tangent);
                        g[j][dofs] = coupled;
                        g[dofs][j] = coupled;
                    }
                    ++dofs;
                }
                if (dofs == 0) break;
                double delta[kMaxPatchEnds] = {};
                if (dofs == 1) {
                    if (g[0][0] > 1e-18) delta[0] = -u[0] / g[0][0];
                } else {
                    const double det = g[0][0] * g[1][1] - g[0][1] * g[1][0];
                    if (std::abs(det) > 1e-6 * std::max(1e-18, std::abs(g[0][0] * g[1][1]))) {
                        delta[0] = (-u[0] * g[1][1] + u[1] * g[0][1]) / det;
                        delta[1] = (-u[1] * g[0][0] + u[0] * g[1][0]) / det;
                    } else {
                        // A face lying flat is one grip, not two.
                        //
                        // Its two ends are at the same height, so a push along the
                        // surface at one end and a push along it at the other are the
                        // same push as far as the shape is concerned: two forces along
                        // one line, which can add up and cannot twist anything. There
                        // is no way to split that push that the face can tell apart,
                        // and asking for a split anyway is asking a singular system
                        // for an answer -- which is no answer at all, and friction
                        // that never fires.
                        //
                        // So the smallest split that does the job is taken, which is
                        // an equal share of one impulse: the pseudo-inverse of the
                        // system, and the only one of the infinitely many answers that
                        // does not invent a difference between two places the physics
                        // says are the same. If one end's share is more than its own
                        // load can hold, it is pinned at its cone below and the other
                        // is answered again -- which is a face whose light end slips
                        // while its loaded end holds, as it should.
                        const double aa = g[0][0] * g[0][0] + g[0][1] * g[1][0];
                        if (aa > 1e-18) {
                            const double factor = -(g[0][0] * u[0] + g[0][1] * u[1]) / aa;
                            delta[0] = factor * g[0][0];
                            delta[1] = factor * g[0][1];
                        }
                    }
                }
                // What the cone will not allow: that end holds as hard as it can and
                // the other is answered again without it. Beyond that the face slips,
                // and the slip is left in its motion, where it belongs.
                bool overCone = false;
                for (std::size_t i = 0; i < dofs; ++i) {
                    const std::size_t k = order[i];
                    const double total = grip[k] + delta[i];
                    if (std::abs(total) <= cone[k]) continue;
                    const double held = std::copysign(cone[k], total);
                    const double change = held - grip[k];
                    if (change != 0.0) {
                        pushEnd(k, tangent * change, ForceKind::Friction);
                        worst = std::max(worst, std::abs(change));
                    }
                    grip[k] = held;
                    patch.tangentImpulse[k] = held;
                    pinned[k] = true;
                    overCone = true;
                }
                if (overCone) continue;
                for (std::size_t i = 0; i < dofs; ++i) {
                    const std::size_t k = order[i];
                    if (delta[i] == 0.0) continue;
                    pushEnd(k, tangent * delta[i], ForceKind::Friction);
                    worst = std::max(worst, std::abs(delta[i]));
                    grip[k] += delta[i];
                    patch.tangentImpulse[k] = grip[k];
                }
                break;
            }
        }
        // What is under each end decides what the body is standing on.
        for (std::size_t k = 0; k < count; ++k) {
            if (!(patch.normalImpulse[k] > 0.0) || !(patch.normal.y > 0.5)) continue;
            ++body->contacts;
            if (other == nullptr && !patch.ball) {
                body->touchingPlane = true;
            } else if (other != nullptr) {
                body->onBody = true;
                body->supportFrozen = other->frozen;
                body->supportBody = patch.other;
            }
            body->supportLow = std::min(body->supportLow, patch.point[k].x);
            body->supportHigh = std::max(body->supportHigh, patch.point[k].x);
        }
    };

    for (int pass = 0; pass < kSolvePasses; ++pass) {
        worst = 0.0;
        for (std::size_t m = 0; m < patches.count; ++m) patchPass(patches.items[m]);
        if (worst < kImpulseTolerance && pass > 0) break;
    }
    return closingAtAll;
}

}  // namespace gtc::native
