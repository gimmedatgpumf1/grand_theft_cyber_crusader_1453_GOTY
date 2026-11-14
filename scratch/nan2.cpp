#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace gtc::native;
int main(int argc, char** argv) {
    const double foot = argc > 1 ? std::atof(argv[1]) : -0.25;
    const double bx = argc > 2 ? std::atof(argv[2]) : 0.06;
    const double by = argc > 3 ? std::atof(argv[3]) : 0.60;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(1.0/20);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    r.placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5*3.14159265358979323846; t.centre = {foot+1.4, 0.7, 0};
    r.placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {bx, by, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < 400; ++k) {
        r.advance(1.0/20);
        if (true)
            std::printf("t%03d ball (%+.6f,%+.6f) v(%+.5f,%+.5f) w%+.3f frz%d | cube c(%+.5f,%+.5f) v(%+.4f,%+.4f) | tri c(%+.5f,%+.5f) a%+.5f v(%+.4f,%+.4f) w%+.4f\n",
                k, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin, int(r.ball_.frozen),
                r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].velocity.x, r.bodies_[0].velocity.y,
                r.bodies_[1].centre.x, r.bodies_[1].centre.y, r.bodies_[1].angle, r.bodies_[1].velocity.x, r.bodies_[1].velocity.y, r.bodies_[1].spin);
    }
    return 0;
}
