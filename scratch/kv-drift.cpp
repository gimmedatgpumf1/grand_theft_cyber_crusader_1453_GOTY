// Does the ball in the King's corner actually move, or does it only hold a speed
// it never spends? Position every 20 ticks, and the largest step there is.
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
    const double footX = argc > 1 ? std::atof(argv[1]) : -0.25;
    const double ballX = argc > 2 ? std::atof(argv[2]) : -0.18;
    const double by = argc > 3 ? std::atof(argv[3]) : 0.13;
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    r.placeCorners(c);
    c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {footX + 1.4, 0.7, 0};
    r.placeCorners(t);
    t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {ballX, by, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    Vec3 was = r.ball_.position;
    double biggest = 0.0, lateTravel = 0.0, lateV = 0.0;
    for (int k = 0; k < 400; ++k) {
        r.advance(kDt);
        const double moved = length(r.ball_.position - was);
        was = r.ball_.position;
        if (k > 200) { lateTravel += moved; biggest = std::max(biggest, moved); lateV = std::max(lateV, length(r.ball_.velocity)); }
        if (k % 50 == 49)
            std::printf("t%3d ball (%+.6f,%+.6f) v(%+.6f,%+.6f) w%+.4f frz%d | cube (%+.6f,%+.6f) v(%+.6f,%+.6f) | tri (%+.6f,%+.6f) v(%+.6f,%+.6f)\n",
                        k, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin, int(r.ball_.frozen),
                        r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].velocity.x, r.bodies_[0].velocity.y,
                        r.bodies_[1].centre.x, r.bodies_[1].centre.y, r.bodies_[1].velocity.x, r.bodies_[1].velocity.y);
    }
    std::printf("over the last 200 ticks: travel %.9f m, biggest step %.9f m, worst |v| %.6f\n", lateTravel, biggest, lateV);
    return 0;
}
