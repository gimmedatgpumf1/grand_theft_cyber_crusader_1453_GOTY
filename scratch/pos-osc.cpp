// Vibration is a place that keeps coming back: count how often the per-tick step
// reverses. A thing settling once changes sign a few times; a thing buzzing changes
// sign on most ticks, however small the speed is at the tick boundary.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace gtc::native;
namespace { constexpr double kDt = 1.0 / 20.0; }
struct Osc { int flipsX = 0, flipsY = 0; double ampX = 0, ampY = 0; double lastX = 0, lastY = 0; };
static void feed(Osc& o, double dx, double dy) {
    if (dx * o.lastX < 0) ++o.flipsX;
    if (dy * o.lastY < 0) ++o.flipsY;
    if (dx != 0) o.lastX = dx;
    if (dy != 0) o.lastY = dy;
    o.ampX = std::max(o.ampX, std::abs(dx));
    o.ampY = std::max(o.ampY, std::abs(dy));
}
int main() {
    int worstFlips = 0;
    char name[160] = "";
    for (double cx : {-0.90, -0.75, -0.60, -0.45})
        for (double bx = -0.45; bx <= 0.30; bx += 0.03)
            for (double by : {0.14, 0.20, 0.26, 0.34, 0.45, 0.60, 0.80}) {
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
                Osc ball, cube, tri;
                Vec3 wasBall = r.ball_.position, wasCube = r.bodies_[0].centre, wasTri = r.bodies_[1].centre;
                for (int k = 0; k < 600; ++k) {
                    r.advance(kDt);
                    const Vec3 b = r.ball_.position, cc = r.bodies_[0].centre, tt = r.bodies_[1].centre;
                    if (k >= 500) {
                        feed(ball, b.x - wasBall.x, b.y - wasBall.y);
                        feed(cube, cc.x - wasCube.x, cc.y - wasCube.y);
                        feed(tri, tt.x - wasTri.x, tt.y - wasTri.y);
                    }
                    wasBall = b; wasCube = cc; wasTri = tt;
                }
                const int flips = std::max({ball.flipsX, ball.flipsY, cube.flipsX, cube.flipsY, tri.flipsX, tri.flipsY});
                if (flips > worstFlips) {
                    worstFlips = flips;
                    std::snprintf(name, sizeof name, "cube %+.2f ball (%+.2f,%+.2f)", cx, bx, by);
                }
                if (flips >= 25)
                    std::printf("BUZZ cube %+.2f ball (%+.2f,%+.2f): flips ball %d/%d amp %.2e/%.2e | cube %d/%d amp %.2e/%.2e | tri %d/%d amp %.2e/%.2e\n",
                                cx, bx, by, ball.flipsX, ball.flipsY, ball.ampX, ball.ampY, cube.flipsX, cube.flipsY,
                                cube.ampX, cube.ampY, tri.flipsX, tri.flipsY, tri.ampX, tri.ampY);
            }
    std::printf("most reversals in 100 ticks anywhere: %d  [%s]\n", worstFlips, name);
    return 0;
}
