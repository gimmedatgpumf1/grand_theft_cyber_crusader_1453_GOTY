// Place, not speed. For every arrangement the King has been shown, the peak-to-peak
// range of every object's position over the last stretch of ticks, in metres, at full
// precision -- and the largest single-tick jump. A body at rest has a range of exactly
// zero; anything else is what the eye calls a vibration.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
using namespace gtc::native;
namespace { constexpr double kDt = 1.0 / 20.0; }
struct Range { double lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9}; };
static void take(Range& r, const Vec3& p) {
    const double v[3] = {p.x, p.y, p.z};
    for (int i = 0; i < 3; ++i) { r.lo[i] = std::min(r.lo[i], v[i]); r.hi[i] = std::max(r.hi[i], v[i]); }
}
static double worst(const Range& r) {
    double w = 0.0;
    for (int i = 0; i < 3; ++i) w = std::max(w, r.hi[i] - r.lo[i]);
    return w;
}
int main() {
    struct Case { const char* name; };
    // 1. the notch, 21 drop positions.
    for (double bx : {-0.30, -0.24, -0.18, -0.12, -0.06, 0.00, 0.06})
        for (double by : {0.40, 0.60, 0.90}) {
            PlaygroundRoom r; r.reset();
            for (int k = 0; k < 20; ++k) r.advance(kDt);
            auto& c = r.bodies_[0];
            c.centre = {-0.60, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
            PlaygroundRoom::placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
            auto& t = r.bodies_[1];
            t.angle = 0.5 * 3.14159265358979323846; t.centre = {-0.25 + 1.4, 0.7, 0};
            PlaygroundRoom::placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
            r.ball_.position = {bx, by, 0}; r.ball_.previous = r.ball_.position;
            r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
            Range ball, c0, c1;
            for (int k = 0; k < 1000; ++k) {
                r.advance(kDt);
                if (k >= 800) { take(ball, r.ball_.position); take(c0, r.bodies_[0].centre); take(c1, r.bodies_[1].centre); }
            }
            std::printf("notch (%+.2f,%+.2f): ball %.3e | cube %.3e | tri %.3e  (m peak-to-peak, last 200 ticks)\n",
                        bx, by, worst(ball), worst(c0), worst(c1));
        }
    return 0;
}
