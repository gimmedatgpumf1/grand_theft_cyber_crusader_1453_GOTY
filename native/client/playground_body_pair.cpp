// Two bodies meeting.
//
// What is left here is the two questions a pair asks that no single body does: can
// the mouse put a shape down where it would overlap something, and are two outlines
// into each other at all. Neither of them is part of a tick: the tick's own walk is
// in playground_room_body.cpp, and it stops each body at the instant it would meet
// something, which is why nothing here ever has to put two shapes apart.

#include "client/playground_ball_shape.h"
#include "client/playground_pair.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdlib>

namespace gtc::native {
using namespace ball_shape;
using namespace playground_detail;

// Whether two bodies' outlines are into each other, how deep, and which way the
// first has to go to be out. The walk never lets this happen; the check is here so
// that the walk can prove it: a step that would have left one inside the other is
// not taken.
bool PlaygroundRoom::bodiesIntoEachOther(std::size_t one, std::size_t two, Vec3& way, double& depth) const {
    if (one >= kBodyCount || two >= kBodyCount) return false;
    const Body& a = bodies_[one];
    const Body& b = bodies_[two];
    if (a.count == 0 || b.count == 0) return false;
    Vec3 out{0, 1, 0};
    double amount = 0.0;
    if (!outlineOverlap(a.position.data(), a.count, b.position.data(), b.count, out, amount)) return false;
    // The direction the first body has to travel to be clear of the second.
    way = out * -1.0;
    depth = amount;
    return true;
}

// Whether a body can be put down with its corners where `wanted` says: not through
// another body's outline, not under a corner of one, not through the ball, and not
// through the floor.
//
// This is the same question the sweep asks, asked before the mouse lets go instead of
// after. Everything else in this room refuses to let two shapes be inside each other,
// and the mouse is the one thing that can put them there -- dragging is not simulated,
// so a body follows the cursor wherever it goes. A body released inside another is a
// state the tick cannot answer without inventing motion: the overlap has to be undone
// somewhere, and undoing a hundredth of a metre every tick is a shape creeping out of
// a shape it was never in. So it is not put there in the first place.
bool PlaygroundRoom::bodyPlacementClear(std::size_t index, const std::array<Vec3, kMaxCorners>& wanted) const {
    const Body& body = bodies_[index < kBodyCount ? index : 0];
    if (body.count == 0) return true;
    if (outlineGap(ball_.position, wanted.data(), wanted.data(), body.count, 1.0).gap <
        ball_.radius + kSkin)
        return false;
    for (std::size_t i = 0; i < body.count; ++i)
        if (wanted[i].y < kSurfaceY - kSkin) return false;
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index || bodies_[other].count == 0) continue;
        const Body& solid = bodies_[other];
        for (std::size_t i = 0; i < body.count; ++i) {
            const OutlineGap gap = outlineGap(wanted[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
            if (gap.gap < kSkin) return false;
        }
        for (std::size_t i = 0; i < solid.count; ++i) {
            const OutlineGap gap = outlineGap(solid.position[i], wanted.data(), wanted.data(), body.count, 1.0);
            if (gap.gap < kSkin) return false;
        }
        // And a side of each through a side of the other, which is the one pair the
        // corner tests cannot see: two shapes lying across each other with every
        // corner of both outside the other's outline is not possible for convex
        // shapes, but a side passing exactly through a corner is, and it is the state
        // a body put down flush against another one lands in.
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 a = wanted[i], b = wanted[(i + 1) % body.count];
            double along = 2.0;
            Vec3 normal{0, 1, 0};
            if (segmentHitsPolygon(a, b, solid.position.data(), solid.count, along, normal)) return false;
        }
    }
    return true;
}

// Out of whatever a shape is inside, by its position and nothing else.
//
// Every other answer in this room is an impulse: the surface stops what is coming
// at it, and what is coming at it is a speed. That works because a contact is met
// on the way *in*. A corner that is already inside something was never met on the
// way in -- it is there because the walk could not see it coming, because the shape
// was put there by the mouse, or because the arithmetic that put it on a surface
// left it a nanometre the wrong side. A constraint on speed has nothing to say about
// it: "do not close" is already satisfied by something that is not closing.
//
// So it is answered where it is: the shape is moved out along the way it is least
// inside, by the depth it is inside. Nothing is added to its speed and nothing is
// taken from it, so no energy is made; the two shapes share the move by their
// inertia, so a heavy one gives less ground than a light one; and a shape asleep is
// not moved at all, because it is not the one that moved into anything.
void PlaygroundRoom::separateBodies() {
    for (std::size_t one = 0; one < kBodyCount; ++one) {
        Body& body = bodies_[one];
        if (body.count == 0 || body.frozen || bodyHeld(one)) continue;
        // The floor.
        double deepest = 0.0;
        for (std::size_t i = 0; i < body.count; ++i) deepest = std::max(deepest, -body.position[i].y);
        if (deepest > 0.0) {
            body.centre.y += deepest;
            placeCorners(body);
        }
        // Another shape.
        for (std::size_t two = 0; two < kBodyCount; ++two) {
            if (two == one || bodies_[two].count == 0) continue;
            Body& solid = bodies_[two];
            // How much of the move this shape has to make: by inverse mass, so the
            // pair share it the way two bodies share any push between them. A shape
            // asleep or in the mouse's hand is not going anywhere, and takes none.
            const bool solidMoves = !solid.frozen && !bodyHeld(two);
            const double mine = 1.0 / body.mass;
            const double theirs = solidMoves ? 1.0 / solid.mass : 0.0;
            const double share = mine / (mine + theirs);
            for (std::size_t i = 0; i < body.count; ++i) {
                const ball_shape::OutlineGap gap = ball_shape::outlineGap(body.position[i], solid.position.data(),
                                                                         solid.position.data(), solid.count, 1.0);
                if (!(gap.gap < 0.0)) continue;
                const Vec3 move = gap.normal * (-gap.gap);
                body.centre = body.centre + move * share;
                if (solidMoves) solid.centre = solid.centre - move * (1.0 - share);
                placeCorners(body);
                if (solidMoves) placeCorners(solid);
            }
            // And the same for two shapes meeting along their edges, where no corner
            // of either is inside the other: the pair is moved apart along the axis
            // it is least inside on.
            {
                pair_shape::PairManifold meeting;
                if (pair_shape::separate(body.position.data(), body.count, solid.position.data(), solid.count,
                                         meeting) &&
                    meeting.overlap > 0.0) {
                    const auto inside = [](const Vec3* from, std::size_t count, const Vec3* outline,
                                           std::size_t outlineCount) {
                        for (std::size_t i = 0; i < count; ++i)
                            if (ball_shape::outlineGap(from[i], outline, outline, outlineCount, 1.0).gap < 0.0)
                                return true;
                        return false;
                    };
                    const bool cornersInside =
                        inside(body.position.data(), body.count, solid.position.data(), solid.count) ||
                        inside(solid.position.data(), solid.count, body.position.data(), body.count);
                    if (!cornersInside) {
                        const Vec3 move = meeting.normal * meeting.overlap;
                        body.centre = body.centre + move * share;
                        if (solidMoves) solid.centre = solid.centre - move * (1.0 - share);
                        placeCorners(body);
                        if (solidMoves) placeCorners(solid);
                    }
                }
            }
            // And the other way round: a corner of the other shape inside this one
            // is the same overlap seen from the other side.
            for (std::size_t i = 0; i < solid.count; ++i) {
                const ball_shape::OutlineGap gap = ball_shape::outlineGap(solid.position[i], body.position.data(),
                                                                         body.position.data(), body.count, 1.0);
                if (!(gap.gap < 0.0)) continue;
                const Vec3 move = gap.normal * (-gap.gap);
                if (solidMoves) solid.centre = solid.centre + move * (1.0 - share);
                body.centre = body.centre - move * share;
                if (solidMoves) placeCorners(solid);
                placeCorners(body);
            }
        }
    }
}


// Nothing in this room ends a tick inside anything -- the ball included.
//
// A body is answered by the sweep before it steps, and by separateBodies() after,
// so a shape that arrives somewhere it may not be is either stopped or moved out.
// The ball has the same two halves and needs the same second one, because there is
// one thing its sweep cannot see: a shape that *turns* into it. The ball's sweep
// subtracts the shape's travel, which is exact for a shape that slides, and says
// nothing about a corner coming round -- a triangle spinning at a radian and a half
// a second walks its own corners through a ball while its middle has barely moved.
// A ball that a spinning corner has just swept through must not be left there.
//
// So it is answered where it is, exactly as a shape inside another shape is: moved
// out along the way out of the ball, by the depth it is inside, the pair sharing the
// move by inverse mass, and nothing added to anybody's speed. A ball against the
// floor cannot be given ground and the floor takes the whole of the move, for the
// same reason nothing in this room can be below it.
// A shape may not be carried through the ball.
//
// The bodies are stepped before the ball, so between the contact solve and the
// ball's own tick there is nobody looking at the two of them. The contact solve
// answers the speed they are closing at along the normal of the contact point the
// tick *began* with -- and a shape that turns swings that point along its own
// outline, so the surface it actually carries into the ball is somewhere else by the
// end of the tick. A triangle rocking at one radian a second put a ball thirty three
// millimetres inside itself that way, and a hard one eighty seven; what the ball then
// did about it was the recovery, and the recovery is a kick.
//
// So the shape's step is stopped where the meeting is. This is the same hitscan the
// ball's own sweep uses, asked the other way round: the ball is standing where it is
// (it has not been stepped yet), the outline travels its tick, and what comes back is
// the fraction of the tick at which the two first touch. The shape is carried that
// far and no further, and its angle with it -- the body is a rigid thing and half of
// its motion is its turn.
//
// Nothing is added to anything by this. The shape is put where it would have been if
// it had stopped when it hit something, which is where a contact would have stopped
// it: no speed is changed, so no energy is made, and the ball's own tick then finds
// it exactly where a contact belongs and answers it as one.
bool PlaygroundRoom::pushBodyOutOfBall(std::size_t index, Vec3 normal, double through) {
    if (index >= kBodyCount || !(through > 0.0)) return false;
    Body& body = bodies_[index];
    if (body.count == 0 || bodyHeld(index)) {
        return false;
    }
    const Vec3 was = body.centre;
    // How far the shape already reaches below the floor, which is a state it may
    // keep -- it was put there by its own step and the separation above -- but may
    // not make worse. Measured against that, not against zero: a corner a micrometre
    // under the floor is not a reason to refuse, or the shape could never give way
    // about anything again.
    double below = 0.0;
    for (std::size_t i = 0; i < body.count; ++i)
        below = std::max(below, playground_tick::kSurfaceY - body.position[i].y);
    // Out of the ball means along -normal, because the normal runs from the shape to
    // the ball: the shape leaves the way the ball would have.
    body.centre = body.centre - normal * through;
    placeCorners(body);
    // The floor is under everything, so a shape cannot be the one to give way if
    // giving way would take it further through the floor. What is left is the ball
    // moving, which is the honest last resort.
    for (std::size_t i = 0; i < body.count; ++i) {
        if (playground_tick::kSurfaceY - body.position[i].y > below + playground_tick::kQuantum) {
            body.centre = was;
            placeCorners(body);
            return false;
        }
    }
    // And the motion that carried it in.
    //
    // Putting the shape back where it would have stopped is only free if nothing
    // was moving: a body lifted by its place, with the speed that took it there
    // still in it, is a body handed the height and keeping the speed -- work done
    // by moving a point, which is the one thing in this room that must never
    // happen. It is not a small slip either: it is repeated every tick, because a
    // shape resting on the ball arrives at the ball again every tick and is lifted
    // out again, and a cube that sits on a ball's crown with its middle a hand's
    // width off the crown was given its own fall back sixty times a second --
    // spinning faster and faster on the crown of a ball it never moved, with the
    // height it was standing at never changing. The energy it collected was real
    // and came from nowhere, which is the whole of what a conjured force is.
    //
    // So a placement answers the speed that made it necessary, along the way it was
    // made: what was going into the ball stops at the ball. That is a contact doing
    // its job, and it costs nothing -- the shape keeps every part of its motion
    // that the ball did not refuse.
    const double closing = dot(body.velocity, normal);
    if (closing > 0.0) body.velocity = body.velocity - normal * closing;
    ++bodyStepClamped_;
    return true;
}

void PlaygroundRoom::separateBall() {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        Vec3 way{};
        Vec3 at{};
        const double gap = bodyBallGap(index, way, at);
        if (!(gap < 0.0)) continue;
        // The shape gives way, and it gives way *first*, because it is the shape that
        // is where it should not be. Splitting the overlap by mass share -- which is
        // what this used to do -- handed three quarters of it to the ball, because
        // the ball is the lighter of the two, and 75 % of a two-centimetre overlap
        // is a ball moved fifteen millimetres in a tick by nothing at all. That is
        // the teleport; this room does not do teleports.
        //
        // So `way`, which runs out of the ball and into the body, is the direction
        // the body leaves along, and pushBodyOutOfBall does the moving -- including
        // refusing when the body has nowhere to go, because it is on the floor.
        if (pushBodyOutOfBall(index, way * -1.0, -gap)) continue;
        // Nowhere to go: the ball gives the rest of it, and only what is left.
        if (ball_.frozen || dragTarget_ == DragTarget::Ball) continue;
        const Vec3 wanted = ball_.position - way * (-gap);
        if (wanted.y < ball_.radius - kSkin) continue;
        // Counted, because this is the room moving the ball by hand: the shape had
        // nowhere to go and the ball was the only thing left that could.
        ++ballFoundThrough_;
        ball_.position = wanted;
    }
}

}  // namespace gtc::native
