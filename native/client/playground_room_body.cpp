// One body's tick, on its own.
//
// A body is its corner points of equal mass. Every tick each corner is given
// gravity and asked to travel its own v/20, its own hitscan segment is traced
// against the plane, and then the fixed distances are pushed back to what they
// were. A corner that moves therefore pulls and pushes the others through those
// distances: that is the whole of the coupling.
//
// It is here, away from the room's own tick, because it is the longest single
// thing in the room and the room had grown to within a hair of the line the
// build refuses to cross. Nothing about it changed in the move.

#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gtc::native {
using namespace playground_detail;
using namespace playground_tick;

void PlaygroundRoom::tickBody(std::size_t index) {
    Body& body = bodies_[index];
    body.previous = body.position;
    body.metBody = kBodyCount;
    const std::size_t count = body.count;
    if (bodyHeld(index)) {
        // The mouse owns this body: it is placed, not simulated, so nothing here
        // may move it or decide it has come to rest.
        for (std::size_t i = 0; i < count; ++i) {
            body.velocity[i] = {};
            body.previous[i] = body.position[i];
        }
        body.frozen = false;
        return;
    }
    std::array<bool, kMaxCorners> onPlane{};
    // Where this tick's step would put each corner, before anything is committed.
    std::array<Vec3, kMaxCorners> target{};
    for (std::size_t i = 0; i < count; ++i) {
        body.velocity[i].y -= kGravity * kTick;
        target[i] = body.position[i] + body.velocity[i] * kTick;
    }
    // The shape's step is swept against the ball before any of it is spent,
    // exactly as a corner's step is swept against the plane -- but along the whole
    // outline, because a face lying across a ball has no corner anywhere near it
    // and a corner's own hitscan says nothing about it. What the sweep answers
    // with is the last fraction of the step at which every piece of the outline is
    // still clear of the ball; the step is walked that far, and what is left of
    // the tick is a contact, solved below against the piece of the outline the
    // ball is actually holding. Without this the whole step is carried into the
    // ball and then pushed back out of it, and coming back to the same place every
    // tick is a shape perched on a ball for ever, never turning about it.
    Vec3 metWay{0, 1, 0};
    const double bodyReach = bodiesReach(index, target.data(), &metWay, &body.metBody);
    const double ballReach = bodyBallReach(index, target.data());
    // And the floor, swept the same way the other two are: the first fraction of the
    // step at which any of this body's corners arrives at it.
    //
    // This is the whole of what a landing is. Clamping each corner after the step has
    // been taken is not a sweep: it lets the corners that reached the floor stop while
    // the rest of the body carries on through the tick, so the shape is squashed flat
    // by however far it fell in that one tick -- a metre and a half a second squashes a
    // cube by a quarter of a metre -- and then the fixed distances have to put it back,
    // and the only way they can is to shove the corners sideways. A cube landing flat
    // on the floor then slides seven centimetres for no reason at all.
    //
    // A corner already on the floor is not arriving at it: it is being carried by it,
    // and gravity would otherwise find an arrival at the first instant of every tick
    // and hold the body still for ever. Only a corner that starts clear of the floor
    // and ends below it has arrived.
    double planeReach = 1.0;
    for (std::size_t i = 0; i < count; ++i) {
        if (body.position[i].y <= kSurfaceY + kContactTolerance) continue;
        if (!(target[i].y < kSurfaceY)) continue;
        const double along = (kSurfaceY - body.position[i].y) / (target[i].y - body.position[i].y);
        if (along < planeReach) planeReach = along;
    }
    const double reach = std::min(std::min(ballReach, bodyReach), planeReach);
    const bool metPlane = planeReach < 1.0 && planeReach <= ballReach && planeReach <= bodyReach;
    // The floor's way out is straight up, whatever the other sweeps found: it is the
    // floor that cut this step short.
    if (metPlane) metWay = Vec3{0, 1, 0};
    // What each corner was trying to do this tick, before anything cut it short.
    //
    // A body stopped by another body must not have its speed read back off the
    // shortened step. Reading it back hands every corner the same fraction of its
    // own travel, which is a support under the body's middle -- and a body standing
    // on the point of another is not held under its middle, that is the whole of why
    // it tips. The blow at such a contact is answered where the two bodies met, by
    // bodiesAnswer, from the approach both of them arrived with; the step is only
    // where they ended up.
    std::array<Vec3, kMaxCorners> attempted{};
    for (std::size_t i = 0; i < count; ++i) attempted[i] = body.velocity[i];
    // A body that met another body keeps the speed it arrived with, because the blow
    // is answered at the point they met by bodiesAnswer. The ball answers its own
    // contact. The floor is answered at the corners it holds, below.
    const bool metBody = bodyReach < ballReach && bodyReach < planeReach && bodyReach < 1.0;
    if (reach < 1.0) {
        for (std::size_t i = 0; i < count; ++i)
            body.position[i] = body.previous[i] + (target[i] - body.previous[i]) * reach;
        if (metPlane || bodyReach <= ballReach) {
            // What is left of the step is spent along the piece of surface that was
            // met, and not into it: a body that has met something still has the rest
            // of its tick to travel, and along the surface is the only travel the
            // surface allows. Walking the whole of it as it was is carrying the
            // body into the other one by however fast it happened to be going.
            for (std::size_t i = 0; i < count; ++i) {
                Vec3 left = body.velocity[i] * (kTick * (1.0 - reach));
                left = left - metWay * std::min(0.0, dot(left, metWay));
                target[i] = body.position[i] + left;
            }
        } else {
            for (std::size_t i = 0; i < count; ++i)
                target[i] = body.position[i] + body.velocity[i] * (kTick * (1.0 - reach));
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 from = body.position[i];
        const Vec3 to = target[i];
        // The same hitscan the point uses, once per corner: the first thing the
        // corner's step reaches is where that corner stops, however far the tick
        // would have carried it. The ball is the other thing in the way, and it
        // was already walked against the whole outline above, so a corner can no
        // longer reach it on its own.
        double along = 2.0;
        bool onFloor = false;
        if (to.y < kSurfaceY) {
            along = std::clamp((kSurfaceY - from.y) / (to.y - from.y), 0.0, 1.0);
            onFloor = true;
        }
        if (along <= 1.0) {
            body.position[i] = from + (to - from) * along;
            if (onFloor) body.position[i].y = kSurfaceY;
        } else {
            body.position[i] = to;
        }
        onPlane[i] = body.position[i].y <= kSurfaceY + 1e-12;
    }
    // The shape and the plane are solved together until both are satisfied. A
    // pass pushes every fixed distance back to its rest length, then puts any
    // corner the pass pushed through the plane back on it, and that repeats
    // until neither is out by more than a nanometre. Solving the distances and
    // then clamping once -- or clamping inside the distance loop without
    // checking the distances again -- is what leaves a body permanently out of
    // shape: it is the clamp that opens the distance error, so the distance
    // solve has to see the clamp.
    // How far the plane lifted each corner. Lifting a corner is a correction,
    // not motion: the body did not travel upwards, the floor put it back. That
    // lift has to be kept so it can be taken back out of the velocity, or a
    // corner pushed down by a blow turns into the whole body hopping upwards.
    // Which corners the plane is holding up. It holds their height and nothing
    // else -- a corner standing on the floor can still slide along it -- so a
    // fixed distance that wants to move such a corner downwards has to put that
    // motion into the corner at the other end instead. Sharing it equally is
    // what leaves a tall body balanced on a corner the floor cannot carry,
    // because the corner it leaned on never actually went anywhere.
    std::array<bool, kMaxCorners> held{};
    for (std::size_t i = 0; i < count; ++i) held[i] = onPlane[i];
    // How far the plane lifted each corner. Lifting a corner is a correction,
    // not motion: the body did not travel upwards, the floor put it back. That
    // lift has to be kept so it can be taken back out of the velocity, or a
    // corner pushed down by a blow turns into the whole body hopping upwards.
    std::array<Vec3, kMaxCorners> lift{};
    // The same for the ball, once: how far it had to put the body's own step back
    // out of it. Later passes are the shape re-arranging itself about the contact,
    // which is the shape turning, and that is motion.
    std::array<Vec3, kMaxCorners> ballLift{};
    // And the same for another body: how far it put this body's own step back out
    // of itself. Later passes are the shape re-arranging about the contact, which is
    // the shape turning, and that is motion.
    std::array<Vec3, kMaxCorners> bodyLift{};

    for (int pass = 0; pass < kMaxDistancePasses; ++pass) {
        double worstDistance = 0.0;
        // Both ways round every pass. Pushing the distances back in one fixed
        // order lets the order itself move the body: whatever the first pair
        // does, the next one is projected onto a slightly different shape, and
        // over a second that steady lean adds up. Sweeping forwards and then
        // backwards takes it out again, so no pair is ever favoured.
        for (int step = 0; step < int(body.pairCount) * 2; ++step) {
            const std::size_t pair = step < int(body.pairCount) ? std::size_t(step)
                                                                : std::size_t(int(body.pairCount) * 2 - 1 - step);
            const std::size_t a = std::size_t(body.pairs[pair][0]), b = std::size_t(body.pairs[pair][1]);
            const Vec3 delta = body.position[b] - body.position[a];
            const double current = length(delta);
            if (!(current > 1e-12)) continue;
            const Vec3 direction = delta / current;
            const double deviation = current - body.rest[pair];
            if (step < int(body.pairCount)) worstDistance = std::max(worstDistance, std::abs(deviation));
            // How much of the correction each end can take: half each across the
            // floor, but none of it downwards at a corner the plane is holding.
            const double acrossA = 0.5, acrossB = 0.5;
            const double downA = held[a] ? 0.0 : 0.5, downB = held[b] ? 0.0 : 0.5;
            const double reach = 2.0 * acrossA * direction.x * direction.x +
                                 (downA + downB) * direction.y * direction.y;
            if (!(reach > 1e-12)) continue;
            const double share = deviation / reach;
            body.position[a] += Vec3{direction.x * share * acrossA, direction.y * share * downA, 0};
            body.position[b] -= Vec3{direction.x * share * acrossB, direction.y * share * downB, 0};
        }
        double deepest = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (body.position[i].y < kSurfaceY) {
                const double amount = kSurfaceY - body.position[i].y;
                deepest = std::max(deepest, amount);
                // The floor corrects the body it is holding the way the ball and
                // another body do: rigidly, at the corner that is through it. Pinning
                // that one corner and letting the fixed distances pull the rest of the
                // body into line is what let a cube come down at seventeen degrees and
                // arrive flat, its top corners thrown a metre and a half sideways, and
                // what left a corner of it sitting under the floor afterwards.
                pushBodyAtPoint(index, body.position[i], Vec3{0, amount, 0}, &lift);
                onPlane[i] = true;
                held[i] = true;
            }
        }
        // The ball is the second thing that holds the shape: the piece of the
        // outline the ball is against is put back outside it, and the distances
        // are solved again, so the corner the ball is not holding is the one that
        // gives way. That is the whole of what makes a shape tip off a ball instead
        // of resting on it.
        deepest = std::max(deepest, pushBodyOutOfBall(index, pass == 0 ? &ballLift : nullptr));
        // And another body is the third thing that holds the shape, by the same
        // rule: the corner that has gone into it is put back out, and the distances
        // are solved again, so the corner that is not held is the one that gives
        // way. That is what makes a shape tip on the corner of another instead of
        // lying across it.
        deepest = std::max(deepest, pushBodyOutOfBody(index, pass == 0 ? &bodyLift : nullptr));
        // The floor is not finished with until every corner is out of it. Asking the
        // loop only after the distances are closed lets it leave with a corner still
        // under the floor, and a corner under the floor is a shape being squashed for
        // a whole tick -- which is what the next tick then spends its passes undoing.
        double under = 0.0;
        for (std::size_t i = 0; i < count; ++i) under = std::max(under, kSurfaceY - body.position[i].y);
        if (worstDistance <= kDistanceTolerance && deepest <= kDistanceTolerance && under <= 0.0) break;
    }
    // Whatever the loop left, no corner may be under the floor when the tick ends:
    // the floor is not a suggestion. Correcting it the way the floor's own pass does
    // -- rigidly, at the corner that is through it -- keeps the shape.
    for (std::size_t i = 0; i < count; ++i) {
        if (body.position[i].y < kSurfaceY - kDistanceTolerance) {
            pushBodyAtPoint(index, body.position[i], Vec3{0, kSurfaceY - body.position[i].y, 0}, &lift);
            onPlane[i] = true;
            held[i] = true;
        }
    }
    // How much of this body is lying on the floor: the length of the contact
    // between the corners that are actually on it. A longer contact grips
    // harder, so the square's 0.70 m footprint is the reference and the
    // triangle's 2.10 m footprint gets three times its friction for the same
    // coefficients.
    double leftmost = 1e30, rightmost = -1e30;
    bool touchingPlane = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (body.position[i].y > kSurfaceY + kContactTolerance) continue;
        touchingPlane = true;
        leftmost = std::min(leftmost, body.position[i].x);
        rightmost = std::max(rightmost, body.position[i].x);
    }
    const double footprint = touchingPlane ? std::max(0.0, rightmost - leftmost) / kCubeSide : 0.0;
    const double holdSpeed = body.staticFriction * footprint * kGravity * kTick;
    const double slideSpeed = body.kineticFriction * footprint * kGravity * kTick;

    // Which corners are touching the plane now, after the solve has finished
    // moving them. A clamp from an earlier pass is not a contact: the solve may
    // have lifted that corner back off the plane, and a corner in the air has to
    // keep falling instead of being held and rubbed by the floor.
    for (std::size_t i = 0; i < count; ++i) onPlane[i] = body.position[i].y <= kSurfaceY + kContactTolerance;
    for (std::size_t i = 0; i < count; ++i) {
        // What the corner actually did this tick, solved shape and all: a shape
        // that is turning about something is doing that, and the turn is motion.
        //
        // A corner that another body stopped keeps what it was trying to do. The
        // contact is answered from the approach the body arrived with, at the place
        // the two of them met, and not by trimming the travel afterwards.
        Vec3 velocity = metBody ? attempted[i] : (body.position[i] - body.previous[i]) / kTick;
        // Take the corrections back out: what is left is what the corner was
        // actually doing. The floor's is upwards; the ball's is outwards.
        velocity = velocity - lift[i] * (1.0 / kTick);
        velocity = velocity - ballLift[i] * (1.0 / kTick);
        velocity = velocity - bodyLift[i] * (1.0 / kTick);
        if (onPlane[i]) {
            // The plane holds this corner: it cannot travel into the plane.
            // Sideways is friction's business, and that is done for the body as
            // a whole just below.
            if (velocity.y < 0.0) velocity.y = 0.0;
        }
        body.velocity[i] = velocity;
    }
    // Friction is a force at the contact patch, and the patch pushes the body,
    // not one corner of it. Taking the slide off each corner on its own does
    // nothing once the floor has pinned that corner, which is why the body used
    // to stop dead no matter what coefficients it had: the body's own sideways
    // motion is what mu_s has to hold and mu_k has to slow, and the fixed
    // distances carry that to every corner.
    if (touchingPlane) {
        Vec3 middleVelocity{};
        for (std::size_t i = 0; i < count; ++i) middleVelocity += body.velocity[i];
        middleVelocity = middleVelocity / static_cast<double>(count);
        const double sideways = middleVelocity.x;
        if (std::abs(sideways) <= holdSpeed) {
            // mu_s * N answers it: it does not move at all.
            for (std::size_t i = 0; i < count; ++i) body.velocity[i].x -= sideways;
        } else {
            // Sliding: mu_k * N takes the same speed off every corner.
            for (std::size_t i = 0; i < count; ++i) body.velocity[i].x -= std::copysign(slideSpeed, sideways);
        }
    }
    if (std::getenv("GTC_V") && index == 1)
        std::fprintf(stderr, "  V c0 pos (%.6f,%.6f) vel (%.5f,%.5f) lift (%+.7f,%+.7f) ballLift (%+.7f,%+.7f) bodyLift (%+.7f,%+.7f) metBody %zu\n",
                     body.position[0].x, body.position[0].y, body.velocity[0].x, body.velocity[0].y, lift[0].x, lift[0].y,
                     ballLift[0].x, ballLift[0].y, bodyLift[0].x, bodyLift[0].y, body.metBody);
    // Nothing moved it this tick. The floor puts a corner back on itself exactly,
    // so a body resting on the floor moves by nothing at all; a body resting on
    // another body is put back by the constraint solve, which is exact only to its
    // own tolerance -- and a body that is still being moved by the width of the
    // solver's own rounding is not moving.
    bool quiet = true;
    for (std::size_t i = 0; i < count; ++i) {
        quiet = quiet && length(body.position[i] - body.previous[i]) < kDistanceTolerance;
    }
    // A body is carried by the plane only if its weight passes between the
    // corners that are actually touching it. Balanced on one corner it is not
    // held up at all -- it topples -- so it is not allowed to freeze there and
    // the solve is left to tip it onto the corners that can carry it. Without
    // this a body can be caught at the instant it is standing on a single
    // corner, and stop in mid-air.
    const Vec3 middle = centroid(body.position.data(), count);
    const bool carriedByPlane = touchingPlane && middle.x >= leftmost - 1e-6 && middle.x <= rightmost + 1e-6;
    // And another body carries it the same way: what it is standing on has to be
    // under the weight, and it has to be at rest itself.
    //
    // What it is standing on also has to have some extent. A single contact point
    // is a round surface with no radius in it -- the triangle's corner, or the top
    // of a ball -- and a shape on one of those is carried only in the one
    // configuration where its weight runs straight down the point, which is not a
    // rest but a knife edge: the smallest thing tips it. Freezing it there is what
    // left a cube perched on the point of the triangle for ever. A support with
    // extent can carry it, and that is a face or a pair of corners, not a point.
    const bool carriedByBody = body.onBody && body.supportFrozen && body.supportHigh > body.supportLow &&
                               middle.x >= body.supportLow - 1e-6 && middle.x <= body.supportHigh + 1e-6;
    const bool carried = carriedByPlane || carriedByBody;
    // Resting on the plane and no longer going anywhere: there is no force left
    // to resolve, so the body is frozen and its ticks stop costing anything.
    // Only the plane can do that. A ball is a round surface: it carries a shape
    // only where the shape's weight runs straight down through the point of
    // contact, which is one configuration out of the whole of the ball, so a shape
    // left on a ball is never at rest -- it tips and it slides off. Freezing it
    // there because its corners happen to be moving very little is what left a
    // shape perched on the top of a ball for ever.
    // A body asleep on another body that is no longer under it has nothing holding
    // it: the plane never goes anywhere, so only a body without its own corner on
    // the floor can lose a support that way.
    if (quiet && carried && !bodyHeld(index)) {
        body.frozen = true;
        for (std::size_t i = 0; i < count; ++i) {
            body.velocity[i] = {};
            body.previous[i] = body.position[i];
            body.shown[i] = body.position[i];
        }
    }
}

}  // namespace gtc::native
