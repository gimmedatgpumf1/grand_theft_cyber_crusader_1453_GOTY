// One arrangement, every tick, in metres: where the ball and the shapes actually are.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace gtc::native;
int main(int argc, char** argv) {
    const double cx = argc > 1 ? std::atof(argv[1]) : -0.90;
    const double bx = argc > 2 ? std::atof(argv[2]) : 0.27;
    const double by = argc > 3 ? std::atof(argv[3]) : 0.14;
    const int total = argc > 4 ? std::atoi(argv[4]) : 1600;
    const int from = argc > 5 ? std::atoi(argv[5]) : 1200;
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
    for (int k = 0; k < total; ++k) {
        r.advance(1.0 / 20.0);
        if (k + 1 < from) continue;
        const auto& b = r.ball_;
        const auto& p = r.bodies_[2];
        std::printf("t%4d ball (%.9f,%.9f) v(%+.3e,%+.3e) w%+.2e | cube (%.9f,%.9f) v(%+.2e,%+.2e) | tri (%.9f,%.9f) v(%+.2e,%+.2e) w%+.2e | plank (%.9f,%.9f) a%+.9f v(%+.2e,%+.2e) w%+.2e\n",
                    k + 1, b.position.x, b.position.y, b.velocity.x, b.velocity.y, b.spin,
                    r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].velocity.x, r.bodies_[0].velocity.y,
                    r.bodies_[1].centre.x, r.bodies_[1].centre.y, r.bodies_[1].velocity.x, r.bodies_[1].velocity.y, r.bodies_[1].spin,
                    p.centre.x, p.centre.y, p.angle, p.velocity.x, p.velocity.y, p.spin);
    }
    return 0;
}
