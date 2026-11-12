#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"
#include "client/playground_ball_shape.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {
using namespace playground_detail;
using namespace playground_tick;
namespace {

// How far a sleeping dot may drift from the surface it is lying on and still be
// lying on it. A micrometre: the same "these two are the same place" the walk uses.
constexpr double kPointSupportBand = 1e-6;

}  // namespace

// Whether the dot's own support has gone away underneath it.
//
// A dot goes to sleep on a floor, a shape or the ball, and while it sleeps nothing
// moves it. Take the thing it is lying on out from under it -- drag the shape, roll
// the ball away -- and it would hang in the air for ever: asleep, and wrong. So the
// surface it was resting on is asked, every tick, whether it is still there, and if
// it is not the dot wakes and falls, which is what it was about to do anyway.
bool PlaygroundRoom::pointSupportGone() const {
    if (restingOnBall_) {
        const Vec3 gap{point_.x - ball_.position.x, point_.y - ball_.position.y, 0.0};
        return length(gap) > ball_.radius + kPointSupportBand;
    }
    if (restingBody_ >= 0 && restingBody_ < int(kBodyCount)) {
        const Body& body = bodies_[std::size_t(restingBody_)];
        if (body.count == 0) return true;
        if (ball_shape::insideOutline(point_, body.position.data(), body.count)) return false;
        const ball_shape::OutlineGap gap =
            ball_shape::outlineGap(point_, body.position.data(), body.position.data(), body.count, kPointRadius);
        return gap.gap > kPointSupportBand;
    }
    return point_.y > kSurfaceY + kPointSupportBand;
}

// Whether any body's step this tick has arrived at the point.
//
// The dot is a point with a radius, and a body arriving at it is a face or a corner
// arriving at a circle -- the same question the ball's own hitscan answers, so it is
// asked here with the same geometry. What it is for is the one thing a sleeping point
// cannot do for itself: a point that is not stepped has no sweep, and a body that
// falls on it while it sleeps has nothing to stop it. The point wakes, and from then
// on its own tick -- which does have a sweep -- answers whatever met it.
bool PlaygroundRoom::pointReachedByBodies() const {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0) continue;
        // It has already overtaken the point: wherever the point is, a body's outline
        // around it is a meeting that has happened and has to be answered. A body that
        // has come to rest is not skipped here: it is asleep, but the point inside it
        // is still inside it, and a sleeping shape is exactly what a shape that landed
        // on a sleeping dot becomes -- skip it and the dot is left inside it for ever.
        if (ball_shape::insideOutline(point_, body.position.data(), body.count)) return true;
        if (body.frozen) continue;
        // And where the shape was, to where it is now: the piece of its outline the
        // step swept, and every corner of it through the same travel. A body falling a
        // quarter of a metre a tick is swept, not sampled.
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 before = body.previous[i], after = body.position[i];
            const Vec3 side = body.position[(i + 1) % body.count] - body.position[i];
            const Vec3 sideBefore = body.previous[(i + 1) % body.count] - body.previous[i];
            if (length(side) <= 0.0 && length(sideBefore) <= 0.0) continue;
            // A corner's travel, against the point's own radius.
            const Vec3 travel = after - before;
            const double lengthSquared = dot(travel, travel);
            const double along = lengthSquared > 1e-18 ? std::clamp(dot(point_ - before, travel) / lengthSquared, 0.0, 1.0) : 0.0;
            if (length(point_ - (before + travel * along)) <= kPointRadius) return true;
            // And the side swept through the tick, which is what catches a face coming
            // down flat across the point with neither of its ends anywhere near it.
            for (const double fraction : {0.0, 0.5, 1.0}) {
                const Vec3 a = body.previous[i] + travel * fraction;
                const Vec3 b = body.previous[(i + 1) % body.count] + travel * fraction;
                const Vec3 edge = b - a;
                const double edgeSquared = dot(edge, edge);
                const double place = edgeSquared > 1e-18 ? std::clamp(dot(point_ - a, edge) / edgeSquared, 0.0, 1.0) : 0.0;
                if (length(point_ - (a + edge * place)) <= kPointRadius) return true;
            }
        }
    }
    return false;
}

void PlaygroundRoom::freezeCheck() {
    // A dot at rest is a dot with nothing left to spend: it is on something, it is
    // not moving, and the surface is holding it. "On the floor" is the top of the
    // floor, which is the plane the dot's own radius makes -- the floor is a plane
    // through kSurfaceY and the dot is a ball that stands its radius clear of it.
    if (!pointFrozen_ && dragTarget_ != DragTarget::Point && resting_ && length(velocity_) < 1e-9 &&
        (point_.y <= kSurfaceY + kPointRadius || restingBody_ >= 0 || restingOnBall_)) {
        pointFrozen_ = true;
        velocity_ = {};
    }
}

// Nothing may be left inside a body. The dot's own walk keeps its disc out of
// everything it moves towards, but the shapes move in their own tick and they do
// not know the dot is there -- so a shape swinging into a dot, or settling onto
// one, can leave the disc inside it. That is repaired here, after they have moved
// and before the frame is drawn: the dot is a ball of a known radius and its disc
// stands that radius clear of every face it is beside.
//
// And the face has answered what put it there. Putting the dot back out without
// touching its speed is a ratchet: it slides a little way in every tick, is lifted
// back to the surface, and keeps the speed that drove it -- so it never comes to
// rest, its speed is never spent, and it never freezes, because it is always
// moving and never going anywhere. A surface a dot is pressed against holds it,
// and holding it means this: the part of the speed driving into the face is gone.
void PlaygroundRoom::separatePoint() {
    // The floor is a plane and the dot is a ball: its middle can never be lower
    // than its own radius above it. That is not a correction, it is where the
    // floor is -- but it is worth saying, because a ball squeezed between a shape
    // and the floor has one honest way out and it is sideways.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        const ball_shape::OutlineGap gap = ball_shape::outlineGap(
            point_, body.position.data(), body.position.data(), body.count, 1.0);
        if (!(gap.gap < kPointRadius)) continue;
        // Out of the shape, along the way that is nearest -- and never through the
        // floor, and never upwards.
        //
        // The floor is a plane and the dot is a ball: its middle can never be lower
        // than its own radius above it, so a push that would take it under the floor
        // is no push at all.
        //
        // And nothing is lifted. This is a repair, not a force: it is here for the
        // one thing the dot cannot answer for itself, which is a shape that has
        // moved onto it rather than the other way round. Repairing an overlap by
        // raising the dot in a gravity field does work that nothing did -- it is
        // energy out of nowhere, which is the one thing this room may never do. A
        // ball caught under a shape does not rise into the air: it is squeezed out
        // the side, at the height it was caught at, and that is the repair.
        Vec3 normalUsed = gap.normal;
        Vec3 target = point_ + normalUsed * (kPointRadius - gap.gap);
        // A hair of overlap is repaired where it is, straight out along the face.
        // The search below is only for the shape that has swallowed the dot whole:
        // there is no local way out of that one, and the question is which way to
        // carry it. Asking it of a rounding overlap picks whichever far face
        // happens to be cheapest to reach, and a dot sliding down a slope is
        // carried across the room to the shape's other side for a micrometre of
        // overlap.
        const bool swallowed = ball_shape::insideOutline(point_, body.position.data(), body.count);
        if (swallowed && (!(target.y >= kSurfaceY + kPointRadius) || target.y > point_.y + 1e-12)) {
            double best = 1e30;
            bool found = false;
            for (std::size_t edge = 0; edge < body.count; ++edge) {
                const Vec3 a = body.position[edge];
                const Vec3 b = body.position[(edge + 1) % body.count];
                const Vec3 side = b - a;
                const double length2 = dot(side, side);
                if (!(length2 > 1e-18)) continue;
                const double sideLength = std::sqrt(length2);
                Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
                // Out of the shape, not into it: the shape's own middle is inside.
                if (dot(body.centre - a, outward) > 0.0) outward = outward * -1.0;
                const double along = std::clamp(dot(point_ - a, side) / length2, 0.0, 1.0);
                const Vec3 candidate = a + side * along + outward * kPointRadius;
                if (!(candidate.y >= kSurfaceY + kPointRadius)) continue;
                if (candidate.y > point_.y + 1e-12) continue;
                const double move = length(candidate - point_);
                if (move < best) {
                    best = move;
                    target = candidate;
                    normalUsed = outward;
                    found = true;
                }
            }
            if (!found) {
                target = point_ + gap.normal * (kPointRadius - gap.gap);
                if (target.y < kSurfaceY + kPointRadius) {
                    target.y = kSurfaceY + kPointRadius;
                    normalUsed = Vec3{0, 1, 0};
                }
            }
        } else if (target.y < kSurfaceY + kPointRadius) {
            // A disc overlapping the underside of a shape lying on the floor has no
            // room to be pushed straight out: the floor is in the way. It is put on
            // the floor, which is where it is anyway, and the rest of the overlap is
            // the shape's -- sideways is a push it will get from its own travel.
            target.y = kSurfaceY + kPointRadius;
            normalUsed = Vec3{0, 1, 0};
        }
        point_ = target;
        if (dot(velocity_, normalUsed) < 0.0) velocity_ = velocity_ - normalUsed * dot(velocity_, normalUsed);
        // Being beside a shape is not standing on it: only a face with an upward
        // part is something the dot's weight can rest on -- and calling a wall the
        // ground takes the floor's friction away from a dot sliding along it.
        if (normalUsed.y > 0.0) {
            restingBody_ = int(index);
            resting_ = true;
            supportNormal_ = normalUsed;
        }
        // A sleeping dot is not woken here. What wakes it is a shape arriving at
        // it, which is asked and answered in the room's own tick; waking it for
        // every hair of separation would be waking it every tick it lies in
        // contact with anything, which is every tick it is asleep on a shape.
    }
}

}  // namespace gtc::native
