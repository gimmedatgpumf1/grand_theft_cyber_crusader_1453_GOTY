#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace { constexpr double kDt = 1.0 / 20.0; }
using namespace gtc::native;
int main(int argc, char** argv) {
    const double bx = argc > 1 ? std::atof(argv[1]) : -0.06;
    const double by = argc > 2 ? std::atof(argv[2]) : 0.40;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    r.placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {-0.25 + 1.4, 0.7, 0};
    r.placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {bx, by, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < 398; ++k) {
        Vec3 was = r.ball_.position;
        r.advance(kDt);
        if (k > 393)
            std::printf("t%d moved %+.12f %+.12f -> pos(%.12f,%.12f) v(%+.8f,%+.8f) frz%d touching%d standing%d\n", k,
                        r.ball_.position.x - was.x, r.ball_.position.y - was.y, r.ball_.position.x, r.ball_.position.y,
                        r.ball_.velocity.x, r.ball_.velocity.y, int(r.ball_.frozen), int(r.ball_.touching), int(r.ball_.standing));
    }
    return 0;
}
