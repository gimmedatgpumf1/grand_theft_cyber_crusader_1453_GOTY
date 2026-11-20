// Is the restitution actually delivered?
//
// Restitution is a statement about the *pair*: the speed at which the two are closing
// along the face's normal must come back out as e times that speed, separating. What
// the ball alone does is not the measure -- a light ball bounced off a free shape
// keeps less of it, because the shape took the rest and is moving away with it.
// So this measures the closing speed, relative to the face itself, before and after.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace {
constexpr double kDt = 1.0 / 20.0;
struct Out { double arrived = 0.0, left = 0.0; };
// The nearest point on body `index`'s outline to `at`, and its speed there.
Vec3 faceSpeed(const PlaygroundRoom& r, int index, Vec3 at, Vec3& nearest) {
    if (index < 0) return Vec3{0, 0, 0};
    const auto& body = r.bodies_[std::size_t(index)];
    double best = 1e30;
    for (std::size_t i = 0; i < body.count; ++i) {
        const Vec3 a = body.position[i];
        const Vec3 b = body.position[(i + 1) % body.count];
        const Vec3 side = b - a;
        const double l2 = dot(side, side);
        const double t = l2 > 0.0 ? std::clamp(dot(at - a, side) / l2, 0.0, 1.0) : 0.0;
        const Vec3 p = a + side * t;
        const double d = length(at - p);
        if (d < best) { best = d; nearest = p; }
    }
    // The speed of that point: the body's own, plus the arc of its turn about its middle.
    const Vec3 arm = nearest - body.centre;
    return body.velocity + Vec3{-body.spin * arm.y, body.spin * arm.x, 0.0};
}
Out drop(PlaygroundRoom& r, Vec3 at, Vec3 normal, int bodyIndex) {
    r.ball_.position = at;
    r.ball_.previous = at;
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    double arrived = 0.0, left = 0.0;
    bool met = false;
    for (int k = 0; k < 80; ++k) {
        Vec3 nearest = at;
        const Vec3 before = faceSpeed(r, bodyIndex, r.ball_.position, nearest);
        const double closingBefore = dot(r.ball_.velocity - before, normal);
        r.advance(kDt);
        const double closingAfter = dot(r.ball_.velocity - faceSpeed(r, bodyIndex, r.ball_.position, nearest), normal);
        if (!met && closingAfter > 0.0 && closingBefore < -0.05) {
            met = true;
            arrived = closingBefore;
            left = closingAfter;
        }
        if (met) left = std::max(left, closingAfter);
    }
    return Out{arrived, left};
}
}
int main() {
    { PlaygroundRoom r; r.reset();
      for (int k = 0; k < 20; ++k) r.advance(kDt);
      const auto o = drop(r, {2.20, 1.60, 0}, {0, 1, 0}, -1);
      std::printf("  the floor:       closing %+.2f m/s, separating %+.2f m/s (%.0f%% of e=%.2f)\n",
                  o.arrived, o.left, 100.0 * o.left / -o.arrived, 87.0); }
    { PlaygroundRoom r; r.reset();
      for (int k = 0; k < 20; ++k) r.advance(kDt);
      const auto& c = r.bodies_[0];
      const auto o = drop(r, {c.centre.x, c.centre.y + 0.35 + 0.12 + 0.60, 0}, {0, 1, 0}, 0);
      std::printf("  the cube's top:  closing %+.2f m/s, separating %+.2f m/s (%.0f%%)\n", o.arrived, o.left,
                  100.0 * o.left / -o.arrived); }
    { PlaygroundRoom r; r.reset();
      for (int k = 0; k < 20; ++k) r.advance(kDt);
      const Vec3 n{0.7071067811865476, 0.7071067811865476, 0.0};
      const auto o = drop(r, Vec3{-1.55, 1.05, 0} + n * (0.12 + 0.60), n, 1);
      std::printf("  the hypotenuse:  closing %+.2f m/s, separating %+.2f m/s (%.0f%%)\n", o.arrived, o.left,
                  100.0 * o.left / -o.arrived); }
    { PlaygroundRoom r; r.reset();
      for (int k = 0; k < 20; ++k) r.advance(kDt);
      const Vec3 n{0.7071067811865476, 0.7071067811865476, 0.0};
      const auto o = drop(r, Vec3{-2.60 + 0.20, 2.10 + 1.60, 0}, n, 1);  // the King's drop
      std::printf("  the King's drop: closing %+.2f m/s, separating %+.2f m/s (%.0f%%)\n", o.arrived, o.left,
                  100.0 * o.left / -o.arrived); }
    return 0;
}
