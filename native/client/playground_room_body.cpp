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
double PlaygroundRoom::timeToContact(double remaining) const {
    double first = remaining;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        // The floor.
        for (std::size_t i = 0; i < body.count; ++i) {
            const double height = body.position[i].y;
            const double falling = pointSpeed(body, body.position[i]).y;
            if (!(falling < 0.0)) continue;
            // A corner already resting on the floor is not on its way to it: it is
            // answered by the contact solve, not by the walk, and stopping the walk
            // at it would stop the whole tick at its first instant.
            if (height <= kContactBand) continue;
            first = std::min(first, height / -falling);
        }
        // The ball. The ball is stepped after the bodies, so for the length of this
        // walk it is a surface that is not going anywhere, and the bodies stop on
        // it; what the ball does about the meeting is its own tick's business.
        {
            Vec3 way{0, 1, 0}, at{};
            const double gap = bodyBallGap(index, way, at);
            const double closing = -dot(pointSpeed(body, at) - ballPointSpeed(at), way);
            if (closing > 1e-12 && gap > kContactBand) first = std::min(first, gap / closing);
        }
        // Another body, seen from the place they are closest.
        for (std::size_t other = 0; other < kBodyCount; ++other) {
            if (other == index || bodies_[other].count == 0) continue;
            const Body& solid = bodies_[other];
            const playground_detail::OutlinePairGap gap =
                pairGap(body.position.data(), body.count, solid.position.data(), solid.count);
            const Vec3 closingSpeed = pointSpeed(body, gap.one) - pointSpeed(solid, gap.two);
            const double closing = -dot(closingSpeed, gap.normal);
            if (closing > 1e-12 && gap.gap > kContactBand) first = std::min(first, gap.gap / closing);
        }
    }
    return std::clamp(first, 0.0, remaining);
}

// Every contact at the configuration the walk has stopped at. The floor is met by
// a corner; another body is met by a corner of one against the outline of the
// other, on both sides, so a face lying on a face is two contacts and a corner
// standing on a face is one.
void PlaygroundRoom::gatherContacts(ContactSet& contacts) const {
    contacts.clear();
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        for (std::size_t i = 0; i < body.count; ++i) {
            if (body.position[i].y > kContactBand) continue;
            Contact contact;
            contact.body = index;
            contact.other = kBodyCount;
            contact.corner = i;
            contact.point = Vec3{body.position[i].x, body.position[i].y, 0.0};
            contact.normal = Vec3{0, 1, 0};
            contacts.add(contact);
        }
    }
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        Vec3 way{0, 1, 0}, at{};
        if (bodyBallGap(index, way, at) <= kContactBand) {
            Contact contact;
            contact.body = index;
            contact.ball = true;
            contact.point = at;
            contact.normal = way;  // out of the ball, into the body
            contact.kind = ForceKind::Ball;
            contacts.add(contact);
        }
    }
    for (std::size_t one = 0; one < kBodyCount; ++one) {
        const Body& body = bodies_[one];
        if (body.count == 0 || body.frozen || bodyHeld(one)) continue;
        for (std::size_t two = 0; two < kBodyCount; ++two) {
            if (two == one || bodies_[two].count == 0) continue;
            const Body& solid = bodies_[two];
            for (std::size_t i = 0; i < body.count; ++i) {
                const ball_shape::OutlineGap gap = ball_shape::outlineGap(body.position[i], solid.position.data(),
                                                  solid.position.data(), solid.count, 1.0);
                if (gap.gap > kContactBand) continue;
                Contact contact;
                contact.body = one;
                contact.other = two;
                contact.corner = i;
                contact.point = body.position[i];
                contact.normal = gap.normal;  // out of the other body, into this one
                contact.kind = ForceKind::Pair;
                contacts.add(contact);
            }
        }
    }
}

// The answer to a whole configuration of contacts at once: sequential impulses.
// Each contact takes the closing speed it finds along its normal, applied to both
// bodies at the place they meet, and the grip along the surface is what the normal
// has carried so far times mu. Nothing bounces: the answer is exactly enough to
// stop the approach and no more.
bool PlaygroundRoom::solveContacts(ContactSet& contacts) {
    bool delivered = false;
    // What the walk needs to know is whether there was anything to answer at all,
    // not whether some impulse was a fraction of a newton different from the last
    // pass: a resting body is touched by its contacts every tick and closing at
    // none of them, and that must not stop the walk.
    bool closingAtAll = false;
    for (int pass = 0; pass < kSolvePasses; ++pass) {
        double worst = 0.0;
        for (std::size_t c = 0; c < contacts.count; ++c) {
            Contact& contact = contacts.items[c];
            Body& body = bodies_[contact.body];
            Body* other = (!contact.ball && contact.other < kBodyCount) ? &bodies_[contact.other] : nullptr;
            // The other side of the contact: a body, the ball, or the floor. Each
            // one contributes its own speed at the place they meet and its own
            // willingness to be moved; the floor contributes neither.
            const auto otherSpeed = [&](Vec3 at) {
                if (other != nullptr) return pointSpeed(*other, at);
                if (contact.ball) return ballPointSpeed(at);
                return Vec3{};
            };
            const auto otherGive = [&](Vec3 at, Vec3 direction) {
                if (other != nullptr) return bodyHeld(contact.other) ? 0.0 : giveAt(*other, at, direction);
                if (contact.ball) return ballGiveAt(at, direction);
                return 0.0;
            };
            const auto pushOther = [&](Vec3 at, Vec3 impulse) {
                if (other != nullptr) {
                    if (!bodyHeld(contact.other)) pushBody(*other, at, impulse, contact.kind);
                } else if (contact.ball) {
                    pushBall(at, impulse, contact.kind);
                }
            };
            const auto relativeSpeed = [&](Vec3 at) { return pointSpeed(body, at) - otherSpeed(at); };
            // The normal. What the contact has carried so far is kept, not
            // forgotten: a contact that has already been answered is answered again
            // from where its own previous answer left it, and an answer that is no
            // longer needed is taken back -- but never past nothing, because a
            // contact can push and cannot pull.
            {
                const double give = giveAt(body, contact.point, contact.normal) +
                                    otherGive(contact.point, contact.normal);
                if (give > 1e-12) {
                    const double closing = dot(relativeSpeed(contact.point), contact.normal);
                    worst = std::max(worst, std::abs(closing));
                    if (closing < -kRestSpeed) closingAtAll = true;
                    const double next = std::max(0.0, contact.normalImpulse - closing / give);
                    const double delta = next - contact.normalImpulse;
                    if (delta != 0.0) {
                        contact.normalImpulse = next;
                        pushBody(body, contact.point, contact.normal * delta, contact.kind);
                        pushOther(contact.point, contact.normal * -delta);
                        delivered = true;
                    }
                }
            }
            // What is under this contact decides what it can carry. A contact on
            // the floor and a contact on another body are the same question, so
            // they are answered by the same lines.
            if (contact.normalImpulse > 0.0 && contact.normal.y > 0.5) {
                if (other == nullptr && !contact.ball) {
                    body.touchingPlane = true;
                } else if (other != nullptr) {
                    body.onBody = true;
                    body.supportFrozen = other->frozen;
                }
                body.supportLow = std::min(body.supportLow, contact.point.x);
                body.supportHigh = std::max(body.supportHigh, contact.point.x);
            }
            // The grip. Coulomb's rule, in its two forms: mu_s holds the slide as
            // long as the grip can carry what holding it would take, and once it
            // cannot, mu_k carries what it can and the rest slips.
            const Vec3 tangent{contact.normal.y, -contact.normal.x, 0.0};
            if (!(contact.normalImpulse > 0.0)) {
                if (contact.tangentImpulse != 0.0) {
                    pushBody(body, contact.point, tangent * -contact.tangentImpulse, contact.kind);
                    pushOther(contact.point, tangent * contact.tangentImpulse);
                    contact.tangentImpulse = 0.0;
                }
                continue;
            }
            const double giveTangent = giveAt(body, contact.point, tangent) +
                                       otherGive(contact.point, tangent);
            if (!(giveTangent > 1e-12)) continue;
            const double slide = dot(relativeSpeed(contact.point), tangent);
            double muStatic = body.staticFriction, muKinetic = body.kineticFriction;
            if (other != nullptr) {
                muStatic = 0.5 * (muStatic + other->staticFriction);
                muKinetic = 0.5 * (muKinetic + other->kineticFriction);
            } else if (contact.ball) {
                muStatic = std::min(muStatic, ball_shape::kBallStaticFriction);
                muKinetic = std::min(muKinetic, ball_shape::kBallKineticFriction);
            }
            double next = contact.tangentImpulse - slide / giveTangent;
            const double hold = muStatic * contact.normalImpulse;
            if (std::abs(next) > hold) {
                // Held no longer: the grip gives what sliding takes, and the rest
                // of the pull goes into movement.
                const double slip = muKinetic * contact.normalImpulse;
                next = std::copysign(std::min(std::abs(next), slip), next);
            }
            const double delta = next - contact.tangentImpulse;
            if (delta != 0.0) {
                contact.tangentImpulse = next;
                pushBody(body, contact.point, tangent * delta, contact.kind);
                pushOther(contact.point, tangent * -delta);
                delivered = true;
            }
        }
        if (worst < kSolveTolerance && pass > 0) break;
    }
    (void)delivered;
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
            body.restPose = body.centre;
            body.restPoseAngle = body.angle;
        }
        // Where it is standing: the lowest pose of the settling, which is the one
        // it comes back to every rock and the one it would be left in if the rock
        // were taken away.
        if (body.centre.y <= body.restLow.y) {
            body.restPose = body.centre;
            body.restPoseAngle = body.angle;
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
            body.frozen = true;
            body.velocity = {};
            body.spin = 0.0;
            // Asleep where it was standing, not at whatever phase of its last
            // twitch the arithmetic happened to end on.
            body.centre = body.restPose;
            body.angle = body.restPoseAngle;
            placeCorners(body);
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
    // One set of contacts for the whole tick, carried through the walk. The
    // contacts that are there when the tick starts are answered first -- the weight
    // of a shape standing on the floor arrives as one tick's worth of impulse, and a
    // contact that answers none of it leaves the shape moving through a tick it
    // should be standing still through. An arrival later in the tick joins the same
    // set, keeping what the contacts it shares already carry, so the tick converges
    // on one answer for each meeting instead of answering the same one again.
    ContactSet contacts;
    gatherContacts(contacts);
    solveContacts(contacts);
    double remaining = kTick;
    for (int stop = 0; stop < kMaxWalkStops; ++stop) {
        const double step = timeToContact(remaining);
        if (step > 0.0) remaining -= advanceBodies(step);
        // A meeting at the very last instant of the tick is left to the next tick,
        // which begins by answering every contact the bodies are touching at -- that
        // is the same meeting, seen with a whole tick to spend on it.
        if (!(remaining > 0.0)) break;
        const std::size_t before = contacts.count;
        gatherContacts(contacts);
        if (contacts.count == before && !solveContacts(contacts)) {
            // Nothing new is touching and nothing already touching is closing: what
            // is left of the tick is travel along the way the bodies are going.
            remaining -= advanceBodies(remaining);
            break;
        }
        if (!solveContacts(contacts)) {
            remaining -= advanceBodies(remaining);
            break;
        }
    }
    settleBodies();
    writeImpulses();
}

}  // namespace gtc::native
