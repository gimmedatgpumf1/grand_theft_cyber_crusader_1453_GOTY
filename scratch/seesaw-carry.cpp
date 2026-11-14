// The plank's turn and the ball on it: does the plank push the ball (a load), and
// does the ball ever end up inside the plank?
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>
int main() {
    const double dt = 1.0 / 20.0;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(dt);
    auto& s = r.bodies_[2];
    s.frozen = false;
    s.angle = 0.40;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    s.spin = 0.0;
    s.velocity = {};
    r.ball_.frozen = false;
    r.ball_.position = {-3.1600, 1.2721 + 0.2400, 0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.previous = r.ball_.position;
    r.clearRepairCounts();
    double deepest = 0.0;
    for (int k = 0; k < 200; ++k) {
        r.advance(dt);
        // how far inside the plank the ball is, this tick
        double into = 0.0;
        for (std::size_t e = 0; e < s.count; ++e) {
            const Vec3 a = s.position[e], b = s.position[(e + 1) % s.count];
            const Vec3 side = b - a;
            const double len2 = side.x * side.x + side.y * side.y;
            const double t = len2 > 0 ? std::clamp(dot(r.ball_.position - a, side) / len2, 0.0, 1.0) : 0.0;
            const double d = length(r.ball_.position - (a + side * t));
            into = std::max(into, r.ball_.radius - d);
        }
        deepest = std::max(deepest, into);
        if (k % 10 == 0)
            std::printf("  t%.2f plank %+.4f spin %+.3f | ball (%+.3f,%+.3f) v(%+.2f,%+.2f) | into plank %.3g\n",
                        (k + 1) * 0.05, s.angle, s.spin, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x,
                        r.ball_.velocity.y, into);
    }
    std::printf("deepest the ball was inside the plank: %.3g m | lifted out %zu | found through %zu | steps clamped %zu\n",
                deepest, r.ballLiftedOutCount(), r.ballFoundThroughCount(), r.bodyStepClampedCount());
    return 0;
}
