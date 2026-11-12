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
#include <cstdio>
#include <cstdlib>

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
// plane touches along a segment: the corners of the body that are on the plane are
// that segment's ends. They are gathered as one patch, not as separate contacts,
// because the load between them is one distributed load -- and a load that can sit
// anywhere between two ends is the reason a body can stand on the floor at all.
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
            if (height > kContactBand) continue;
            // The two lowest: a convex body stands on a face, and more than two
            // corners on one plane is the same face seen with a collinear corner.
            for (std::size_t k = 0; k < kMaxPatchEnds; ++k) {
                if (height < lowest[k]) {
                    for (std::size_t j = kMaxPatchEnds - 1; j > k; --j) {
                        lowest[j] = lowest[j - 1];
                        patch.corner[j] = patch.corner[j - 1];
                        patch.point[j] = patch.point[j - 1];
                    }
                    lowest[k] = height;
                    patch.corner[k] = i;
                    patch.point[k] = body.position[i];
                    break;
                }
            }
        }
        while (patch.ends < kMaxPatchEnds && lowest[patch.ends] < 1e29) ++patch.ends;
        if (patch.ends > 0) patches.add(patch);
    }
    // The ball, met at the one place a body's outline comes closest to it.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        Vec3 way{0, 1, 0}, at{};
        if (bodyBallGap(index, way, at) > kContactBand) continue;
        Manifold patch;
        patch.body = index;
        patch.ball = true;
        patch.ends = 1;
        patch.point[0] = at;
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
            bool haveNormal = false;
            for (std::size_t i = 0; i < body.count; ++i) {
                const ball_shape::OutlineGap gap = ball_shape::outlineGap(
                    body.position[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
                if (gap.gap > kContactBand) continue;
                if (!haveNormal) {
                    patch.normal = gap.normal;  // out of the other body, into this one
                    haveNormal = true;
                }
                for (std::size_t k = 0; k < kMaxPatchEnds; ++k) {
                    if (gap.gap < nearest[k]) {
                        for (std::size_t j = kMaxPatchEnds - 1; j > k; --j) {
                            nearest[j] = nearest[j - 1];
                            patch.corner[j] = patch.corner[j - 1];
                            patch.point[j] = patch.point[j - 1];
                        }
                        nearest[k] = gap.gap;
                        patch.corner[k] = i;
                        patch.point[k] = body.position[i];
                        break;
                    }
                }
            }
            while (patch.ends < kMaxPatchEnds && nearest[patch.ends] < 1e29) ++patch.ends;
            if (patch.ends > 0) patches.add(patch);
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

    // The shape of one patch: the body, what is under it, and the pair of ends.
    struct Ends {
        Body* body = nullptr;
        Body* other = nullptr;
        double ballMoment = 0.0;
        bool otherFixed = false;
    };
    const auto endsOf = [&](const Manifold& patch) {
        Ends ends;
        ends.body = &bodies_[patch.body];
        ends.other = (!patch.ball && patch.other < kBodyCount) ? &bodies_[patch.other] : nullptr;
        ends.otherFixed = ends.other != nullptr && bodyHeld(patch.other);
        ends.ballMoment = 0.4 * ball_.mass * ball_.radius * ball_.radius;
        return ends;
    };
    // Every body in the patch contributes the give of its own share of the shape:
    // the reciprocal of its mass, plus the two levers about its centre.
    const auto give = [&](const Manifold& patch, const Ends& ends, Vec3 at, Vec3 toward, Vec3 pushAt,
                          Vec3 direction) {
        const auto lever = [](Vec3 arm, Vec3 axis) { return arm.x * axis.y - arm.y * axis.x; };
        double value = 1.0 / ends.body->mass +
                       lever(at - ends.body->centre, toward) * lever(pushAt - ends.body->centre, direction) /
                           ends.body->moment;
        if (ends.other != nullptr) {
            if (!ends.otherFixed) {
                value += 1.0 / ends.other->mass +
                         lever(at - ends.other->centre, toward) *
                             lever(pushAt - ends.other->centre, direction) / ends.other->moment;
            }
        } else if (patch.ball) {
            value += 1.0 / ball_.mass +
                     lever(at - ball_.position, toward) * lever(pushAt - ball_.position, direction) /
                         ends.ballMoment;
        }
        return value;
    };
    const auto otherSpeed = [&](const Manifold& patch, const Ends& ends, Vec3 at) {
        if (ends.other != nullptr) return ends.otherFixed ? Vec3{} : pointSpeed(*ends.other, at);
        if (patch.ball) return ballPointSpeed(at);
        return Vec3{};
    };
    const auto pushOther = [&](const Manifold& patch, const Ends& ends, Vec3 at, Vec3 impulse) {
        if (ends.other != nullptr) {
            if (!ends.otherFixed) pushBody(*ends.other, at, impulse, patch.kind);
        } else if (patch.ball) {
            pushBall(at, impulse, patch.kind);
        }
    };
    const auto relativeSpeed = [&](const Manifold& patch, const Ends& ends, Vec3 at) {
        return pointSpeed(*ends.body, at) - otherSpeed(patch, ends, at);
    };
    const auto normalPass = [&](Manifold& patch) {
        const Ends ends = endsOf(patch);
        // Each end of the patch, in turn: the impulse that stops it closing, kept
        // as what it has carried so far and never allowed to become a pull. The
        // sweep is repeated, so an impulse given at one end -- which turns the body
        // and so moves the other end -- is answered at the other end on the next
        // pass, and the patch settles on one load instead of two separate ones.
        for (std::size_t k = 0; k < patch.ends; ++k) {
            const double closing = dot(relativeSpeed(patch, ends, patch.point[k]), patch.normal);
            if (closing < -kRestSpeed) closingAtAll = true;
            worst = std::max(worst, std::abs(closing));
            const double compliance = give(patch, ends, patch.point[k], patch.normal, patch.point[k], patch.normal);
            if (!(compliance > 1e-12)) continue;
            double carried = patch.normalImpulse[k] - closing / compliance;
            if (!(carried > 0.0)) carried = 0.0;
            const double delta = carried - patch.normalImpulse[k];
            if (delta == 0.0) continue;
            patch.normalImpulse[k] = carried;
            pushBody(*ends.body, patch.point[k], patch.normal * delta, patch.kind);
            pushOther(patch, ends, patch.point[k], patch.normal * -delta);
        }
    };
    const auto gripPass = [&](Manifold& patch) {
        const Ends ends = endsOf(patch);
        const Vec3 tangent{patch.normal.y, -patch.normal.x, 0.0};
        for (std::size_t k = 0; k < patch.ends; ++k) {
            // What is under this end decides what the body is standing on.
            if (patch.normalImpulse[k] > 0.0 && patch.normal.y > 0.5) {
                ++ends.body->contacts;
                if (ends.other == nullptr && !patch.ball) {
                    ends.body->touchingPlane = true;
                } else if (ends.other != nullptr) {
                    ends.body->onBody = true;
                    ends.body->supportFrozen = ends.other->frozen;
                }
                ends.body->supportLow = std::min(ends.body->supportLow, patch.point[k].x);
                ends.body->supportHigh = std::max(ends.body->supportHigh, patch.point[k].x);
            }
            // Coulomb's rule, at this end, on what this end carries: mu_s holds the
            // slide while the grip can carry what holding it would take, and once it
            // cannot, mu_k carries what it can and the rest slips.
            const auto release = [&]() {
                if (patch.tangentImpulse[k] == 0.0) return;
                pushBody(*ends.body, patch.point[k], tangent * -patch.tangentImpulse[k], patch.kind);
                pushOther(patch, ends, patch.point[k], tangent * patch.tangentImpulse[k]);
                patch.tangentImpulse[k] = 0.0;
            };
            if (!(patch.normalImpulse[k] > 0.0)) {
                release();
                continue;
            }
            const double compliance = give(patch, ends, patch.point[k], tangent, patch.point[k], tangent);
            if (!(compliance > 1e-12)) continue;
            const double slide = dot(relativeSpeed(patch, ends, patch.point[k]), tangent);
            double muStatic = ends.body->staticFriction, muKinetic = ends.body->kineticFriction;
            if (ends.other != nullptr) {
                muStatic = 0.5 * (muStatic + ends.other->staticFriction);
                muKinetic = 0.5 * (muKinetic + ends.other->kineticFriction);
            } else if (patch.ball) {
                muStatic = std::min(muStatic, ball_shape::kBallStaticFriction);
                muKinetic = std::min(muKinetic, ball_shape::kBallKineticFriction);
            }
            double carried = patch.tangentImpulse[k] - slide / compliance;
            const double hold = muStatic * patch.normalImpulse[k];
            if (std::abs(carried) > hold) {
                const double slip = muKinetic * patch.normalImpulse[k];
                carried = std::copysign(std::min(std::abs(carried), slip), carried);
            }
            const double delta = carried - patch.tangentImpulse[k];
            if (delta == 0.0) continue;
            patch.tangentImpulse[k] = carried;
            pushBody(*ends.body, patch.point[k], tangent * delta, patch.kind);
            pushOther(patch, ends, patch.point[k], tangent * -delta);
        }
    };

    for (int pass = 0; pass < kSolvePasses; ++pass) {
        worst = 0.0;
        for (std::size_t m = 0; m < patches.count; ++m) normalPass(patches.items[m]);
        for (std::size_t m = 0; m < patches.count; ++m) gripPass(patches.items[m]);
        if (worst < kSolveTolerance && pass > 0) break;
    }
    // The normal has the last word: whatever the grip did to the way the shape is
    // sitting, nothing is left being driven into a surface.
    for (std::size_t m = 0; m < patches.count; ++m) normalPass(patches.items[m]);
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

// One tick of every body. The forces first, then the walk.
//
// The walk stops at every arrival -- the first instant anything would meet
// anything else -- and answers it there before spending what is left of the tick.
// An arrival at the very last instant of the tick is answered too: a body that
// lands as the tick ends would otherwise begin the next one already closing on the
// surface it is standing on, and be answered a tick late, from inside.
void PlaygroundRoom::tickBodies() {
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
        if (std::getenv("WOB"))
            std::fprintf(stderr, "  stop %d step %.10f used %.10f rem %.10f v(%.6f,%.6f) w %.6f y %.9f %.9f\n",
                         stop, step, used, remaining, bodies_[0].velocity.x, bodies_[0].velocity.y,
                         bodies_[0].spin, bodies_[0].position[0].y, bodies_[0].position[1].y);
        // A meeting at the very last instant of the tick is left to the next tick,
        // which begins by answering every patch the bodies are touching at -- that
        // is the same meeting, seen with a whole tick to spend on it.
        if (!(remaining > 0.0)) break;
        const std::size_t before = patches.count;
        gatherContacts(patches);
        if (!solveContacts(patches)) {
            // Nothing new is touching and nothing already touching is closing: what
            // is left of the tick is travel along the way the bodies are going.
            remaining -= advanceBodies(remaining);
            break;
        }
        (void)before;
    }
    settleBodies();
    writeImpulses();
}

}  // namespace gtc::native
