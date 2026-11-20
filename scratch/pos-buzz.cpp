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
    const double cx = argc > 1 ? std::atof(argv[1]) : -0.90;
    const double bx = argc > 2 ? std::atof(argv[2]) : 0.09;
    const double by = argc > 3 ? std::atof(argv[3]) : 0.14;
    const int from = argc > 4 ? std::atoi(argv[4]) : 560;
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
    r.clearRepairCounts();
    size_t thr = 0, cl = 0, li = 0;
    for (int k = 0; k < 1600; ++k) {
        Vec3 wb = r.ball_.position, wt = r.bodies_[1].centre, wc = r.bodies_[0].centre;
        r.advance(kDt);
        if (k >= from)
            std::printf("t%d ball (%.9f,%.9f) d(%+.3e,%+.3e) v(%+.5f,%+.5f) | tri (%.9f,%.9f) d(%+.3e,%+.3e) v(%+.5f,%+.5f) w%+.4f | cube (%.9f,%.9f) d(%+.3e,%+.3e) | thr%zu cl%zu li%zu\n",
                        k, r.ball_.position.x, r.ball_.position.y, r.ball_.position.x - wb.x, r.ball_.position.y - wb.y,
                        r.ball_.velocity.x, r.ball_.velocity.y,
                        r.bodies_[1].centre.x, r.bodies_[1].centre.y, r.bodies_[1].centre.x - wt.x, r.bodies_[1].centre.y - wt.y,
                        r.bodies_[1].velocity.x, r.bodies_[1].velocity.y, r.bodies_[1].spin,
                        r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].centre.x - wc.x, r.bodies_[0].centre.y - wc.y,
                        r.ballFoundThroughCount() - thr, r.bodyStepClampedCount() - cl, r.ballLiftedOutCount() - li);
        thr = r.ballFoundThroughCount(); cl = r.bodyStepClampedCount(); li = r.ballLiftedOutCount();
    }
    return 0;
}
