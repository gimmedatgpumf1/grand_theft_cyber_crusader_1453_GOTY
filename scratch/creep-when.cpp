#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
using namespace gtc::native;
int main() {
    const double kDt = 0.05;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.90, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    PlaygroundRoom::placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {-0.25 + 1.4, 0.7, 0};
    PlaygroundRoom::placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {0.27, 0.14, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    double lastLog = 0;
    for (int k = 0; k < 1600; ++k) {
        r.advance(kDt);
        const double vt = std::sqrt(dot(t.velocity, t.velocity));
        const double vb = std::sqrt(dot(r.ball_.velocity, r.ball_.velocity));
        if (k >= 1300 && k < 1320)
            std::printf("P t%-4d tri c(%.12f,%.12f) a%.12f | v(%+.3e,%+.3e) w%+.3e\n", k, t.centre.x, t.centre.y, t.angle, t.velocity.x, t.velocity.y, t.spin);
        if (false)
            std::printf("t%-4d tri v(%+.6e,%+.6e) w%+.4e | ball v(%+.6e,%+.6e) w%+.4e | cube (%+.6f,%+.6f) frz%d%d%d\n",
                        k, t.velocity.x, t.velocity.y, t.spin, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin,
                        c.centre.x, c.centre.y, c.frozen?1:0, t.frozen?1:0, r.ball_.frozen?1:0);
        (void)lastLog; (void)vb;
    }
    return 0;
}
