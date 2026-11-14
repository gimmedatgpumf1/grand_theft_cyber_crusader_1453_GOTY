// Hunt for a place, not a speed: every arrangement of ball, cube and triangle in the
// notch, and any case whose *position* still moves after a long settle is reported.
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
    int found = 0;
    double worstAll = 0.0;
    char worstName[128] = "";
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
                Vec3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
                Vec3 clo{1e9, 1e9, 1e9}, chi{-1e9, -1e9, -1e9};
                Vec3 tlo{1e9, 1e9, 1e9}, thi{-1e9, -1e9, -1e9};
                for (int k = 0; k < 600; ++k) {
                    r.advance(kDt);
                    if (k < 500) continue;
                    const auto take = [](Vec3& l, Vec3& h, Vec3 p) {
                        l.x = std::min(l.x, p.x); l.y = std::min(l.y, p.y);
                        h.x = std::max(h.x, p.x); h.y = std::max(h.y, p.y);
                    };
                    take(lo, hi, r.ball_.position);
                    take(clo, chi, r.bodies_[0].centre);
                    take(tlo, thi, r.bodies_[1].centre);
                }
                const double wob = std::max({hi.x - lo.x, hi.y - lo.y, chi.x - clo.x, chi.y - clo.y,
                                             thi.x - tlo.x, thi.y - tlo.y});
                if (wob > worstAll) {
                    worstAll = wob;
                    std::snprintf(worstName, sizeof worstName, "cube %+.2f ball (%+.2f,%+.2f)", cx, bx, by);
                }
                if (wob > 1e-7) {
                    ++found;
                    std::printf("WOBBLE %.3e m: cube %+.2f ball (%+.3f,%+.3f) | ball range %.2e %.2e | cube %.2e %.2e | tri %.2e %.2e | v(%+.2e,%+.2e)\n",
                                wob, cx, bx, by, hi.x - lo.x, hi.y - lo.y, chi.x - clo.x, chi.y - clo.y,
                                thi.x - tlo.x, thi.y - tlo.y, r.ball_.velocity.x, r.ball_.velocity.y);
                }
            }
    std::printf("cases with any position wobble > 1e-7 m: %d ; worst %.3e m [%s]\n", found, worstAll, worstName);
    return 0;
}
