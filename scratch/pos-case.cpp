#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
using namespace gtc::native;
namespace { constexpr double kDt = 1.0 / 20.0; }
int main(int argc, char** argv) {
    const double cx = argc > 1 ? std::atof(argv[1]) : -0.60;
    const double bx = argc > 2 ? std::atof(argv[2]) : -0.33;
    const double by = argc > 3 ? std::atof(argv[3]) : 0.34;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {cx, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    PlaygroundRoom::placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {-0.25 + 1.4, 0.7, 0};
    PlaygroundRoom::placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {bx, by, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < 600; ++k) {
        const Vec3 wasBall = r.ball_.position;
        const Vec3 wasC = r.bodies_[0].centre;
        r.advance(kDt);
        if (k >= 580)
            std::printf("t%d ball (%.12f,%.12f) d(%+.3e,%+.3e) v(%+.6f,%+.6f) w%+.4f | cube (%.12f,%.12f) d(%+.3e,%+.3e) v(%+.6f,%+.6f) | tri (%.12f,%.12f) v(%+.6f,%+.6f)\n",
                        k, r.ball_.position.x, r.ball_.position.y, r.ball_.position.x - wasBall.x, r.ball_.position.y - wasBall.y,
                        r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin,
                        r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].centre.x - wasC.x, r.bodies_[0].centre.y - wasC.y,
                        r.bodies_[0].velocity.x, r.bodies_[0].velocity.y,
                        r.bodies_[1].centre.x, r.bodies_[1].centre.y, r.bodies_[1].velocity.x, r.bodies_[1].velocity.y);
    }
    std::printf("repairs: through %zu clamped %zu lifted %zu\n", r.ballFoundThroughCount(), r.bodyStepClampedCount(), r.ballLiftedOutCount());
    return 0;
}
