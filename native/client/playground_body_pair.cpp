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

// Where a pinned body's corners are, turned about the pin.
void PlaygroundRoom::pivotCornersAt(const Body& body, double angle, std::array<Vec3, kMaxCorners>& corners) const {
    const double c = std::cos(angle), s = std::sin(angle);
    for (std::size_t i = 0; i < body.count; ++i)
        corners[i] = {body.pivotPoint.x + body.local[i].x * c - body.local[i].y * s,
                      body.pivotPoint.y + body.local[i].x * s + body.local[i].y * c, 0};
}

// How deep a pinned body would be inside anything at that angle: the floor, another
// shape, or the ball. Zero is clear.
double PlaygroundRoom::pivotDepthAt(const Body& body, double angle) const {
    std::array<Vec3, kMaxCorners> corners{};
    pivotCornersAt(body, angle, corners);
    double deepest = 0.0;
    for (std::size_t i = 0; i < body.count; ++i)
        deepest = std::max(deepest, playground_tick::kSurfaceY - corners[i].y);
    const double floorDeep = deepest;
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        const Body& solid = bodies_[other];
        if (&solid == &body || solid.count == 0) continue;
        Vec3 way{0, 1, 0};
        double amount = 0.0;
        if (outlineOverlap(corners.data(), body.count, solid.position.data(), solid.count, way, amount)) {
            if (getenv("GTC_FL3") && amount > playground_tick::kQuantum)
                std::fprintf(stderr, "PD body%zu other%zu amount%+.6f way(%+.2f,%+.2f)\n", std::size_t(&body - &bodies_[0]), other, amount, way.x, way.y);
            deepest = std::max(deepest, amount);
        }
    }
    const double gap = outlineGap(ball_.position, corners.data(), corners.data(), body.count, 1.0).gap;
    if (getenv("GTC_FL3") && ball_.radius - gap > playground_tick::kQuantum)
        std::fprintf(stderr, "PD body%zu ballgap%+.6f balldepth%+.6f floord%.6f ball(%+.3f,%+.3f)\n", std::size_t(&body - &bodies_[0]), gap, ball_.radius - gap, floorDeep, ball_.position.x, ball_.position.y);
    deepest = std::max(deepest, ball_.radius - gap);
    return deepest;
}

// The first angle of a turn that meets something, as the fraction of the turn the
// body is allowed to take.
//
// This is the hitscan. A turn is travel: the far end of a plank sweeps through an
// arc, and an arc that ends above the floor can still have passed through it on the
// way. Asking only about the end of the turn leaves the whole of the arc unexamined,
// and a plank turning fast enough was found on the far side of the floor with
// nothing having stopped it -- which is exactly what "it does not hitscan the floor"
// is. So the turn is walked in small steps, the first step that is inside something
// is bisected to the instant of the meeting, and the body stops there. Nothing is
// placed inside anything, not even for the length of one tick.
double PlaygroundRoom::pivotTurnAllowed(const Body& body, double turn) const {
    if (turn == 0.0) return 1.0;
    if (pivotDepthAt(body, body.angle) > playground_tick::kQuantum) return 0.0;
    // A step small enough that the furthest corner moves no more than a few
    // millimetres: the thinnest thing in this room is the plank itself, a quarter of
    // a metre, so a step of four millimetres cannot pass over anything.
    double furthest = 0.0;
    for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
    const double step = furthest > 0.0 ? std::min(0.25, 0.004 / furthest) : 0.25;
    const int steps = std::min(4096, std::max(1, int(std::abs(turn) / step) + 1));
    double clear = 0.0;
    for (int i = 1; i <= steps; ++i) {
        const double fraction = double(i) / double(steps);
        if (pivotDepthAt(body, body.angle + turn * fraction) <= playground_tick::kQuantum) {
            clear = fraction;
            continue;
        }
        // Between the last clear step and this one. Bisected, so the stop is the
        // instant of the meeting and not a step size.
        double low = clear, high = fraction;
        for (int bisect = 0; bisect < 60; ++bisect) {
            const double mid = 0.5 * (low + high);
            if (pivotDepthAt(body, body.angle + turn * mid) <= playground_tick::kQuantum)
                low = mid;
            else
                high = mid;
        }
        return low;
    }
    return 1.0;
}

// How deep a free body would be inside anything if it were turned where it stands:
// below the floor, into another shape, or into the ball. Zero is clear.
double PlaygroundRoom::turnDepthAt(std::size_t index, double angle) const {
    const Body& body = bodies_[index];
    const double c = std::cos(angle), s = std::sin(angle);
    std::array<Vec3, kMaxCorners> corners{};
    for (std::size_t i = 0; i < body.count; ++i)
        corners[i] = {body.centre.x + body.local[i].x * c - body.local[i].y * s,
                      body.centre.y + body.local[i].x * s + body.local[i].y * c, 0};
    return depthOfCorners(index, corners);
}

// The same question asked of a set of corners, which is where the answer actually
// lives: everything in the room is either a plane, an outline, or the ball.
double PlaygroundRoom::depthOfCorners(std::size_t index, const std::array<Vec3, kMaxCorners>& corners) const {
    const Body& body = bodies_[index];
    double deepest = 0.0;
    for (std::size_t i = 0; i < body.count; ++i)
        deepest = std::max(deepest, playground_tick::kSurfaceY - corners[i].y);
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index || bodies_[other].count == 0) continue;
        const Body& solid = bodies_[other];
        Vec3 way{0, 1, 0};
        double amount = 0.0;
        if (outlineOverlap(corners.data(), body.count, solid.position.data(), solid.count, way, amount))
            deepest = std::max(deepest, amount);
    }
    const double gap = outlineGap(ball_.position, corners.data(), corners.data(), body.count, 1.0).gap;
    return std::max(deepest, ball_.radius - gap);
}

// The rest of a turn, taken about the corner the body is standing on.
//
// A body whose corner is on the floor is a body whose corner cannot go through it,
// so a turn that would carry that corner down is not a turn about the middle at
// all: it is a turn about the contact. That is what pivoting on a surface is, and
// it is the reason a shape lying on the floor does not sink into it while it rolls
// or topples -- its middle swings up and over the point it is resting on instead.
//
// There is nothing added here and nothing taken away: the body is the same body,
// turned about a different point for the part of the tick the floor allows. Its
// speeds are untouched, so the contact still has the whole of what it was given to
// answer. Every corner on the floor is tried, because a shape lying on a face has
// two of them down and which one it turns on is decided by which way it is turning.
bool PlaygroundRoom::turnAboutFloor(std::size_t index, double rest) {
    Body& body = bodies_[index];
    const Vec3 centre = body.centre;
    const double angle = body.angle;
    const double c = std::cos(rest), s = std::sin(rest);
    bool found = false;
    Vec3 bestCentre{};
    std::array<Vec3, kMaxCorners> bestCorners{};
    double bestDeep = 1e30;
    for (std::size_t i = 0; i < body.count; ++i) {
        const Vec3 pivot = body.position[i];
        if (pivot.y > playground_tick::kSurfaceY + playground_tick::kQuantum) continue;
        const Vec3 arm = centre - pivot;
        const Vec3 moved{pivot.x + arm.x * c - arm.y * s, pivot.y + arm.x * s + arm.y * c, centre.z};
        // The body turned about that corner: its outline is the outline it had, moved.
        std::array<Vec3, kMaxCorners> corners{};
        const double turned = angle + rest;
        const double tc = std::cos(turned), ts = std::sin(turned);
        for (std::size_t k = 0; k < body.count; ++k)
            corners[k] = {moved.x + body.local[k].x * tc - body.local[k].y * ts,
                          moved.y + body.local[k].x * ts + body.local[k].y * tc, 0};
        const double deep = depthOfCorners(index, corners);
        if (deep < bestDeep) {
            bestDeep = deep;
            bestCentre = moved;
            bestCorners = corners;
            found = true;
        }
    }
    if (found && bestDeep <= playground_tick::kQuantum) {
        body.centre = bestCentre;
        body.angle = angle + rest;
        for (std::size_t k = 0; k < body.count; ++k) body.position[k] = bestCorners[k];
        return true;
    }
    body.centre = centre;
    body.angle = angle;
    placeCorners(body);
    return false;
}

// A free body's step, walked the way a turn is walked: the meeting is found, not
// arrived at and then undone.
//
// A shape falls sixteen centimetres in a tick at three metres a second, and a shape
// that is five centimetres above another one at the top of a tick is a fifth of a
// metre inside it by the end. Nothing in this room is placed inside anything, so the
// step is a piece of travel like the turn and is swept the same way: walked in steps
// small enough that the outline cannot pass over anything, the first step that is
// inside something bisected to the instant of the meeting, and the body stops there.
// What it was carrying is untouched -- its speed is still the speed it arrived with,
// and the contact next tick is what answers it. Nothing is added and nothing taken.
//
// The floor and the other shapes are asked. The ball is not: the ball has a sweep of
// its own and it is the ball's own tick that walks the shape against it, so stopping
// the shape here as well would answer the same meeting twice.
Vec3 PlaygroundRoom::sweptBodyMove(std::size_t index, Vec3 from, Vec3 to) {
    Body& body = bodies_[index];
    if (body.count == 0) return to;
    const Vec3 travel = to - from;
    const double span = length(travel);
    if (span <= playground_tick::kQuantum) return to;
    // Where it is now: a body that has begun the tick inside something is not swept
    // -- it is answered by the separation, which is the only thing that can undo it.
    const Vec3 wasCentre = body.centre;
    if (depthOfCornersExceptBall(index) > playground_tick::kQuantum) return to;
    double furthest = 0.0;
    for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
    const double step = furthest > 0.0 ? std::min(0.25, 0.004 / furthest) : 0.25;
    const int steps = std::min(4096, std::max(1, int(span / step) + 1));
    double clear = 0.0;
    for (int n = 1; n <= steps; ++n) {
        const double fraction = double(n) / double(steps);
        body.centre = from + travel * fraction;
        placeCorners(body);
        if (depthOfCornersExceptBall(index) <= playground_tick::kQuantum) {
            clear = fraction;
            continue;
        }
        double low = clear, high = fraction;
        for (int bisect = 0; bisect < 60; ++bisect) {
            const double mid = 0.5 * (low + high);
            body.centre = from + travel * mid;
            placeCorners(body);
            if (depthOfCornersExceptBall(index) <= playground_tick::kQuantum)
                low = mid;
            else
                high = mid;
        }
        body.centre = from + travel * low;
        placeCorners(body);
        (void)wasCentre;
        return body.centre;
    }
    body.centre = to;
    placeCorners(body);
    return to;
}

// How deep a body is inside the floor or another shape, with the ball left to the
// ball's own sweep.
double PlaygroundRoom::depthOfCornersExceptBall(std::size_t index) const {
    double deepest = 0.0;
    const Body& body = bodies_[index];
    for (std::size_t i = 0; i < body.count; ++i)
        deepest = std::max(deepest, playground_tick::kSurfaceY - body.position[i].y);
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index || bodies_[other].count == 0) continue;
        const Body& solid = bodies_[other];
        Vec3 way{0, 1, 0};
        double amount = 0.0;
        if (outlineOverlap(body.position.data(), body.count, solid.position.data(), solid.count, way, amount))
            deepest = std::max(deepest, amount);
    }
    return deepest;
}

// A free body's turn, walked the way its travel is.
//
// The pinned plank's turn is swept by pivotTurnAllowed(), and a free body's turn is
// travel in exactly the same sense: a corner covers an arc, and an arc that ends
// above the floor can have gone through it on the way. Nothing in this room is
// placed inside anything, not even for the length of one tick.
//
// What a free body does about a meeting is decided by what it met. The floor is the
// one surface a body can turn *on*: a corner that is down is a point that cannot
// move, so the rest of the turn goes about it and the middle swings up over it --
// that is rolling, and it is why a shape on the floor does not sink into it. Any
// other meeting stops the turn where it happens, exactly as the plank's is stopped,
// and what is left of the spin is the contact's to answer.
void PlaygroundRoom::turnBody(std::size_t index, double turn) {
    Body& body = bodies_[index];
    if (body.count == 0 || turn == 0.0) {
        placeCorners(body);
        return;
    }
    double furthest = 0.0;
    for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
    const double step = furthest > 0.0 ? std::min(0.25, 0.004 / furthest) : 0.25;
    const int steps = std::min(4096, std::max(1, int(std::abs(turn) / step) + 1));
    const double piece = turn / double(steps);
    for (int n = 0; n < steps; ++n) {
        const double was = body.angle;
        if (turnDepthAt(index, was + piece) <= playground_tick::kQuantum) {
            body.angle = was + piece;
            placeCorners(body);
            continue;
        }
        // Where the meeting is, as the fraction of this piece of the turn. The start
        // of the piece is clear -- it is where the last one ended.
        double low = 0.0, high = 1.0;
        for (int bisect = 0; bisect < 60; ++bisect) {
            const double mid = 0.5 * (low + high);
            if (turnDepthAt(index, was + piece * mid) <= playground_tick::kQuantum)
                low = mid;
            else
                high = mid;
        }
        body.angle = was + piece * low;
        placeCorners(body);
        const double rest = piece * (1.0 - low);
        if (rest == 0.0) continue;
        // Above the floor and into a shape or the ball: the turn stops there. On the
        // floor: the rest of it is taken on the corner the body is standing on, and
        // if there is no corner down -- a corner swinging onto the floor in mid air --
        // then that turn stops too, at the meeting, as everything else does.
        const bool onFloor = [&] {
            for (std::size_t i = 0; i < body.count; ++i)
                if (body.position[i].y <= playground_tick::kSurfaceY + playground_tick::kQuantum) return true;
            return false;
        }();
        if (!onFloor) continue;
        turnAboutFloor(index, rest);
    }
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
        // A pinned body is not separated from anything by moving it: it is where its
        // axle says it is, and a shape that has come to be inside a seesaw's plank is
        // answered by the contact, not by shifting the plank out of the way.
        //
        // It can still be inside something, and what gets it out is its turn.
        //
        // A pinned body has exactly one kind of place -- its angle -- so being
        // inside the floor, inside another shape or inside the ball is answered the
        // only way this body can answer anything: turn it back. That is not a
        // separate rule from every other body's, it is the same one. A free shape
        // that has ended up inside something is moved out along its position, and
        // the only position a pinned body has is which way it is turned.
        //
        // Without this the plank was exempt from the room. Its turn was integrated
        // with nothing asked about what it turned through, and nothing here could
        // give any of it back, so a plank coming down across a cube cut seven
        // centimetres into it and stayed there -- the contact underneath then spent
        // every tick pushing the cube sideways out of the way, which is the drift the
        // King saw. The plank stops where it met the cube and the axle carries the
        // rest, exactly as it would in the yard.
        if (body.pivoted && getenv("GTC_FL2")) {
            std::fprintf(stderr, "PBQ in a%+.5f deep%+.6f ball(%+.4f,%+.4f) r%.2f cube(%+.4f,%+.4f) a%+.3f tri(%+.4f,%+.4f) a%+.3f\n",
                         body.angle, pivotDepthAt(body, body.angle), ball_.position.x, ball_.position.y, ball_.radius,
                         bodies_[0].centre.x, bodies_[0].centre.y, bodies_[0].angle,
                         bodies_[1].centre.x, bodies_[1].centre.y, bodies_[1].angle);
        }
        if (body.pivoted) {
            if (0) {
                std::fprintf(stderr, "PBQ in a%+.5f deep%+.6f ball(%+.4f,%+.4f) r%.2f cube(%+.4f,%+.4f) a%+.3f tri(%+.4f,%+.4f) a%+.3f\n",
                             body.angle, pivotDepthAt(body, body.angle), ball_.position.x, ball_.position.y, ball_.radius,
                             bodies_[0].centre.x, bodies_[0].centre.y, bodies_[0].angle,
                             bodies_[1].centre.x, bodies_[1].centre.y, bodies_[1].angle);
            }
            // The floor is never negotiable. Whatever else is in the way, a plank may
            // not be through the ground, so if there is no angle that clears
            // everything, the one that clears the floor is taken: the floor is the
            // room's own surface and the axle, not the shape standing on it, is what
            // holds the plank up.
            const auto throughFloorOnly = [&](double angle) {
                std::array<Vec3, kMaxCorners> corners{};
                pivotCornersAt(body, angle, corners);
                double deepest = 0.0;
                for (std::size_t i = 0; i < body.count; ++i)
                    deepest = std::max(deepest, playground_tick::kSurfaceY - corners[i].y);
                return deepest;
            };
            const auto clear = [&](double angle) {
                return pivotDepthAt(body, angle) <= playground_tick::kQuantum;
            };
            const auto deepest = [&](double angle) { return pivotDepthAt(body, angle); };
            if (deepest(body.angle) > playground_tick::kQuantum) {
                // The least turn, either way, that takes it clear. The bound is grown
                // until something clears and the answer is then bisected, so the turn
                // given back is the smallest that does the job and not a chosen number.
                double found = -1.0;
                for (double reach : {1e-3, 1e-2, 1e-1, 0.25, 0.5, 1.0, 1.5, 3.0, 6.29}) {
                    if (clear(body.angle + reach) || clear(body.angle - reach)) {
                        found = reach;
                        break;
                    }
                }
                if (found > 0.0) {
                    const double side = clear(body.angle + found) ? 1.0 : -1.0;
                    double low = 0.0, high = found;
                    for (int step = 0; step < 60; ++step) {
                        const double mid = 0.5 * (low + high);
                        if (clear(body.angle + side * mid))
                            high = mid;
                        else
                            low = mid;
                    }
                    body.angle += side * high;
                    placeCorners(body);
                } else {
                    // Nothing inside the whole circle is clear -- it is walled in on
                    // every side. The floor still comes first.
                    double foundFloor = -1.0;
                    for (double reach : {1e-3, 1e-2, 1e-1, 0.25, 0.5, 1.0, 1.5, 3.0, 6.29}) {
                        if (throughFloorOnly(body.angle + reach) <= playground_tick::kQuantum ||
                            throughFloorOnly(body.angle - reach) <= playground_tick::kQuantum) {
                            foundFloor = reach;
                            break;
                        }
                    }
                    if (foundFloor > 0.0) {
                        const double side =
                            throughFloorOnly(body.angle + foundFloor) <= playground_tick::kQuantum ? 1.0 : -1.0;
                        double low = 0.0, high = foundFloor;
                        for (int step = 0; step < 60; ++step) {
                            const double mid = 0.5 * (low + high);
                            if (throughFloorOnly(body.angle + side * mid) <= playground_tick::kQuantum)
                                high = mid;
                            else
                                low = mid;
                        }
                        body.angle += side * high;
                        placeCorners(body);
                    }
                }
            }
            if (getenv("GTC_FL")) std::fprintf(stderr, "PB out a%+.5f\n", body.angle);
            continue;
        }
        // Another shape first; the floor has the last word below.
        // Another shape.
        for (std::size_t two = 0; two < kBodyCount; ++two) {
            if (two == one || bodies_[two].count == 0) continue;
            Body& solid = bodies_[two];
            // How much of the move this shape has to make: by inverse mass, so the
            // pair share it the way two bodies share any push between them. A shape
            // asleep or in the mouse's hand is not going anywhere, and takes none.
            const bool solidMoves = !solid.frozen && !bodyHeld(two) && !solid.pivoted;
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
    // And the floor, once, for everything in the room, after everything else has
    // had its say.
    //
    // It used to be answered first, and an overlap with another shape was then taken
    // out of the same body afterwards -- downwards, if the other shape was on top of
    // it -- with the ground it had just been put back on top of already spent. A cube
    // under a seesaw's end was pushed seventeen centimetres into the floor that way.
    // It cannot be answered body by body either: a shape that is put right in its own
    // turn can be pushed back under by the next one, because the shape it is sharing
    // an overlap with is moved after it. The floor is not a suggestion and it is not
    // one body's business: nothing in this room is below it, and that is decided here,
    // at the end, for all of them.
    for (std::size_t one = 0; one < kBodyCount; ++one) {
        Body& body = bodies_[one];
        if (body.count == 0 || body.frozen || bodyHeld(one) || body.pivoted) continue;
        double deepest = 0.0;
        for (std::size_t i = 0; i < body.count; ++i)
            deepest = std::max(deepest, playground_tick::kSurfaceY - body.position[i].y);
        if (deepest > 0.0) {
            body.centre.y += deepest;
            placeCorners(body);
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
    if (body.count == 0 || bodyHeld(index) || body.pivoted) {
        // A pinned body cannot give way -- it has nowhere to go -- so a ball that has
        // come to be inside one is answered by the ball moving, which is the honest
        // last resort and the only one left.
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
