#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace gtc::native { extern int gtcAudit; }
namespace { constexpr double kDt = 1.0/20.0; }
int main(int argc, char** argv) {
    const double footX = argc > 1 ? std::atof(argv[1]) : -0.25;
    const double ballX = argc > 2 ? std::atof(argv[2]) : -0.13;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    r.placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5*3.14159265358979323846; t.centre = {footX+1.4, 0.7, 0};
    r.placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {ballX, 0.13, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < 400; ++k) {
        auto& a = gtc::native::gtcAudit;
        a = (k >= 396) ? 1 : 0;
        if (a) std::fprintf(stderr, "===== tick %d\n", k);
        r.advance(kDt);
        a = 0;
        if (k >= 396)
            std::printf("t%03d ball (%+.7f,%+.7f) v(%+.5f,%+.5f) w%+.4f frz%d touch%d touches%zu\n", k,
                r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin,
                int(r.ball_.frozen), int(r.ball_.touching), r.ballTouchCount_);
    }
    return 0;
}
