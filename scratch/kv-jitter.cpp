// Not speed -- place. Every tick's position of everything, at full precision, and the
// peak-to-peak range of it over the last stretch. A thing that is at rest has a range
// of zero; anything else is a wobble the eye can see even when the speed is nil.
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
    const double footX = argc > 1 ? std::atof(argv[1]) : -0.25;
    const double ballX = argc > 2 ? std::atof(argv[2]) : -0.18;
    const double ballY = argc > 3 ? std::atof(argv[3]) : 0.60;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    r.placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {footX + 1.4, 0.7, 0};
    r.placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {ballX, ballY, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    double cx0 = 1e9, cx1 = -1e9, cy0 = 1e9, cy1 = -1e9, tx0 = 1e9, tx1 = -1e9, ty0 = 1e9, ty1 = -1e9;
    double ta0 = 1e9, ta1 = -1e9;
    for (int k = 0; k < 1000; ++k) {
        r.advance(kDt);
        if (k >= 800) {
            minX = std::min(minX, r.ball_.position.x); maxX = std::max(maxX, r.ball_.position.x);
            minY = std::min(minY, r.ball_.position.y); maxY = std::max(maxY, r.ball_.position.y);
            cx0 = std::min(cx0, r.bodies_[0].centre.x); cx1 = std::max(cx1, r.bodies_[0].centre.x);
            cy0 = std::min(cy0, r.bodies_[0].centre.y); cy1 = std::max(cy1, r.bodies_[0].centre.y);
            tx0 = std::min(tx0, r.bodies_[1].centre.x); tx1 = std::max(tx1, r.bodies_[1].centre.x);
            ty0 = std::min(ty0, r.bodies_[1].centre.y); ty1 = std::max(ty1, r.bodies_[1].centre.y);
            ta0 = std::min(ta0, r.bodies_[1].angle); ta1 = std::max(ta1, r.bodies_[1].angle);
        }
        if (k >= 995)
            std::printf("t%d ball (%.17g, %.17g) prev (%.17g, %.17g) a%+.15g w%+.15g v(%+.12g,%+.12g) frz%d | cube (%.17g, %.17g) a%+.15g | tri (%.17g, %.17g) a%+.15g\n",
                        k, r.ball_.position.x, r.ball_.position.y, r.ball_.previous.x, r.ball_.previous.y, r.ball_.angle, r.ball_.spin,
                        r.ball_.velocity.x, r.ball_.velocity.y, int(r.ball_.frozen),
                        r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].angle,
                        r.bodies_[1].centre.x, r.bodies_[1].centre.y, r.bodies_[1].angle);
    }
    std::printf("over ticks 800..999 -- ball range x %.17g m, y %.17g m\n", maxX - minX, maxY - minY);
    std::printf("cube range x %.17g y %.17g | triangle range x %.17g y %.17g a %.17g\n",
                cx1 - cx0, cy1 - cy0, tx1 - tx0, ty1 - ty0, ta1 - ta0);
    std::printf("repairs: through %zu clamped %zu lifted %zu\n", r.ballFoundThroughCount(), r.bodyStepClampedCount(), r.ballLiftedOutCount());
    return 0;
}
