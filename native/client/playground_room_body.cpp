// One tick of every solid in the room.
//
// A body is a rigid body: a mass, a moment of inertia, a place, a turn, a speed
// and a spin. Nothing here moves a corner. A force is applied at a place, the
// impulse it delivers over the tick is turned into a change of the body's speed
// and of its turn, and the corners are then read off the shape.
//
// The tick is walked, not sampled. Every instant at which anything would begin to
// touch anything else is solved for -- a corner reaching the floor, a corner
// reaching another body's outline, an outline reaching the ball -- and the bodies
// are carried exactly that far, the contact is answered with an impulse at the
// place it happens, and the walk goes on with what is left of the tick. That is
// what makes a fast body stop at the surface it hits instead of being found
// inside it afterwards, and it is why there is no position correction anywhere in
// this file: nothing ever gets in.

#include "client/playground_ball_shape.h"
#include "client/playground_pair.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace gtc::native {
using namespace playground_detail;
using namespace playground_tick;

namespace {
// How close to a surface counts as being on it. A micrometre: the walk stops at
// the touching instant to an arithmetic fraction of a step, and this is the width
// of the band in which the two are the same place.
constexpr double kOnSurface = 1e-6;
// How near a surface counts as resting on it. A shape lying flat on the floor and
// a shape balanced on one corner of it differ by the floor's own flatness -- at the
// scale of this room, a few microns -- and a contact set that sees only the corner
// because the face is a hundredth of a millimetre up is a contact set that lets the
// shape rock. This is the band in which two surfaces are touching.
constexpr double kContactBand = 1e-4;
// How many sweeps of the patches one tick is solved in. Sequential impulses
// converge quickly for a handful of contacts; this is the point at which a solve
// that is still moving is taken to be a corner case, not a solve.
constexpr int kSolvePasses = 32;
// How much of an interpenetration is paid off in one tick, as a fraction. A gap that
// is already inside something is not motion; it is a state the physics never put the
// room in, and giving all of it back in one tick would be a separating speed neither
// body was given. A fifth a tick is the usual positional-bias rule, and it is gone in
// a quarter of a second without anything being launched.
constexpr double kRecovery = 0.2;
// A sweep of the patches is over when nothing it can still see is closing faster
// than this. A millionth of a metre per second is a thousandth of a millimetre in
// a whole tick: slower than the arithmetic of the positions can even show, and
// small enough that what is left over cannot pump a stacked pair.
constexpr double kSolveTolerance = 1e-12;  // m/s
// A sweep of the patches is over when it has stopped changing the answer. Every
// impulse it applies is a change, so this is measured in newton seconds: a thousandth
// of a micronewton second is nothing next to a corner's weight over a tick (half a
// newton second), and it is far smaller than anything the eye or the integrator can
// see. It is a separate number from the one above because it answers a separate
// question: the sweep now stops when the *answer* has stopped moving rather than when
// the surfaces have stopped closing.
constexpr double kImpulseTolerance = 1e-12;  // N s
// How long a body's pose has to stay inside one small box before it is at rest,
// and how small that box is. A shape standing on its supports does not go
// anywhere; the last thing it does is settle, and settling is measured, not
// declared. The box is the width of the contact band over a second -- a millimetre
// a tick -- which is far below anything that can be seen at the scale of a body.
constexpr int kRestTicks = 20;
constexpr double kRestBox = 6e-3;      // m
constexpr double kRestTurn = 1.2e-2;   // rad
}  // namespace

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
        body.velocity = body.velocity + jog * (1.0 / body.mass);
        body.spin += (arm.x * jog.y - arm.y * jog.x) / body.moment;
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
            // Only a face the air is being driven into is pressed; the one behind is
            // in the shape's shadow and the air there is not pushing it anywhere.
            if (!(vn < 0.0)) continue;
            const double press = 0.5 * ball_shape::kAirDensity * kPlateDragCoefficient * span * kRoomDepth * vn * vn;
            const Vec3 air = out * press;
            addForce(at, air, ForceKind::Drag, nullptr, index);
            const Vec3 puff = air * kTick;
            const Vec3 arm = at - body.centre;
            body.velocity = body.velocity + puff * (1.0 / body.mass);
            body.spin += (arm.x * puff.y - arm.y * puff.x) / body.moment;
        }
    }
}

// Carries every simulated body by `seconds` of its own speed and turn. The only
// place a body's place or turn is ever changed by the simulation.
double PlaygroundRoom::advanceBodies(double seconds) {
    if (!(seconds > 0.0)) return 0.0;
    const auto move = [&](double time) {
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            Body& body = bodies_[index];
            if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
            body.centre = body.centre + body.velocity * time;
            body.angle += body.spin * time;
            placeCorners(body);
        }
    };
    std::array<Vec3, kBodyCount> centres{};
    std::array<double, kBodyCount> angles{};
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        centres[index] = bodies_[index].centre;
        angles[index] = bodies_[index].angle;
    }
    move(seconds);
    return seconds;
}

// Every patch at the configuration the walk has stopped at.
//
// A body meets the floor with the face it is standing on, and a convex face on a
// plane touches along a segment: the two lowest corners of the body are that
// segment's ends. They are gathered as one patch, not as separate contacts,
// because the load between them is one distributed load -- and a load that can sit
// anywhere between two ends is the reason a body can stand on the floor at all.
// Both ends are gathered as soon as one of them is down, and each is answered
// against the distance it still has to fall, so a face that is a hair off the floor
// at one end is a face landing on it, not a face held up at one corner.
// A ball touches at one point and nowhere else, so its patch has one end.
void PlaygroundRoom::gatherContacts(ManifoldSet& patches, double band) const {
    patches.clear();
    // The floor, met by the lowest corners of each body.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        Manifold patch;
        patch.body = index;
        patch.other = kBodyCount;
        patch.normal = Vec3{0, 1, 0};
        double lowest[kMaxPatchEnds] = {1e30, 1e30};
        for (std::size_t i = 0; i < body.count; ++i) {
            const double height = body.position[i].y;
            // The corners that are *on* the floor, nearest first. A corner half a
            // metre up is not an end of a contact patch, it is a corner in the air,
            // and taking it as one asks the floor to hold the face by a corner it is
            // not touching -- which it cannot do without pulling, so it lets go.
            if (height > band) continue;
            for (std::size_t k = 0; k < kMaxPatchEnds; ++k) {
                if (height < lowest[k]) {
                    for (std::size_t j = kMaxPatchEnds - 1; j > k; --j) {
                        lowest[j] = lowest[j - 1];
                        patch.corner[j] = patch.corner[j - 1];
                        patch.point[j] = patch.point[j - 1];
                        patch.gap[j] = patch.gap[j - 1];
                    }
                    lowest[k] = height;
                    patch.corner[k] = i;
                    patch.point[k] = body.position[i];
                    patch.gap[k] = height;
                    break;
                }
            }
        }
        while (patch.ends < kMaxPatchEnds && lowest[patch.ends] < 1e29) ++patch.ends;
        // A face whose ends are all still in the air is a body falling, and a body
        // falling is the walk's business: it is not answered here, where all it
        // would get is a stop a tick early.
        if (patch.ends > 0 && patch.gap[0] <= band) patches.add(patch);
    }
    // The ball, met at the one place a body's outline comes closest to it.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        Vec3 way{0, 1, 0}, at{};
        const double gap = bodyBallGap(index, way, at);
        if (gap > band) continue;
        Manifold patch;
        patch.body = index;
        patch.ball = true;
        patch.ends = 1;
        patch.point[0] = at;
        patch.gap[0] = gap;
        patch.normal = way;  // out of the ball, into the body
        patch.kind = ForceKind::Ball;
        patches.add(patch);
    }
    // Another shape, met over the whole span the two of them share. The separating
    // axis gives the way the load can go and the span of the face lying on the other
    // one gives where it acts; each end of that span carries its own distance, so an
    // end that is still in the air is not answered as though it were down.
    for (std::size_t one = 0; one < kBodyCount; ++one) {
        const Body& body = bodies_[one];
        if (body.count == 0 || body.frozen || bodyHeld(one)) continue;
        for (std::size_t two = 0; two < kBodyCount; ++two) {
            if (two == one || bodies_[two].count == 0) continue;
            // One meeting, one patch. Two shapes touching are one load, not two: if
            // each of them gathered its own patch against the other, the pair would be
            // answered twice, in opposite directions, from two different ends -- and a
            // load that doubles itself every sweep is a shape that rocks for ever on
            // nothing at all. The lower-numbered awake body carries the meeting, and a
            // body asleep can only be the surface in someone else's patch, which is
            // why the ordering is skipped when the other one is not awake.
            if (two < one && !bodies_[two].frozen) continue;
            const Body& solid = bodies_[two];
            pair_shape::PairManifold meeting;
            if (!pair_shape::collide(body.position.data(), body.count, solid.position.data(), solid.count,
                                     band, meeting))
                continue;
            Manifold patch;
            patch.body = one;
            patch.other = two;
            patch.kind = ForceKind::Pair;
            patch.normal = meeting.normal;
            for (std::size_t k = 0; k < meeting.ends && k < kMaxPatchEnds; ++k) {
                patch.corner[k] = kMaxCorners;  // a place on a face, not a corner
                patch.point[k] = meeting.at[k];
                // Signed, like every other gap: inside is negative, and what is left
                // of it after the tick is the position pass's business.
                patch.gap[k] = -meeting.depth[k];
                patch.normalImpulse[k] = 0.0;
                patch.tangentImpulse[k] = 0.0;
            }
            patch.ends = meeting.ends;
            patches.add(patch);
        }
    }
}

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
            bool onTheSurface = false;
            for (std::size_t k = 0; k < count; ++k)
                if (live[k] && patch.gap[k] <= kContactBand) onTheSurface = true;
            for (std::size_t k = 0; k < count; ++k) {
                if (!live[k]) continue;
                order[dofs] = k;
                const double bias = (onTheSurface && patch.gap[k] <= kContactBand)
                                        ? 0.0
                                        : (patch.gap[k] > 0.0 ? patch.gap[k] * (1.0 / kTick) : 0.0);
                u[dofs] = away(patch.point[k], patch.normal) + bias;
                for (std::size_t j = 0; j <= dofs; ++j)
                    g[j][dofs] = give(patch.point[order[j]], patch.normal, patch.point[k], patch.normal);
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
                    for (std::size_t j = 0; j <= dofs; ++j)
                        g[j][dofs] = give(patch.point[order[j]], tangent, patch.point[k], tangent);
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
        for (std::size_t m = 0; m < patches.count; ++m) {
            const std::size_t one = patches.items[m].body;
            const std::size_t two = patches.items[m].other;
            patchPass(patches.items[m]);
        }
        if (worst < kImpulseTolerance && pass > 0) break;
    }
    return closingAtAll;
}

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
        const bool carriedByPlane = body.touchingPlane && body.centre.x >= body.supportLow - kContactBand &&
                                    body.centre.x <= body.supportHigh + kContactBand;
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
        const bool carried = carriedByPlane || carriedByBody;
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
        // Asleep, or not yet: a body that has been inside one small box for a
        // second is standing still on what is holding it up, and a body that has
        // been anywhere is not, and starts its second again.
        if (body.restTicks >= kRestTicks && anywhere) {
            // Asleep where the physics left it. Nothing is moved to make it look
            // tidier: a body that is standing still is standing where it stands.
            body.frozen = true;
            body.velocity = {};
            body.spin = 0.0;
            body.shown = body.position;
        } else if (body.restTicks >= kRestTicks && !anywhere) {
            body.restTicks = 0;
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
        if (!touching && bodyBallGap(index, way, at) <= kContactBand) touching = true;
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
// This is the plainest thing a contact solver can do and the reason to do it is that
// it cannot lie about motion: a body's speed is what moves it, every tick, and there
// is no state in which a shape is held still while its speed keeps growing. That
// state is what a walk that refuses steps produces, and it is a body with a weight
// that never lands and a drag arrow from a speed it does not have.
void PlaygroundRoom::tickBodies() {
    // What every body arrived with, and how many contacts it had already been given.
    //
    // A body that spends a whole tick in free flight has followed a parabola, and a
    // parabola can be drawn exactly. One burst of speed at the top of the tick and a
    // whole tick's travel at that speed does not: it lands the body half a step
    // further along than the path went, every tick, in the direction of the weight --
    // energy out of the arithmetic and out of nothing else. The exact rule is the
    // trapezoid, and it is only a move: half of the speed the forces changed, times
    // the tick. Nothing is added to any speed, so it cannot speed a body up. A body
    // that was given contacts is left out of it, because its travel is the solver's
    // answer and not a parabola's.
    std::array<Vec3, kBodyCount> arrival{};
    std::array<int, kBodyCount> metBefore{};
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        arrival[index] = bodies_[index].velocity;
        metBefore[index] = bodies_[index].met;
    }
    wakeUnsupportedBodies();
    // The air first, on the motion the body arrived with -- the speed its own weight
    // has given it this tick is not air it has travelled through, and a shape standing
    // still must feel none of it.
    applyAirDrag();
    applyBodyForces();
    // How far anything can get before this tick is over. Every end within that is a
    // contact *this tick*: it could arrive before the tick ends, so it is answered
    // now, against the distance it has left.
    double reach = kContactBand;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0) continue;
        // A sleeping body is still a surface something can arrive at, so its absence
        // of speed is not what the band is measured by -- but neither does it need to
        // be, because what arrives at it has its own speed and its own band.
        double furthest = 0.0;
        for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
        reach = std::max(reach, (length(body.velocity) + std::abs(body.spin) * furthest) * kTick);
    }
    reach = std::max(reach, length(ball_.velocity) * kTick);
    const double band = reach;
    // A load that arrives before this tick ends is a load arriving, so the wake is
    // asked with the same reach the patches are gathered with.
    wakeLoadedBodies(band);
    ManifoldSet patches;
    gatherContacts(patches, band);
    solveContacts(patches);
    advanceBodies(kTick);
    // The parabola, finished, for the bodies that were free to follow one.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index) || body.met != metBefore[index]) continue;
        body.centre = body.centre + (arrival[index] - body.velocity) * (0.5 * kTick);
        placeCorners(body);
    }
    // Whatever a spinning corner carried into a surface within the tick -- the one
    // thing a single straight step cannot see -- is put back out, by position alone.
    // It is the only thing in the room that moves a shape without an impulse, and
    // with the contacts answered against their own distances there is normally
    // nothing left for it to do.
    separateBodies();
    settleBodies();
    writeImpulses();
}


// Was the frozen body under `index` ever given this body's load?
//
// The forces a sleeping body replays are the ones the solver last left it with, so if
// it is really carrying something, the thing it carries is in that list: an impulse at
// the place the two meet, of the pair kind. If nothing of the sort is there, the pair
// was never solved with both sides, which means the shape above is leaning on a wall
// that no longer exists as a physical thing -- it is only frozen.
bool PlaygroundRoom::carriedBySolvedSupport(std::size_t index) const {
    const Body& body = bodies_[index];
    if (body.count == 0) return false;
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index) continue;
        const Body& support = bodies_[other];
        if (!support.frozen || support.count == 0) continue;
        pair_shape::PairManifold meeting;
        if (!pair_shape::collide(body.position.data(), body.count, support.position.data(), support.count,
                                 kContactBand, meeting))
            continue;
        for (std::size_t i = 0; i < support.appliedCount; ++i) {
            const Body::Applied& applied = support.applied[i];
            if (applied.kind != ForceKind::Pair) continue;
            for (std::size_t k = 0; k < meeting.ends; ++k)
                if (length(Vec3{applied.at.x - meeting.at[k].x, applied.at.y - meeting.at[k].y, 0}) < 0.05)
                    return true;
        }
    }
    return false;
}

}  // namespace gtc::native
