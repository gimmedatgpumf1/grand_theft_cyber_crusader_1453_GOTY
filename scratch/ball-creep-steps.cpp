#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
using namespace gtc::native;
int main() {
    const double dt = 1.0 / 20.0;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 40; ++k) r.advance(dt);
    const auto putBody = [&](std::size_t i, Vec3 centre, double angle) {
        auto& b = r.bodies_[i];
        b.centre = centre; b.angle = angle; b.velocity = {}; b.spin = 0; b.frozen = false;
        PlaygroundRoom::placeCorners(b);
        b.previous = b.position; b.previousCentre = b.centre; b.previousAngle = b.angle;
    };
    putBody(0, {-0.90, 0.35, 0}, 0.0);
    putBody(1, {1.15, 0.7, 0}, 0.5 * 3.14159265358979323846);
    r.ball_.position = {0.33, 0.14, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k <= 1800; ++k) {
        const Vec3 before = r.ball_.position;
        const Vec3 triBefore = r.bodies_[1].centre;
        r.advance(dt);
        if (k >= 1700 && k < 1710) {
            std::printf("t%-4d ball moved (%+.4e,%+.4e) = %.3e m | v*dt (%+.3e) | tri moved (%+.4e,%+.4e) = %.3e m\n", k,
                        r.ball_.position.x - before.x, r.ball_.position.y - before.y,
                        std::hypot(r.ball_.position.x - before.x, r.ball_.position.y - before.y),
                        r.ball_.velocity.x * dt, r.bodies_[1].centre.x - triBefore.x,
                        r.bodies_[1].centre.y - triBefore.y,
                        std::hypot(r.bodies_[1].centre.x - triBefore.x, r.bodies_[1].centre.y - triBefore.y));
        }
    }
    return 0;
}
