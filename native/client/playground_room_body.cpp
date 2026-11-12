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
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
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
// How many times one tick may stop for a contact. A body landing on the floor and
// then tipping onto its other corner is two; a pair of shapes settling against
// each other is a handful. This is a guard against a pathological arrangement
// looping for ever, never a limit on what is answered: the walk leaves as soon as
// nothing is closing.
constexpr int kMaxWalkStops = 8;
// How many passes the contacts of one stop are solved in. Sequential impulses
// converge quickly for a handful of contacts; this is the point at which a solve
// that is still moving is taken to be a corner case, not a solve.
constexpr int kSolvePasses = 32;
constexpr double kSolveTolerance = 1e-12;  // m/s
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
        // The motion the body arrives with. Everything that acts on it over this
        // tick acts on *this*, not on the speed its own weight has just given it: the
        // weight of the tick is not air it has travelled through, and a shape standing
        // still must feel no air at all. Measuring the drag against the speed the
        // body had when the tick began is what makes that true.
        const Vec3 arriving = body.velocity;
        const double arrivingSpin = body.spin;
        const auto arrivingAt = [&](Vec3 at) {
            const Vec3 arm = at - body.centre;
            return Vec3{arriving.x - arrivingSpin * arm.y, arriving.y + arrivingSpin * arm.x, arriving.z};
        };
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 weight{0.0, -share * kGravity, 0.0};
            addForce(body.position[i], weight, ForceKind::Gravity);
            // The impulse the weight delivers over one tick, at that very corner.
            // Written out rather than short-circuited to g*dt: what a body feels is
            // a force at a place, and this is that force at that place.
            const Vec3 jog = weight * kTick;
            const Vec3 arm = body.position[i] - body.centre;
            body.velocity = body.velocity + jog * (1.0 / body.mass);
            body.spin += (arm.x * jog.y - arm.y * jog.x) / body.moment;
            // And the air it has to push through. A plate does not fall through air
            // as if there were none: F = half rho Cd A v^2, against the way it is
            // moving. Each corner carries the piece of the plate nearest it -- half
            // of each side that meets there, one metre deep -- and each piece is
            // moving with the body, turn included, so a spinning shape is slowed by
            // the air it turns through as well as by what it slides on.
            const Vec3 before = body.position[(i + body.count - 1) % body.count];
            const Vec3 after = body.position[(i + 1) % body.count];
            const double piece = 0.5 * (length(body.position[i] - before) + length(after - body.position[i]));
            const Vec3 speed = arrivingAt(body.position[i]);
            const double swift = length(speed);
            if (!(swift > 0.0) || !(piece > 0.0)) continue;
            const double drag = 0.5 * ball_shape::kAirDensity * kPlateDragCoefficient * piece * kRoomDepth *
                               swift * swift;  // N
            const Vec3 air = speed * (-drag / swift);
            addForce(body.position[i], air, ForceKind::Drag);
            const Vec3 puff = air * kTick;
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
    // The walk stops at an instant solved for to within a rounding of the step, so
    // the shapes are apart when they should be touching and never the other way
    // round. This is the check that keeps that true: if a step has left a corner
    // under the floor or two outlines into each other, the step is shortened until
    // it has not. It is not a correction -- nothing is moved back out. The step
    // that would have gone in is simply not taken.
    double used = seconds;
    for (int guard = 0; guard < 40; ++guard) {
        bool inside = false;
        for (std::size_t index = 0; index < kBodyCount && !inside; ++index) {
            const Body& body = bodies_[index];
            if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
            for (std::size_t i = 0; i < body.count; ++i)
                if (body.position[i].y < -kOnSurface) {
                    inside = true;
                }
            // The ball is a surface too, and one that cannot be travelled into: a
            // step that would put the outline into the ball is not taken. The
            // momentum the meeting carries is answered by the contact solve; this
            // is only about not walking through it while that answer settles.
            {
                Vec3 way{}, at{};
                if (bodyBallGap(index, way, at) < -kOnSurface) inside = true;
            }
            Vec3 way{};
            for (std::size_t other = index + 1; other < kBodyCount && !inside; ++other) {
                if (bodies_[other].count == 0) continue;
                double depth = 0.0;
                if (bodiesIntoEachOther(index, other, way, depth) && depth > kOnSurface) {
                    inside = true;
                }
            }
        }
        if (!inside) break;
        used *= 0.5;
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            bodies_[index].centre = centres[index];
            bodies_[index].angle = angles[index];
            placeCorners(bodies_[index]);
        }
        move(used);
    }
    return used;
}

// The first fraction of `remaining` at which anything would *arrive* at anything.
//
// Arriving is the whole of it: a corner already lying on the floor is not on its
// way to the floor, and stopping the walk at it every tick would eat the tick
// before it ever got spent. What counts is closing -- being driven into a surface
// it is already touching -- or reaching one it is clear of. That distinction is
// the difference between a walk and a stutter.
//
// The instant is solved for on the configuration that will actually happen, corner
// by corner: a corner on a turning body travels an arc, not a line, and taking the
// line through it is what leaves a shape a hair inside the floor. The gap at a
// given time is measured, the rate it is closing is measured, and the crossing is
// bisected to the width of the arithmetic.
double PlaygroundRoom::timeToContact(double remaining) const {
    // Where every body's corners would be after `time`: carried by its speed and
    // turned by its spin, with the corners read off that. A body that is asleep or
    // held is a surface that stays where it is.
    std::array<std::array<Vec3, kMaxCorners>, kBodyCount> corners{};
    const auto place = [&](double time) {
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            if (body.count == 0) continue;
            if (body.frozen || bodyHeld(index)) {
                corners[index] = body.position;
                continue;
            }
            const double angle = body.angle + body.spin * time;
            const Vec3 centre = body.centre + body.velocity * time;
            const double c = std::cos(angle), s = std::sin(angle);
            for (std::size_t i = 0; i < body.count; ++i) {
                const Vec3 local = body.local[i];
                corners[index][i] = Vec3{centre.x + local.x * c - local.y * s,
                                         centre.y + local.x * s + local.y * c, centre.z};
            }
        }
    };
    const auto moving = [&](std::size_t index) {
        const Body& body = bodies_[index];
        return body.count != 0 && !body.frozen && !bodyHeld(index);
    };
    // The gap between a body and the ball, at the corners it has now.
    const auto ballGap = [&](std::size_t index) {
        return ball_shape::outlineGap(ball_.position, corners[index].data(), corners[index].data(),
                                      bodies_[index].count, 1.0)
                   .gap -
               ball_.radius;
    };
    const auto bodyGap = [&](std::size_t one, std::size_t two) {
        return pairGap(corners[one].data(), bodies_[one].count, corners[two].data(), bodies_[two].count).gap;
    };
    // Which meetings are worth watching: the ones that are not already touching.
    // A meeting already in the contact band is answered by the patch, not by the
    // walk.
    bool watchCorner[kBodyCount][kMaxCorners] = {};
    bool watchBall[kBodyCount] = {};
    bool watchPair[kBodyCount][kBodyCount] = {};
    place(0.0);
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        if (!moving(index)) continue;
        for (std::size_t i = 0; i < bodies_[index].count; ++i)
            watchCorner[index][i] = corners[index][i].y > kContactBand;
        watchBall[index] = ballGap(index) > kContactBand;
        for (std::size_t other = 0; other < kBodyCount; ++other) {
            if (other == index || bodies_[other].count == 0) continue;
            watchPair[index][other] = bodyGap(index, other) > kContactBand;
        }
    }
    // The smallest gap anywhere, over the watched meetings, at `time`.
    const auto gapAfter = [&](double time) {
        place(time);
        double least = 1e30;
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            if (!moving(index)) continue;
            for (std::size_t i = 0; i < bodies_[index].count; ++i)
                if (watchCorner[index][i]) least = std::min(least, corners[index][i].y);
            if (watchBall[index]) least = std::min(least, ballGap(index));
        }
        for (std::size_t one = 0; one < kBodyCount; ++one) {
            if (!moving(one)) continue;
            for (std::size_t two = 0; two < kBodyCount; ++two)
                if (watchPair[one][two]) least = std::min(least, bodyGap(one, two));
        }
        return least;
    };
    // Nothing comes within touch over the whole of what is left: the tick is travel.
    if (gapAfter(remaining) > 0.0) return remaining;
    // Something does. Walk forward to the instant: how fast the gap is closing is
    // measured rather than assumed, so the step to the surface is the distance over
    // the rate, and the crossing is then bisected.
    double time = 0.0;
    double gap = gapAfter(0.0);
    for (int step = 0; step < 32 && time < remaining; ++step) {
        if (!(gap > 0.0)) break;
        const double probe = 1e-6;
        const double closing = (gap - gapAfter(time + probe)) / probe;
        if (!(closing > 1e-12)) return remaining;
        const double arrive = std::min(remaining, time + gap / closing);
        if (gapAfter(arrive) > 0.0) {
            time = arrive;
            gap = gapAfter(time);
            continue;
        }
        double low = time, high = arrive;
        for (int i = 0; i < 60; ++i) {
            const double middle = 0.5 * (low + high);
            if (gapAfter(middle) > 0.0) {
                low = middle;
            } else {
                high = middle;
            }
        }
        return high;
    }
    return time;
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
void PlaygroundRoom::gatherContacts(ManifoldSet& patches) const {
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
            // The two lowest: a convex body stands on a face, and more than two
            // corners on one plane is the same face seen with a collinear corner.
            // Both of them are taken whether or not the second one has arrived: a
            // face is either on the floor or it is not, and once it is, both of its
            // ends are answered -- the one that is down against the floor it is on,
            // and the one still in the air against the distance it has left to fall.
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
                    patch.gap[k] = std::max(0.0, height);
                    break;
                }
            }
        }
        while (patch.ends < kMaxPatchEnds && lowest[patch.ends] < 1e29) ++patch.ends;
        // A face whose ends are all still in the air is a body falling, and a body
        // falling is the walk's business: it is not answered here, where all it
        // would get is a stop a tick early.
        if (patch.ends > 0 && patch.gap[0] <= kContactBand) patches.add(patch);
    }
    // The ball, met at the one place a body's outline comes closest to it.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        Vec3 way{0, 1, 0}, at{};
        const double gap = bodyBallGap(index, way, at);
        if (gap > kContactBand) continue;
        Manifold patch;
        patch.body = index;
        patch.ball = true;
        patch.ends = 1;
        patch.point[0] = at;
        patch.gap[0] = std::max(0.0, gap);
        patch.normal = way;  // out of the ball, into the body
        patch.kind = ForceKind::Ball;
        patches.add(patch);
    }
    // Another body, met by the corners of this one against its outline. The outline
    // of a convex face is a segment too, so the corners of this body that are on it
    // are the ends of one load as well.
    for (std::size_t one = 0; one < kBodyCount; ++one) {
        const Body& body = bodies_[one];
        if (body.count == 0 || body.frozen || bodyHeld(one)) continue;
        for (std::size_t two = 0; two < kBodyCount; ++two) {
            if (two == one || bodies_[two].count == 0) continue;
            const Body& solid = bodies_[two];
            Manifold patch;
            patch.body = one;
            patch.other = two;
            patch.kind = ForceKind::Pair;
            double nearest[kMaxPatchEnds] = {1e30, 1e30};
            Vec3 way[kMaxPatchEnds];
            for (std::size_t i = 0; i < body.count; ++i) {
                const ball_shape::OutlineGap gap = ball_shape::outlineGap(
                    body.position[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
                for (std::size_t k = 0; k < kMaxPatchEnds; ++k) {
                    if (gap.gap < nearest[k]) {
                        for (std::size_t j = kMaxPatchEnds - 1; j > k; --j) {
                            nearest[j] = nearest[j - 1];
                            patch.corner[j] = patch.corner[j - 1];
                            patch.point[j] = patch.point[j - 1];
                            patch.gap[j] = patch.gap[j - 1];
                            way[j] = way[j - 1];
                        }
                        nearest[k] = gap.gap;
                        patch.corner[k] = i;
                        patch.point[k] = body.position[i];
                        patch.gap[k] = std::max(0.0, gap.gap);
                        way[k] = gap.normal;  // out of the other body, into this one
                        break;
                    }
                }
            }
            // The way out of the body this one is meeting is the way at the place
            // they are nearest, which is where the load is.
            patch.normal = way[0];
            while (patch.ends < kMaxPatchEnds && nearest[patch.ends] < 1e29) ++patch.ends;
            if (patch.ends > 0 && patch.gap[0] <= kContactBand) patches.add(patch);
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
        const bool otherFixed = other != nullptr && bodyHeld(patch.other);
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
                otherSpeed = ballPointSpeed(at);
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
                pushBall(at, impulse, kind);
            }
        };
        const auto pushEnd = [&](std::size_t k, Vec3 impulse, ForceKind kind) {
            if (impulse.x == 0.0 && impulse.y == 0.0 && impulse.z == 0.0) return;
            pushBody(*body, patch.point[k], impulse, kind);
            pushOther(patch.point[k], impulse * -1.0, kind);
        };
        // What the ends were closing at before this answer, for the walk.
        for (std::size_t k = 0; k < count; ++k) {
            const double closing = away(patch.point[k], patch.normal);
            if (closing < -kRestSpeed) closingAtAll = true;
            worst = std::max(worst, std::abs(closing));
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
            for (std::size_t k = 0; k < count; ++k) {
                if (!live[k]) continue;
                order[dofs] = k;
                // What the end has to do to be on the surface when the tick ends: it
                // may close the distance it has left, and not one micron more. An end
                // already on the surface has nothing left to close and is simply
                // stopped there; an end a hair above it, with the face tipping onto
                // it, is left free to come down onto it in this very tick instead of
                // being held off it and rocking.
                u[dofs] = away(patch.point[k], patch.normal) + patch.gap[k] / kTick;
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
                if (carried[k] + delta[i] > 0.0) continue;
                // A pull: the floor lets go here. What this end took is given back
                // and the rest of the patch is answered again without it.
                pushEnd(k, patch.normal * -carried[k], patch.kind);
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
                carried[k] += delta[i];
                patch.normalImpulse[k] = carried[k];
            }
            break;
        }
        // The grip, end by end, against the load that end is carrying.
        //
        // Coulomb, in the two forms it has, and it is the same law in both: the
        // surface holds along itself with whatever it takes to leave the face still
        // against it, up to what the load under it can hold. The coefficient is what
        // differs. An end that is already slipping is held by the sliding
        // coefficient, and it is held by that much *only against the slip* -- a
        // surface can stop a slide and can never drive one backwards, so an end that
        // is moving is never given more than it takes to bring it to rest.
        const double staticMu =
            other != nullptr ? 0.5 * (body->staticFriction + other->staticFriction)
            : patch.ball   ? std::min(body->staticFriction, ball_shape::kBallStaticFriction)
                           : body->staticFriction;
        const double kineticMu =
            other != nullptr ? 0.5 * (body->kineticFriction + other->kineticFriction)
            : patch.ball   ? std::min(body->kineticFriction, ball_shape::kBallKineticFriction)
                           : body->kineticFriction;
        for (std::size_t k = 0; k < count; ++k) {
            if (!(patch.normalImpulse[k] > 0.0)) {
                // No pressure, nothing to grip with.
                if (patch.tangentImpulse[k] != 0.0) {
                    pushEnd(k, tangent * -patch.tangentImpulse[k], ForceKind::Friction);
                    patch.tangentImpulse[k] = 0.0;
                }
                continue;
            }
            const double sliding = away(patch.point[k], tangent);
            const double giveT = give(patch.point[k], tangent, patch.point[k], tangent);
            if (!(giveT > 1e-12)) continue;
            const double mu = std::abs(sliding) > kRestSpeed ? kineticMu : staticMu;
            const double hold = mu * patch.normalImpulse[k];
            double grip = patch.tangentImpulse[k] - sliding / giveT;
            if (std::abs(grip) > hold) {
                // More than the surface can hold: it slips, and what it is given is
                // what the surface can give -- the rest of the slip is left in the
                // body's motion, where it belongs.
                grip = std::copysign(hold, grip);
            }
            const double delta = grip - patch.tangentImpulse[k];
            if (delta == 0.0) continue;
            pushEnd(k, tangent * delta, ForceKind::Friction);
            patch.tangentImpulse[k] = grip;
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
            }
            body->supportLow = std::min(body->supportLow, patch.point[k].x);
            body->supportHigh = std::max(body->supportHigh, patch.point[k].x);
        }
    };

    for (int pass = 0; pass < kSolvePasses; ++pass) {
        worst = 0.0;
        for (std::size_t m = 0; m < patches.count; ++m) patchPass(patches.items[m]);
        if (worst < kSolveTolerance && pass > 0) break;
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
        const bool carriedByBody = body.onBody && body.supportFrozen &&
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
        body.restTicks = 0;
        body.restLow = body.restHigh = body.centre;
        body.restAngleLow = body.restAngleHigh = body.angle;
    }
}

// One tick of every body. The forces first, then the walk.
//
// The walk stops at every arrival -- the first instant anything would meet
// anything else -- and answers it there before spending what is left of the tick.
// An arrival at the very last instant of the tick is answered too: a body that
// lands as the tick ends would otherwise begin the next one already closing on the
// surface it is standing on, and be answered a tick late, from inside.
void PlaygroundRoom::tickBodies() {
    wakeUnsupportedBodies();
    applyBodyForces();
    // One set of patches for the whole tick, carried through the walk. The patches
    // that are there when the tick starts are answered first -- the weight of a shape
    // standing on the floor arrives as one tick's worth of impulse, and a patch that
    // answers none of it leaves the shape moving through a tick it should be standing
    // still through. An arrival later in the tick joins the same set, keeping what
    // the ends it shares already carry, so the tick converges on one answer for each
    // meeting instead of answering the same one again.
    ManifoldSet patches;
    gatherContacts(patches);
    solveContacts(patches);
    double remaining = kTick;
    for (int stop = 0; stop < kMaxWalkStops; ++stop) {
        const double step = timeToContact(remaining);
        const double used = step > 0.0 ? advanceBodies(step) : 0.0;
        remaining -= used;
        // A meeting at the very last instant of the tick is left to the next tick,
        // which begins by answering every patch the bodies are touching at -- that
        // is the same meeting, seen with a whole tick to spend on it.
        if (!(remaining > 0.0)) break;
        gatherContacts(patches);
        if (!solveContacts(patches)) {
            // Nothing new is touching and nothing already touching is closing: what
            // is left of the tick is travel along the way the bodies are going.
            remaining -= advanceBodies(remaining);
            break;
        }
    }
    settleBodies();
    writeImpulses();
}

}  // namespace gtc::native
