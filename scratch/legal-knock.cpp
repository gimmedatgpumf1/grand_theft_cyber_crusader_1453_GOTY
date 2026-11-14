// The room as it is actually played: nothing placed underground, nothing put down
// inside anything.
//
// The King's picture is a shape knocked over and a plank lying across it. Whatever he
// did to get there, he could not have put either of them through the floor or inside
// the other -- the room does not allow it. So this does what a hand can do and nothing
// more: lift the triangle, tilt it, let it go; then drop the plank across it; then the
// cube on the plank. Every tick the total energy is counted, and any tick that finds
// more of it than the tick before is printed with everything that was moving.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr double kDt = 1.0 / 20.0;

double energyOf(const PlaygroundRoom& r) {
    double kinetic = 0.0, potential = 0.0;
    r.energyBudget(kinetic, potential);
    return kinetic + potential;
}

struct Watch {
    double start = 0.0;
    double peak = 0.0;
    double worstJump = 0.0;
    int worstTick = -1;
    double jumpAt(int tick) const { return tick == worstTick ? worstJump : 0.0; }
};

Watch follow(PlaygroundRoom& r, int ticks, bool show) {
    Watch watch;
    watch.start = watch.peak = energyOf(r);
    for (int k = 0; k < ticks; ++k) {
        const double before = energyOf(r);
        r.advance(kDt);
        const double now = energyOf(r);
        if (now - before > watch.worstJump) {
            watch.worstJump = now - before;
            watch.worstTick = k;
        }
        if (now - before > 0.05 && show) {
            std::printf("   t%3d energy %+.3f J -> %.2f J\n", k, now - before, now);
            for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
                const auto& b = r.bodies_[i];
                if (!b.count) continue;
                std::printf("      body %zu a%+.4f v(%+.3f,%+.3f) w%+.3f frz%d c(%+.3f,%+.3f)\n", i, b.angle,
                            b.velocity.x, b.velocity.y, b.spin, int(b.frozen), b.centre.x, b.centre.y);
            }
            std::printf("      ball (%+.3f,%+.3f) v(%+.3f,%+.3f) frz%d\n", r.ball_.position.x, r.ball_.position.y,
                        r.ball_.velocity.x, r.ball_.velocity.y, int(r.ball_.frozen));
        }
        watch.peak = std::max(watch.peak, now);
    }
    return watch;
}
}  // namespace

int main(int argc, char** argv) {
    const bool show = argc > 1;
    // 1. The room left alone. Nothing should ever gain anything.
    {
        PlaygroundRoom r;
        r.reset();
        const Watch w = follow(r, 1200, show);
        std::printf("1. left alone 60 s            | worst single-tick gain %+.4f J (t%d) | peak above start %+.4f J\n",
                    w.worstJump, w.worstTick, w.peak - w.start);
    }
    // 2. The triangle lifted, tilted, and let go.
    for (int variant = 0; variant < 6; ++variant) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 30; ++i) r.advance(kDt);
        auto& t = r.bodies_[PlaygroundRoom::kTriangle];
        // Lifted clear of the floor first, then tilted: never through it.
        t.centre = {-1.900, 2.4 + 0.25 * double(variant), 0};
        t.angle = -0.5 - 0.35 * double(variant);
        PlaygroundRoom::placeCorners(t);
        double lowest = 1e9;
        for (std::size_t i = 0; i < t.count; ++i) lowest = std::min(lowest, t.position[i].y);
        if (lowest < 0.0) t.centre.y -= lowest;  // put it above the floor, never through
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        t.previousAngle = t.angle;
        t.velocity = {};
        t.spin = 0.0;
        t.frozen = false;
        const Watch w = follow(r, 600, show);
        std::printf("2.%d triangle tilted %+.2f and dropped | worst single-tick gain %+.4f J (t%d) | peak above start %+.4f J\n",
                    variant, t.angle, w.worstJump, w.worstTick, w.peak - w.start);
    }
    // 3. And then the plank let go across whatever the triangle came to rest as.
    for (int variant = 0; variant < 6; ++variant) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 30; ++i) r.advance(kDt);
        auto& t = r.bodies_[PlaygroundRoom::kTriangle];
        t.centre = {-1.900, 2.6, 0};
        t.angle = -0.5 - 0.35 * double(variant);
        PlaygroundRoom::placeCorners(t);
        double lowest = 1e9;
        for (std::size_t i = 0; i < t.count; ++i) lowest = std::min(lowest, t.position[i].y);
        if (lowest < 0.0) t.centre.y -= lowest;
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        t.previousAngle = t.angle;
        t.velocity = {};
        t.spin = 0.0;
        t.frozen = false;
        follow(r, 200, false);
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        s.angle = -0.3 + 0.25 * double(variant);
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.spin = 0.0;
        s.frozen = false;
        const Watch w = follow(r, 600, show);
        std::printf("3.%d plank tilted %+.2f across it     | worst single-tick gain %+.4f J (t%d) | peak above start %+.4f J\n",
                    variant, s.angle, w.worstJump, w.worstTick, w.peak - w.start);
    }
    return 0;
}
