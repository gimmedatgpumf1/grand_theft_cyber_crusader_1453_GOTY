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
constexpr double kImpulseTolerance = 1e-12;  // N s
// The most times a patch is asked for its load, then its grip, then its load again
// before the room is taken as answered. Grip lands a moment on a body that changes
// where it presses, so the two answers change each other, and a round that changes
// nothing is what says they agree. The count is a ceiling and not a cost: a resting
// room agrees in two or three rounds, and stops there.
constexpr int kGripRoundsMax = 48;
// What the two answers are asked to agree to, in newton seconds. A thousandth of a
// micronewton second over a tick is a force of twenty nanonewtons: far below the
// weight of a speck, and far below anything the eye can find in a shape's travel over
// an hour. Asking for more than this buys nothing but arithmetic.
constexpr double kAgree = 1e-9;  // N s
}  // namespace

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
    const auto patchPass = [&](Manifold& patch, bool withGrip) {
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
        // A real blow wakes it. The rule for a shape asleep is the same one, and it
        // is the same reason: what a sleeping thing may not do is be *treated as a
        // wall* when something arrives at speed. A cube dropped onto a sleeping ball
        // had its whole arrival cancelled by a surface that never moved -- the ball
        // was given as a fixed wall and the cube stopped on it, and the ball took
        // nothing, which is a ball that is not there. A contact that is merely
        // resting closes at nothing and does not wake it, so a shape sitting on a
        // sleeping ball still cannot buzz it awake.
        if (patch.ball && ball_.frozen) {
            for (std::size_t k = 0; k < count; ++k) {
                const double closing =
                    dot(pointSpeed(*body, patch.point[k]) - ballPointSpeed(patch.point[k]), patch.normal);
                if (closing < -kRestSpeed) {
                    ball_.frozen = false;
                    break;
                }
            }
        }
        const bool ballFixed = patch.ball && ball_.frozen;
        const double ballMoment = 0.4 * ball_.mass * ball_.radius * ball_.radius;
        // What a ball lying on something can take of an impulse, and what the thing
        // under it takes instead.
        //
        // The floor does not give. An impulse with a part pointing into the surface
        // the ball is resting on is answered by that surface, not by the ball: the
        // ball cannot move that way, so nothing is left for it to move. What is left
        // of the impulse -- the part along the surface -- is the ball's, and that part
        // does move it, which is why a shape striking the side of a ball still shoves
        // it across the room while a shape landing on the top of it is caught.
        //
        // This is a projection and not a share. A share would be a number that says
        // "the ball takes three quarters of every impulse", which is not true of any
        // direction; the projection takes exactly the part of the impulse the ball is
        // free to carry and leaves the rest with the floor, and it is the same
        // projection in the answer as in what the pair is told its own mass is -- one
        // law in both halves of the solve, or the impulse and the matrix disagree and
        // the sweep walks away with itself.
        const bool ballSupported = patch.ball && !ballFixed && ball_.touching && ball_.standing;
        const Vec3 ballSupport = ball_.supportNormal;
        const auto carriedByBall = [&](Vec3 impulse) {
            if (!ballSupported) return impulse;
            const double into = dot(impulse, ballSupport);
            return into < 0.0 ? impulse - ballSupport * into : impulse;
        };
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
                if (!ballFixed) {
                    const Vec3 carried = carriedByBall(direction);
                    value += dot(toward, carried) / ball_.mass +
                             lever(at - ball_.position, toward) * lever(pushAt - ball_.position, carried) /
                                 ballMoment;
                }
            }
            return value;
        };
        const auto pushOther = [&](Vec3 at, Vec3 impulse, ForceKind kind) {
            if (other != nullptr) {
                if (!otherFixed) pushBody(*other, at, impulse, kind);
            } else if (patch.ball) {
                if (!ballFixed) pushBall(at, carriedByBall(impulse), kind);
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
        for (std::size_t attempt = 0; withGrip ? false : attempt <= count; ++attempt) {
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
                u[dofs] = away(patch.point[k], patch.normal) + bias - patch.rebound[k];
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
        // The grip, over the face, against the load the face as a whole carries.
        //
        // Coulomb, in the two forms it has, and it is the same law in both: a surface
        // holds along itself with whatever it takes to leave the face still against
        // it, up to what the load on it can hold. The coefficient is what differs. A
        // face that is already slipping is held by the sliding coefficient, and it is
        // held by that much *only against the slip* -- a surface can stop a slide and
        // can never drive one backwards.
        //
        // Over the face and not at its ends, because friction is a traction: it is
        // spread across the area that is pressed, and its resultant runs through the
        // middle of that area. Answered at one end and not at the other, friction that
        // is nothing but a force along the surface arrives with a moment attached to
        // it, and that moment is what turns a shape that is standing still. The two
        // ends of a flat face on a flat surface have the same way of moving along it
        // -- a rigid face cannot slide at one end and not the other -- so the answer
        // is one impulse, shared equally between the ends that carry load. On a face
        // that is turning, the two ends do slip differently, and the share is still
        // equal in the sweep because the moment it must carry is in the levers, where
        // the ends are; the sweep comes round again and again until the tractions
        // agree with the load and the load agrees with them.
        //
        // A face with one end is the same rule with one share, which is the ordinary
        // point contact.
        if (withGrip) {
        const double staticMu =
            other != nullptr ? 0.5 * (body->staticFriction + other->staticFriction)
            : patch.ball   ? std::min(body->staticFriction, ball_shape::kBallStaticFriction)
                           : body->staticFriction;
        const double kineticMu =
            other != nullptr ? 0.5 * (body->kineticFriction + other->kineticFriction)
            : patch.ball   ? std::min(body->kineticFriction, ball_shape::kBallKineticFriction)
                           : body->kineticFriction;
        // The ends that are pressed, and how much load is under the whole face.
        //
        // The grip is taken back and laid down afresh every sweep rather than added
        // to, and the difference is the difference between a force and a memory of
        // one. A sweep passes through states the room is never in -- a load answered
        // before the shape above it has been answered -- and grip that is only ever
        // added to keeps what it took in those moments and goes on holding with it:
        // a face standing still on a surface, gripped by a force that is no longer
        // needed, pushing the shape along until the sweep's end. Taken back first,
        // grip is what the face needs *now*: a face that is not slipping is not being
        // pushed, and a tick that ends at rest ends with no friction in it at all.
        std::size_t pressed = 0;
        double load = 0.0;
        for (std::size_t k = 0; k < count; ++k) {
            if (patch.tangentImpulse[k] != 0.0) {
                pushEnd(k, tangent * -patch.tangentImpulse[k], ForceKind::Friction);
                worst = std::max(worst, std::abs(patch.tangentImpulse[k]));
                patch.tangentImpulse[k] = 0.0;
            }
            if (!(patch.normalImpulse[k] > 0.0)) continue;
            ++pressed;
            load += patch.normalImpulse[k];
        }
        if (pressed > 0) {
            // The face's slip: the slip of each pressed end, averaged, and what an
            // equal share of impulse at every pressed end does to that average. The
            // ends' levers are in it, so a share that turns the body is answered for
            // as the turn it makes.
            double slip = 0.0, total = 0.0;
            for (std::size_t k = 0; k < count; ++k) {
                if (!(patch.normalImpulse[k] > 0.0)) continue;
                slip += away(patch.point[k], tangent);
                for (std::size_t j = 0; j < count; ++j) {
                    if (!(patch.normalImpulse[j] > 0.0)) continue;
                    total += give(patch.point[k], tangent, patch.point[j], tangent);
                }
            }
            const double n = static_cast<double>(pressed);
            slip /= n;
            const double response = total / (n * n);
            if (response > 1e-12) {
                const double mu = std::abs(slip) > kRestSpeed ? kineticMu : staticMu;
                const double hold = mu * load;
                double grip = -slip / response;
                if (std::abs(grip) > hold) {
                    // More than the surface can hold: it slips, and what it is given
                    // is what the surface can give -- the rest of the slip is left in
                    // the body's motion, where it belongs.
                    grip = std::copysign(hold, grip);
                }
                const double share = grip / n;
                for (std::size_t k = 0; k < count; ++k) {
                    if (!(patch.normalImpulse[k] > 0.0)) continue;
                    pushEnd(k, tangent * share, ForceKind::Friction);
                    worst = std::max(worst, std::abs(share));
                    patch.tangentImpulse[k] = share;
                }
            }
        }
        }
        // What is under each end decides what each body is standing on.
        //
        // A patch is one meeting, held by whichever of the two is the lower-numbered
        // awake body, so the one that is *on top* is usually not the one the patch is
        // filed under. It is still carried, by the same load at the same ends, and it
        // has to know it: a body that is supported and does not know it never comes to
        // rest, never sleeps, and -- because nothing in it is at rest -- goes on being
        // solved every tick for as long as the room is open. So a patch whose surface
        // faces up carries its owner, and one whose surface faces down is its owner
        // being pressed from above, which carries the other.
        for (std::size_t k = 0; k < count; ++k) {
            if (!(patch.normalImpulse[k] > 0.0)) continue;
            if (patch.normal.y > 0.5) {
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
            } else if (patch.normal.y < -0.5 && other != nullptr) {
                other->onBody = true;
                other->supportFrozen = body->frozen;
                other->supportBody = patch.body;
                other->supportLow = std::min(other->supportLow, patch.point[k].x);
                other->supportHigh = std::max(other->supportHigh, patch.point[k].x);
            }
        }
    };

    // A patch's load and its grip are swept one after the other and not together,
    // and the difference is the difference between a shape standing still and a shape
    // walking to one side.
    //
    // A sweep passes through states the room is never in. In the middle of one, a
    // body has been answered against the floor it stands on but not yet against what
    // stands on it, so it is falling and turning in a way nothing in the room is
    // doing -- and grip, which is read off the slip between two surfaces, reads the
    // transient and holds against it. Held with a force the shape never really felt,
    // the pair is pushed a little apart every tick: a slow walk, with nothing on
    // screen to explain it, which is exactly what it is.
    //
    // So the load is answered first, on its own, and to convergence: at the end of
    // that sweep the bodies are standing as they will stand. Then the grip is read
    // off that state, against the load that state carries, and laid down. Grip lands
    // a moment on a body that changes where it presses, so the load is asked again
    // after it, and the two are asked again until they agree. The sweep ends on the
    // load, so no tick finishes with a corner being driven into the floor.
    const auto sweep = [&](bool withGrip) {
        for (int pass = 0; pass < kSolvePasses; ++pass) {
            worst = 0.0;
            for (std::size_t m = 0; m < patches.count; ++m) patchPass(patches.items[m], withGrip);
            if (worst < kImpulseTolerance && pass > 0) break;
        }
    };
    for (int round = 0; round < kGripRoundsMax; ++round) {
        sweep(false);
        const double loadWorst = worst;
        sweep(true);
        const double gripWorst = worst;
        // Both answers have stopped moving: the load the room is carrying and the
        // grip holding it agree with each other, which is where the sweep is done.
        if (loadWorst < kAgree && gripWorst < kAgree) break;
    }
    sweep(false);
    return closingAtAll;
}

}  // namespace gtc::native
