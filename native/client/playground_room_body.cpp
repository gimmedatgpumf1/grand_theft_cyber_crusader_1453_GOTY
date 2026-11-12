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

    // A patch has four unknowns: the load at each of its two ends, and the grip at
    // each of them. They are solved together, as one four by four system, because
    // they are not independent -- the load at one end turns the body, the grip at an
    // end turns it the other way, and answering them one at a time leaves the body
    // being turned by whichever was answered last. That is what held a face up at a
    // single corner and rocked it; answered together, a face that has landed flat is
    // stopped where it lands.
    const auto patchPass = [&](Manifold& patch) {
        const std::size_t count = patch.ends;
        Body* body = &bodies_[patch.body];
        Body* other = (!patch.ball && patch.other < kBodyCount) ? &bodies_[patch.other] : nullptr;
        const bool otherFixed = other != nullptr && bodyHeld(patch.other);
        const double ballMoment = 0.4 * ball_.mass * ball_.radius * ball_.radius;
        const Vec3 tangent{patch.normal.y, -patch.normal.x, 0.0};
        const auto lever = [](Vec3 arm, Vec3 axis) { return arm.x * axis.y - arm.y * axis.x; };
        // How much the body gives at one place along one direction when it is pushed
        // at another: the reciprocal of its mass, plus the two levers about its
        // centre. Every body in the patch contributes its own.
        const auto give = [&](Vec3 at, Vec3 toward, Vec3 pushAt, Vec3 direction) {
            // The share the mass carries is only the part of the push that is along
            // the direction being asked about; a push across it moves the point only
            // through the turn. Leaving that out makes a patch's own system singular
            // as soon as its two ends are at the same height, which is every face
            // lying flat on the floor.
            double value = dot(toward, direction) / body->mass +
                           lever(at - body->centre, toward) * lever(pushAt - body->centre, direction) /
                               body->moment;
            if (other != nullptr) {
                if (!otherFixed) {
                    value += dot(toward, direction) / other->mass +
                             lever(at - other->centre, toward) *
                                 lever(pushAt - other->centre, direction) / other->moment;
                }
            } else if (patch.ball) {
                value += dot(toward, direction) / ball_.mass +
                         lever(at - ball_.position, toward) * lever(pushAt - ball_.position, direction) /
                             ballMoment;
            }
            return value;
        };
        const auto otherSpeed = [&](Vec3 at) {
            if (other != nullptr) return otherFixed ? Vec3{} : pointSpeed(*other, at);
            if (patch.ball) return ballPointSpeed(at);
            return Vec3{};
        };
        const auto pushOther = [&](Vec3 at, Vec3 impulse) {
            if (other != nullptr) {
                if (!otherFixed) pushBody(*other, at, impulse, patch.kind);
            } else if (patch.ball) {
                pushBall(at, impulse, patch.kind);
            }
        };
        // Three unknowns: the load at each end of the patch, and the grip over it.
        //
        // The grip is one number and not two. A face sliding on a surface slides as
        // a whole -- the two ends of it have the same way along the surface -- so two
        // separate grips would be two unknowns answering one question. The grip is
        // therefore a uniform one over the face: half of it at each end, which is
        // one resultant at the middle of the patch and no couple of its own. The load
        // is two numbers, because the load is what a body standing on the floor
        // decides with: it is the difference between the two ends that holds a body
        // flat instead of tipping it.
        const std::size_t dofs = count + 1;
        Vec3 point[2 * kMaxPatchEnds], direction[2 * kMaxPatchEnds];
        for (std::size_t k = 0; k < count; ++k) {
            point[k] = patch.point[k];
            direction[k] = patch.normal;
            point[count + k] = patch.point[k];
            direction[count + k] = tangent;
        }
        double share[2 * kMaxPatchEnds] = {};
        for (std::size_t k = 0; k < count; ++k) share[count + k] = count == 2 ? 0.5 : 1.0;
        // What each of those acts on, and how hard the body answers it.
        double u[2 * kMaxPatchEnds] = {};
        double g[2 * kMaxPatchEnds][2 * kMaxPatchEnds] = {};
        for (std::size_t i = 0; i < 2 * count; ++i) {
            u[i] = dot(pointSpeed(*body, point[i]) - otherSpeed(point[i]), direction[i]);
            for (std::size_t j = 0; j < 2 * count; ++j) g[i][j] = give(point[i], direction[i], point[j], direction[j]);
        }
        for (std::size_t k = 0; k < count; ++k) {
            if (u[k] < -kRestSpeed) closingAtAll = true;
            worst = std::max(worst, std::abs(u[k]));
        }
        // The reduced system: the two loads and the one grip, in the grip's own
        // basis, so that what is solved is exactly what is applied. Answering the
        // whole patch at once is what holds a face flat; the constraints are then
        // checked one at a time and the ones the floor cannot honour are let go, so
        // that what is left is answered exactly rather than approximately.
        double loadCarried[2] = {patch.normalImpulse[0], count == 2 ? patch.normalImpulse[1] : 0.0};
        const double carriedGrip = count == 2 ? 0.5 * (patch.tangentImpulse[0] + patch.tangentImpulse[1])
                                              : patch.tangentImpulse[0];
        double mu = body->staticFriction;
        if (other != nullptr) {
            mu = 0.5 * (mu + other->staticFriction);
        } else if (patch.ball) {
            mu = std::min(mu, ball_shape::kBallStaticFriction);
        }
        bool useLoad[2] = {true, count == 2};
        bool useGrip = true;
        double answer[3] = {};
        double gripFixed = 0.0;  // the grip already applied, when it is at its limit
        double gripHeld = carriedGrip;
        for (int attempt = 0; attempt < 4; ++attempt) {
            const std::size_t active = (useLoad[0] ? 1u : 0u) + (useLoad[1] ? 1u : 0u) + (useGrip ? 1u : 0u);
            if (active == 0) break;
            std::size_t dofOf[3] = {3, 3, 3};  // 3 = not in the system
            std::size_t dofs = 0;
            for (std::size_t k = 0; k < count; ++k)
                if (useLoad[k]) dofOf[k] = dofs++;
            if (useGrip) dofOf[2] = dofs++;
            double matrix[3][4] = {};
            for (std::size_t i = 0; i < count; ++i) {
                if (!useLoad[i]) continue;
                matrix[dofOf[i]][dofs] = -u[i];
                for (std::size_t j = 0; j < count; ++j)
                    if (useLoad[j]) matrix[dofOf[i]][dofOf[j]] = g[i][j];
                if (useGrip) {
                    double cross = 0.0;
                    for (std::size_t j = 0; j < 2 * count; ++j) cross += share[j] * g[i][j];
                    matrix[dofOf[i]][dofOf[2]] = cross;
                    matrix[dofOf[2]][dofOf[i]] = cross;
                }
            }
            if (useGrip) {
                double self = 0.0, target = 0.0;
                for (std::size_t i = 0; i < 2 * count; ++i) {
                    target += share[i] * u[i];
                    for (std::size_t j = 0; j < 2 * count; ++j) self += share[i] * share[j] * g[i][j];
                }
                matrix[dofOf[2]][dofOf[2]] = self;
                matrix[dofOf[2]][dofs] = -target;
            }
            bool singular = false;
            for (std::size_t col = 0; col < dofs && !singular; ++col) {
                std::size_t pivot = col;
                for (std::size_t row = col + 1; row < dofs; ++row)
                    if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) pivot = row;
                if (!(std::abs(matrix[pivot][col]) > 1e-18)) singular = true;
                if (singular) break;
                if (pivot != col)
                    for (std::size_t j = 0; j <= dofs; ++j) std::swap(matrix[col][j], matrix[pivot][j]);
                for (std::size_t row = 0; row < dofs; ++row) {
                    if (row == col) continue;
                    const double factor = matrix[row][col] / matrix[col][col];
                    if (factor == 0.0) continue;
                    for (std::size_t j = col; j <= dofs; ++j) matrix[row][j] -= factor * matrix[col][j];
                }
            }
            if (singular) return;
            for (std::size_t i = 0; i < 3; ++i) answer[i] = 0.0;
            for (std::size_t i = 0; i < 3; ++i)
                if (dofOf[i] < dofs) answer[i] = matrix[dofOf[i]][dofs] / matrix[dofOf[i]][dofOf[i]];
            // A load the floor cannot give is a pull: that end lifts off, and the
            // patch is answered again without it, so what is left is exact.
            bool changed = false;
            for (std::size_t k = 0; k < count; ++k) {
                if (!useLoad[k]) continue;
                const double wouldBe = loadCarried[k] + answer[k];
                if (wouldBe < 0.0) {
                    useLoad[k] = false;
                    changed = true;
                }
            }
            if (changed) continue;
            double total = 0.0;
            for (std::size_t k = 0; k < count; ++k)
                if (useLoad[k]) total += loadCarried[k] + answer[k];
            // Grip is only ever as much as the load under it can hold. When it asks
            // for more than that, the rest slides: the grip goes to the limit, is
            // applied as a fixed blow, and the loads are answered again under it.
            if (useGrip) {
                const double wouldBe = gripHeld + answer[2];
                const double limit = mu * total;
                if (std::abs(wouldBe) > limit) {
                    gripFixed = std::copysign(limit, wouldBe);
                    const double delta = gripFixed - gripHeld;
                    if (delta != 0.0)
                        for (std::size_t k = 0; k < count; ++k) {
                            pushBody(*body, patch.point[k], tangent * (delta * share[count + k]), patch.kind);
                            pushOther(patch.point[k], tangent * -(delta * share[count + k]));
                        }
                    gripHeld = gripFixed;
                    useGrip = false;
                    changed = true;
                }
            }
            if (!changed) break;
        }
        double load[2] = {};
        for (std::size_t k = 0; k < count; ++k) {
            load[k] = loadCarried[k] + (useLoad[k] || !(loadCarried[k] > 0.0) ? answer[k] : 0.0);
            // A load the floor cannot give is a pull: the floor presses and does not
            // pull, so nothing is carried there.
            if (!(load[k] > 0.0)) load[k] = 0.0;
            const double loadDelta = load[k] - patch.normalImpulse[k];
            if (loadDelta != 0.0) {
                patch.normalImpulse[k] = load[k];
                pushBody(*body, patch.point[k], patch.normal * loadDelta, patch.kind);
                pushOther(patch.point[k], patch.normal * -loadDelta);
            }
            // What is under this end decides what the body is standing on.
            if (patch.normalImpulse[k] > 0.0 && patch.normal.y > 0.5) {
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
        }
        // The grip, half at each end, as the one resultant it is.
        if (useGrip) gripFixed = gripHeld + answer[2];
        const double gripDelta = gripFixed - carriedGrip;
        if (gripDelta != 0.0) {
            for (std::size_t k = 0; k < count; ++k) {
                patch.tangentImpulse[k] += gripDelta * share[count + k];
                pushBody(*body, patch.point[k], tangent * (gripDelta * share[count + k]), patch.kind);
                pushOther(patch.point[k], tangent * -(gripDelta * share[count + k]));
            }
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
        if (step > 0.0) remaining -= advanceBodies(step);
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
