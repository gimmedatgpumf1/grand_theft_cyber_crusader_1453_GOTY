// The King's corner, set up honestly: the ball is put down clear of both faces and
// let fall into the notch. Does it come to rest, and with what speed left over?
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace {
constexpr double kDt = 1.0 / 20.0;
}
int main() {
    for (double bx : {-0.30, -0.24, -0.18, -0.12, -0.06, 0.00, 0.06}) {
        for (double by : {0.40, 0.60, 0.90}) {
            PlaygroundRoom r;
            r.reset();
            for (int k = 0; k < 20; ++k) r.advance(kDt);
            auto& c = r.bodies_[0];
            c.centre = {-0.60, 0.35, 0};
            c.angle = 0.0;
            c.velocity = {}; c.spin = 0.0; c.frozen = false;
            r.placeCorners(c);
            c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
            auto& t = r.bodies_[1];
            t.angle = 0.5 * 3.14159265358979323846;
            t.centre = {-0.25 + 1.4, 0.7, 0};
            r.placeCorners(t);
            t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
            r.ball_.position = {bx, by, 0};
            r.ball_.previous = r.ball_.position;
            r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
            double peak = 0.0, travel = 0.0;
            Vec3 was = r.ball_.position;
            for (int k = 0; k < 400; ++k) {
                r.advance(kDt);
                if (k > 200) {
                    peak = std::max(peak, length(r.ball_.velocity));
                    travel += length(r.ball_.position - was);
                }
                was = r.ball_.position;
            }
            std::printf("     travel over the last 200 ticks: %.9f m\n", travel);
            Vec3 way{}, at{};
            const double g0 = r.bodyBallGap(0, way, at);
            const double g1 = r.bodyBallGap(1, way, at);
            std::printf("  from (%+.2f,%+.2f): settles (%+.4f,%+.4f) v(%+.5f,%+.5f) |v| %.5f w%+.3f frz%d | gaps cube%+.4f tri%+.4f floor%+.4f | worst late |v| %.4f\n",
                        bx, by, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                        length(r.ball_.velocity), r.ball_.spin, int(r.ball_.frozen), g0, g1,
                        r.ball_.position.y - r.ball_.radius, peak);
        }
    }
    return 0;
}
