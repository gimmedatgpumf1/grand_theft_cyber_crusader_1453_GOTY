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
        // Out of the shape, along the way that is nearest, and never through the
        // floor. The floor is a plane and the dot is a ball: its middle can never be
        // lower than its own radius above it. A ball squeezed between a shape and
        // the floor is caught; the floor is not negotiable, so what is left of the
        // push is sideways along the floor.
        Vec3 normalUsed = gap.normal;
        point_ = point_ + normalUsed * (kPointRadius - gap.gap);
        if (point_.y < kSurfaceY + kPointRadius) {
            point_.y = kSurfaceY + kPointRadius;
            normalUsed = Vec3{0, 1, 0};
        }
        if (dot(velocity_, normalUsed) < 0.0) velocity_ = velocity_ - normalUsed * dot(velocity_, normalUsed);
        // Being beside a shape is not standing on it: only a face with an upward
        // part is something the dot's weight can rest on.
        // A sleeping dot is not woken here. What wakes it is a shape arriving at
        // it, which is asked and answered in the room's own tick; waking it for
        // every hair of separation would be waking it every tick it lies in
        // contact with anything, which is every tick it is asleep on a shape.
    }
}

}  // namespace gtc::native
