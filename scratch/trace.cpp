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
        r.advance(dt);
        if (k >= 1390 && k % 20 == 0)
            std::printf("t%-5d ball (%.6f,%.6f) v (%+.2e) spin %+.2e frz %d | tri rest %d frz %d v (%+.2e,%+.2e)\n", k,
                        r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.spin,
                        r.ball_.frozen ? 1 : 0, r.bodies_[1].restTicks, r.bodies_[1].frozen ? 1 : 0,
                        r.bodies_[1].velocity.x, r.bodies_[1].velocity.y);
    }
    return 0;
}
