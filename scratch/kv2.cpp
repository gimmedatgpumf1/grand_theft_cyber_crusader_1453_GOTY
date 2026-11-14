#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cstdio>
#include <cstdlib>
using namespace gtc::native;
int main(int argc, char** argv) {
    const double cx = argc > 1 ? std::atof(argv[1]) : -0.45;
    const double bx = argc > 2 ? std::atof(argv[2]) : 0.12;
    const double by = argc > 3 ? std::atof(argv[3]) : 0.80;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(1.0 / 20.0);
    auto& c = r.bodies_[0];
    c.centre = {cx, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    PlaygroundRoom::placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {-0.25 + 1.4, 0.7, 0};
    PlaygroundRoom::placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {bx, by, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < 600; ++k) {
        const Vec3 before = r.bodies_[1].centre;
        if (k >= 520) std::printf("TICK %d\n", k);
        r.advance(1.0 / 20.0);
        if (k >= 520) {
            const Vec3 after = r.bodies_[1].centre;
            double low = 1e9;
            for (std::size_t i = 0; i < r.bodies_[1].count; ++i) low = std::min(low, r.bodies_[1].position[i].y);
            std::printf("   [end] tri low %+.9e", low);
            std::printf(" tri d(%+.4e,%+.4e) v(%+.4e,%+.4e) w%+.4e | ball (%.9f,%.9f) v(%+.3e,%+.3e)\n",
                        after.x - before.x, after.y - before.y, r.bodies_[1].velocity.x, r.bodies_[1].velocity.y,
                        r.bodies_[1].spin, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y);
        }
    }
    return 0;
}
