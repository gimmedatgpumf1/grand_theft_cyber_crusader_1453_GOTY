#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
using namespace gtc::native;
int main() {
    const double dt = 1.0 / 20.0;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 40; ++k) r.advance(dt);
    const auto putBody = [&](std::size_t i, Vec3 centre, double angle) {
        auto& b = r.bodies_[i];
        b.centre = centre; b.angle = angle; b.velocity = {}; b.spin = 0; b.frozen = false;
        PlaygroundRoom::placeCorners(b);
        b.previous = b.position; b.previousCentre = b.centre; b.previousAngle = b.angle;
    };
    putBody(0, {-0.90, 0.35, 0}, 0.0);
    putBody(1, {1.15, 0.7, 0}, 0.5 * 3.14159265358979323846);
    r.ball_.position = {0.33, 0.14, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k <= 2400; ++k) {
        const Vec3 before = r.ball_.position;
        const Vec3 triBefore = r.bodies_[1].centre;
        const std::size_t clampedBefore = r.bodyStepClamped_;
        const std::size_t foundBefore = r.ballFoundThrough_;
        r.advance(dt);
        if (k >= 2360 && k <= 2400) {
            std::printf("t%-4d ball moved (%+.4e,%+.4e) = %.3e m | v*dt (%+.3e) | tri moved (%+.4e,%+.4e) = %.3e m\n", k,
                        r.ball_.position.x - before.x, r.ball_.position.y - before.y,
                        std::hypot(r.ball_.position.x - before.x, r.ball_.position.y - before.y),
                        r.ball_.velocity.x * dt, r.bodies_[1].centre.x - triBefore.x,
                        r.bodies_[1].centre.y - triBefore.y,
                        std::hypot(r.bodies_[1].centre.x - triBefore.x, r.bodies_[1].centre.y - triBefore.y));
            std::printf("     clamps %zu foundThrough %zu\n", std::size_t(r.bodyStepClamped_ - clampedBefore),
                        std::size_t(r.ballFoundThrough_ - foundBefore));
            std::printf("     ball (%.6f,%.6f) r %.3f floorY %.6f | supN (%+.3f,%+.3f) | touches %zu", r.ball_.position.x,
                        r.ball_.position.y, r.ball_.radius, r.ball_.radius + 1e-6, r.ball_.supportNormal.x,
                        r.ball_.supportNormal.y, r.ballTouchCount_);
            for (std::size_t i = 0; i < r.ballTouchCount_; ++i)
                std::printf(" [%zu body %zu n (%+.3f,%+.3f)]", i, r.ballTouches_[i].body, r.ballTouches_[i].normal.x,
                            r.ballTouches_[i].normal.y);
            std::printf(" tri v (%+.2e,%+.2e) w %+.2e frozen %d angle %.4f\n", r.bodies_[1].velocity.x,
                        r.bodies_[1].velocity.y, r.bodies_[1].spin, r.bodies_[1].frozen ? 1 : 0,
                        r.bodies_[1].angle);
            const auto& t = r.bodies_[1];
            std::printf("     tri centre (%.4f,%.4f) rest %d plane %d onBody %d supF %d lo %.4f hi %.4f | corners", t.centre.x,
                        t.centre.y, t.restTicks, t.touchingPlane ? 1 : 0, t.onBody ? 1 : 0, t.supportFrozen ? 1 : 0,
                        t.supportLow, t.supportHigh);
            for (std::size_t i = 0; i < t.count; ++i) std::printf(" (%.3f,%.3f)", t.position[i].x, t.position[i].y);
            std::printf("\n     cube rest %d frozen %d v (%.2e,%.2e)\n", r.bodies_[0].restTicks,
                        r.bodies_[0].frozen ? 1 : 0, r.bodies_[0].velocity.x, r.bodies_[0].velocity.y);
        }
    }
    return 0;
}
