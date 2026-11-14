// Anything still moving its PLACE after thirty seconds of being left alone.
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
int main() {
    int bad = 0;
    for (double cx : {-0.90, -0.60})
        for (double bx = -0.45; bx <= 0.35; bx += 0.06)
            for (double by : {0.14, 0.25, 0.40, 0.70}) {
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
                Vec3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
                Vec3 clo = lo, chi = hi, tlo = lo, thi = hi;
                double vmax = 0;
                for (int k = 0; k < 1600; ++k) {
                    r.advance(kDt);
                    if (k < 1400) continue;
                    const auto take = [](Vec3& l, Vec3& h, Vec3 p) {
                        l.x = std::min(l.x, p.x); l.y = std::min(l.y, p.y);
                        h.x = std::max(h.x, p.x); h.y = std::max(h.y, p.y);
                    };
                    take(lo, hi, r.ball_.position);
                    take(clo, chi, r.bodies_[0].centre);
                    take(tlo, thi, r.bodies_[1].centre);
                    vmax = std::max({vmax, length(r.ball_.velocity), length(r.bodies_[0].velocity), length(r.bodies_[1].velocity)});
                }
                const double wob = std::max({hi.x - lo.x, hi.y - lo.y, chi.x - clo.x, chi.y - clo.y, thi.x - tlo.x, thi.y - tlo.y});
                if (wob > 1e-9) {
                    ++bad;
                    std::printf("STILL MOVING %.3e m after 70 s: cube %+.2f ball (%+.2f,%+.2f) | ball %.2e %.2e | cube %.2e %.2e | tri %.2e %.2e | worst v %.2e\n",
                                wob, cx, bx, by, hi.x - lo.x, hi.y - lo.y, chi.x - clo.x, chi.y - clo.y, thi.x - tlo.x, thi.y - tlo.y, vmax);
                }
            }
    std::printf("%d of 132 arrangements still moving after 70 s\n", bad);
    return 0;
}
