// A plank pressed into the floor with two radians a second on it: what does the
// spin say after a tick in which the angle did not move?
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>
int main() {
    const double dt = 1.0 / 20.0;
    for (const double at : {0.0, 0.60, 0.8852, 0.8852 + 0.02}) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 40; ++i) r.advance(dt);
        auto& s = r.bodies_[2];
        s.frozen = false;
        s.angle = at;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.spin = 2.0;  // into the floor at the corner limit
        s.velocity = {};
        const double wasAngle = s.angle;
        for (int k = 0; k < 3; ++k) {
            const double a0 = s.angle, w0 = s.spin;
            r.advance(dt);
            std::printf("  a=%.3f tick%d  angle %+.5f -> %+.5f (wanted %+.5f)  spin %+.4f -> %+.4f\n", at, k, a0,
                        s.angle, w0 * dt, w0, s.spin);
        }
        std::printf("  angle moved %.5f over three ticks\n", s.angle - wasAngle);
    }
    return 0;
}
