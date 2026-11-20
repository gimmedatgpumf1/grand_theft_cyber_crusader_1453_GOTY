// The wedge creep: whose energy is it, and is it going up or down?
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
using namespace gtc::native;
namespace { constexpr double kDt = 1.0 / 20.0; }
int main() {
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 40; ++k) r.advance(kDt);
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
        if (k >= 1300 && k % 100 == 0) {
            double ke = 0, pe = 0;
            r.energyBudget(ke, pe);
            const auto& t = r.bodies_[1];
            const auto& b = r.ball_;
            const double bKE = 0.5 * b.mass * dot(b.velocity, b.velocity) + 0.5 * (0.4 * b.mass * b.radius * b.radius) * b.spin * b.spin;
            std::printf("t%-5d total %.12f (ke %.3e pe %.6f) | ball KE %.3e | tri v(%+.3e,%+.3e) w%+.3e KE %.3e\n",
                        k, ke + pe, ke, pe, bKE, t.velocity.x, t.velocity.y, t.spin,
                        0.5 * t.mass * dot(t.velocity, t.velocity) + 0.5 * t.moment * t.spin * t.spin);
        }
        r.advance(kDt);
    }
    return 0;
}
