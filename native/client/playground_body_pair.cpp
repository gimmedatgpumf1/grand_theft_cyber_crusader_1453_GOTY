// Two bodies meeting.
//
// What is left here is the two questions a pair asks that no single body does: can
// the mouse put a shape down where it would overlap something, and are two outlines
// into each other at all. Neither of them is part of a tick: the tick's own walk is
// in playground_room_body.cpp, and it stops each body at the instant it would meet
// something, which is why nothing here ever has to put two shapes apart.

#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>

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

} // namespace gtc::native
