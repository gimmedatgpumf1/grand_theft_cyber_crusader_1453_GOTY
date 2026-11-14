// What the ball does, in the four things a ball has to do.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace { constexpr double kDt = 1.0 / 20.0; }
int main() {
    // 1. Dropped on the floor: it bounces, and never higher than it fell.
    {
        PlaygroundRoom r; r.reset();
        r.ball_.position = {-4.6, 1.6, 0}; r.ball_.previous = r.ball_.position;
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
        double top = -1e30, firstLow = 1e30, afterTop = -1e30;
        for (int k = 0; k < 120; ++k) {
            r.advance(kDt);
            top = std::max(top, r.ball_.position.y - r.ball_.radius);
            if (k < 20) firstLow = std::min(firstLow, r.ball_.position.y - r.ball_.radius);
            if (k > 25) afterTop = std::max(afterTop, r.ball_.position.y - r.ball_.radius);
        }
        std::printf("1. dropped 1.48 m: lowest %+.4f, came back to %+.4f m (%.1f%% of the drop)\n", firstLow,
                    afterTop, 100.0 * afterTop / 1.48);
    }
    // 2. Rolled at the triangle's slope: it is turned, not eaten.
    {
        PlaygroundRoom r; r.reset();
        r.ball_.position = {-0.2, r.ball_.radius, 0}; r.ball_.previous = r.ball_.position;
        r.ball_.velocity = {-2.0, 0.0, 0}; r.ball_.spin = 0.0; r.ball_.frozen = false;
        double closest = r.ball_.position.x, deepest = 0.0;
        for (int k = 0; k < 100; ++k) {
            r.advance(kDt);
            const auto g = ball_shape::outlineGap(r.ball_.position, r.bodies_[1].position.data(),
                                                  r.bodies_[1].position.data(), r.bodies_[1].count, 1.0);
            deepest = std::max(deepest, r.ball_.radius - g.gap);
            closest = std::min(closest, r.ball_.position.x);
        }
        std::printf("2. rolled at the triangle: cut to x %+.3f, deepest inside it %.9f m, v (%+.3f,%+.3f)\n",
                    closest, deepest, r.ball_.velocity.x, r.ball_.velocity.y);
    }
    // 3. At rest on the floor: does it stay put?
    {
        PlaygroundRoom r; r.reset();
        r.ball_.position = {-4.6, r.ball_.radius, 0}; r.ball_.previous = r.ball_.position;
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
        for (int k = 0; k < 60; ++k) r.advance(kDt);
        const Vec3 a = r.ball_.position;
        for (int k = 0; k < 100; ++k) r.advance(kDt);
        std::printf("3. at rest on the floor: moved %.9f m in 5 s, frozen %d, vy %+.6f\n",
                    length(r.ball_.position - a), int(r.ball_.frozen), r.ball_.velocity.y);
    }
    // 4. Rolled along the floor: the grip is real, and it does not stop dead.
    {
        PlaygroundRoom r; r.reset();
        r.ball_.position = {-4.7, r.ball_.radius, 0}; r.ball_.previous = r.ball_.position;
        r.ball_.velocity = {-2.0, 0.0, 0}; r.ball_.spin = 0.0; r.ball_.frozen = false;
        double travelled = 0.0; Vec3 last = r.ball_.position;
        for (int k = 0; k < 100; ++k) {
            r.advance(kDt);
            travelled += length(r.ball_.position - last);
            last = r.ball_.position;
        }
        std::printf("4. rolled at 2.00 m/s: travelled %.3f m in 5 s, now v (%+.3f,%+.3f) w %+.3f\n", travelled,
                    r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin);
    }
    return 0;
}
