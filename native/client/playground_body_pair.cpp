// Bodies against each other.
//
// A corner is a point. Two bodies meet wherever one of those points is on the
// other's outline, and that meeting is answered by the three things this room
// already does for one body:
//
//  - the corner's step is a hitscan segment against the other's outline, the same
//    test the point makes (bodiesReach), so a fast body is stopped by what lies
//    across its path instead of by the size of a step;
//  - a corner that has ended up inside another body is put back onto its outline
//    (pushBodyOutOfBody) -- the same correction the point's own contact makes,
//    applied a corner at a time so the fixed distances carry it and the body turns;
//  - what the two surfaces are still closing at is taken off both sides, once
//    every body has been stepped (bodiesAnswer), exactly as the point's blow is
//    taken off both the point and the body it hit.
//
// Nothing here knows which shapes it is looking at, and there is no radius
// anywhere: a corner has no size.

#include "client/playground_ball_shape.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gtc::native {
using namespace ball_shape;
using namespace playground_detail;
// The tick's own numbers are named in full here: the ball's geometry carries its
// own copy of the time step, the mass and the plane, and two namespaces of the
// same name would be an ambiguity the compiler rightly refuses.
namespace tick = playground_tick;

namespace {

// How far one corner's step goes before it crosses another body's outline, and
// which way out of the piece of outline it meets. The answer is a fraction of the
// step, and the same fraction is used for the whole body: a body is one solid, so
// it is walked as one.
double cornerReaches(Vec3 from, Vec3 to, const Vec3* corners, std::size_t count, Vec3& normal) {
    double along = 2.0;
    if (!segmentHitsPolygon(from, to, corners, count, along, normal)) return 1.0;
    return along;
}

}  // namespace

// How far this body's step can be walked before it meets another body, as a
// fraction of the step. The nearest meeting is where the walk stops, and every
// corner stops with it, because a body is one solid.
//
// Both ways round are walked, and both are the same hitscan. This body's corners
// against the other's outline is the obvious one. The other way -- the other
// body's corner against this body's outline -- is the same meeting seen from the
// other side, and it is the one that catches a face arriving at a corner, which
// brings none of its own corners near anything. Measured in this body's frame, that
// corner travels the other way by the length of this body's own step, so it is the
// same segment test against this body's outline where it started.
double PlaygroundRoom::bodiesReach(std::size_t index, const Vec3* target, Vec3* wayOut) const {
    const Body& body = bodies_[index];
    if (body.count == 0) return 1.0;
    const Vec3 travelling = centroid(target, body.count) - centroid(body.previous.data(), body.count);
    double reach = 1.0;
    Vec3 way{0, 1, 0};
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index) continue;
        const Body& solid = bodies_[other];
        if (solid.count == 0) continue;
        for (std::size_t i = 0; i < body.count; ++i) {
            Vec3 normal{0, 1, 0};
            const double along = cornerReaches(body.previous[i], target[i], solid.position.data(), solid.count, normal);
            if (along < reach) {
                reach = along;
                way = normal;
            }
        }
        for (std::size_t i = 0; i < solid.count; ++i) {
            Vec3 normal{0, 1, 0};
            const double along =
                cornerReaches(solid.position[i], solid.position[i] - travelling, body.previous.data(), body.count, normal);
            if (along < reach) {
                reach = along;
                // The other body's corner is coming in from the outside of this
                // body, so the way out for this body is the other way.
                way = normal * -1.0;
            }
        }
    }
    if (wayOut != nullptr) *wayOut = way;
    return reach;
}

// No corner of a body may be inside another body.
//
// A corner that has ended up inside another body is put back onto that body's
// outline, at the closest point of it -- a corner nudged out along an edge normal
// by a hair stays as deep as it was and creeps. Each body does this for its own
// corners, in its own tick, so a pair is separated by both of them and neither is
// ever shoved by the other's business: a face landing on a corner pushes the body
// the corner belongs to, which is where the force of that landing belongs.
//
// `lift` remembers how far each corner was put back, because a correction is not
// motion: the body's tick takes it back out of that corner's speed, so a body
// pushed up out of another does not fly, and a body resting on one does not gain
// speed every tick.
double PlaygroundRoom::pushBodyOutOfBody(std::size_t index, std::array<Vec3, kMaxCorners>* lift) {
    Body& body = bodies_[index];
    if (body.count == 0) return 0.0;
    double deepest = 0.0;
    const auto move = [&](std::size_t corner, Vec3 amount) {
        body.position[corner] = body.position[corner] + amount;
        if (lift != nullptr) (*lift)[corner] = (*lift)[corner] + amount;
    };
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index) continue;
        const Body& solid = bodies_[other];
        if (solid.count == 0) continue;
        // This body's corners against the other's outline.
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 before = body.position[i];
            Vec3 corner = before;
            if (!pushOutOfPolygon(corner, solid.position.data(), solid.count)) continue;
            const Vec3 amount = corner - before;
            deepest = std::max(deepest, length(amount));
            move(i, amount);
        }
    }
    return deepest;
}

// What the two bodies are still doing at each place they meet, once every body has
// been stepped.
//
// The rule is the one the point's blow already uses, and it is symmetric because
// the two corners carry the same mass. What is driving them together is measured
// along the outline's normal; each side takes half of it, the corner that met
// takes half of that side's share and the body's other corners share the rest, so
// a body is set turning by where it was struck rather than by its middle. Nothing
// is given back -- nothing in this room bounces.
//
// Friction is a force at the contact and it is the same two coefficients the floor
// is held to: while the two surfaces are sliding at less than mu_s times what the
// contact is carrying, it holds them still, and past that mu_k takes its share off
// the slide every tick. Without it a body lying on another slides along it for
// ever, a micrometre at a time, and the contact is never a rest.
//
// The same pass records what is holding what up: a contact whose way out points up
// means this body is standing on the other one, and where along its bottom that
// contact is. A body whose weight does not pass between those places is not held
// up by them, and is not allowed to freeze there.
void PlaygroundRoom::bodiesAnswer() {
    constexpr double kStruckShare = 0.5;
    const auto share = [&](const Body& body, std::size_t struck, std::size_t corner) {
        if (corner == struck) return kStruckShare;
        return body.count > 1 ? (1.0 - kStruckShare) / static_cast<double>(body.count - 1) : 0.0;
    };
    const auto nearestCorner = [](const Body& body, Vec3 point) {
        std::size_t struck = 0;
        double closest = 1e30;
        for (std::size_t i = 0; i < body.count; ++i) {
            const double distance = length(point - body.position[i]);
            if (distance < closest) {
                closest = distance;
                struck = i;
            }
        }
        return struck;
    };
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        bodies_[index].onBody = false;
        bodies_[index].supportLow = 1e30;
        bodies_[index].supportHigh = -1e30;
        bodies_[index].supportFrozen = false;
    }
    for (std::size_t a = 0; a < kBodyCount; ++a) {
        for (std::size_t b = a + 1; b < kBodyCount; ++b) {
            Body& one = bodies_[a];
            Body& two = bodies_[b];
            if (one.count == 0 || two.count == 0) continue;
            // Both ways round: a corner of either body on the other's outline is
            // the same meeting, and it is the corner that carries it.
            for (int round = 0; round < 2; ++round) {
                Body& body = round == 0 ? one : two;
                Body& solid = round == 0 ? two : one;
                const std::size_t self = std::size_t(round == 0 ? a : b);
                const std::size_t them = std::size_t(round == 0 ? b : a);
                for (std::size_t i = 0; i < body.count; ++i) {
                    const OutlineGap gap =
                        outlineGap(body.position[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
                    if (gap.gap > kTouch) continue;
                    const Vec3 normal = gap.normal;
                    const Vec3 contact = body.position[i] - normal * std::max(0.0, gap.gap);
                    const std::size_t other = nearestCorner(solid, contact);
                    // Standing on the other body: the weight has to pass between
                    // these places for it to be carried. The way out of the surface
                    // points from it towards whichever body has this corner, so an
                    // upward one means this body is the one on top -- and a
                    // downward one is the same meeting with the other body on top,
                    // which is the pair seen from the side that did not move.
                    const bool carries = std::abs(normal.y) > 1e-6;
                    Body& held = normal.y > 0.0 ? body : solid;
                    Body& under = normal.y > 0.0 ? solid : body;
                    if (carries) {
                        held.onBody = true;
                        held.supportLow = std::min(held.supportLow, contact.x);
                        held.supportHigh = std::max(held.supportHigh, contact.x);
                        held.supportFrozen = under.frozen;
                    }
                    if (bodyHeld(self) || bodyHeld(them)) continue;
                    const Vec3 meeting = body.velocity[i] - solid.velocity[other];
                    const double closing = dot(meeting, normal);
                    if (closing < -kRestSpeed) {
                        // Something really is leaning on them: a resting body is
                        // woken by a real blow, exactly as a resting shape is woken
                        // by the ball.
                        body.frozen = false;
                        solid.frozen = false;
                    }
                    if (closing < 0.0) {
                        const double answer = -closing * 0.5;
                        for (std::size_t corner = 0; corner < body.count; ++corner)
                            body.velocity[corner] =
                                body.velocity[corner] + normal * (answer * share(body, i, corner));
                        for (std::size_t corner = 0; corner < solid.count; ++corner)
                            solid.velocity[corner] =
                                solid.velocity[corner] - normal * (answer * share(solid, other, corner));
                    }
                    // Friction, on what is running along the surface. The two
                    // bodies share the answer, so the slide each of them gives up is
                    // half of the slide between them.
                    const Vec3 sliding = (body.velocity[i] - solid.velocity[other]) - normal * closing;
                    const double slip = length(sliding);
                    if (!(slip > 0.0)) continue;
                    const Vec3 along = sliding * (1.0 / slip);
                    const double grip = std::min(body.staticFriction, solid.staticFriction);
                    const double slipCoefficient = std::min(body.kineticFriction, solid.kineticFriction);
                    // The pressing is what the contact is carrying: what was driven
                    // together this tick, and -- when it is holding a body up -- that
                    // body's own weight, which is the force the position solve is
                    // answering instead of a speed.
                    const double pressing = std::max(-closing, carries ? kGravity * kTick : 0.0);
                    const double grabbed = grip * pressing;
                    const double kept = slip <= grabbed ? 0.0 : slip - slipCoefficient * pressing;
                    const Vec3 change = along * (slip - kept);
                    for (std::size_t corner = 0; corner < body.count; ++corner)
                        body.velocity[corner] =
                            body.velocity[corner] - change * (0.5 * share(body, i, corner));
                    for (std::size_t corner = 0; corner < solid.count; ++corner)
                        solid.velocity[corner] =
                            solid.velocity[corner] + change * (0.5 * share(solid, other, corner));
                }
            }
        }
    }
    // A frozen body has nothing holding it up the moment the body under it is
    // taken away or starts moving. The plane never goes anywhere, so only a body
    // that is neither on the plane nor on a resting body can be left in the air.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (!body.frozen) continue;
        bool touchingPlane = false;
        for (std::size_t i = 0; i < body.count; ++i)
            touchingPlane = touchingPlane || body.position[i].y <= kSurfaceY + tick::kContactTolerance;
        if (!body.onBody && !touchingPlane) body.frozen = false;
    }
}

// Nothing in this file is a special case of a pair: these are the things a body's
// contacts are made of, and every pair of shapes goes through all of them.

}  // namespace gtc::native
