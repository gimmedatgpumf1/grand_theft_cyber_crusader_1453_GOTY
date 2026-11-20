#include "common.h"
#include <cstdio>
#include <cmath>
static double energy(PlaygroundRoom& r) {
    double e = 0.0;
    for (int i = 0; i < 2; ++i) {
        const PlaygroundRoom::Body& b = r.bodies_[i];
        if (b.count == 0) continue;
        e += b.mass * playground_tick::kGravity * b.centre.y;
        e += 0.5 * b.mass * dot(b.velocity, b.velocity) + 0.5 * b.moment * b.spin * b.spin;
    }
    e += r.ball_.mass * playground_tick::kGravity * r.ball_.position.y;
    e += 0.5 * r.ball_.mass * dot(r.ball_.velocity, r.ball_.velocity);
    e += 0.5 * 0.4 * r.ball_.mass * r.ball_.radius * r.ball_.radius * r.ball_.spin * r.ball_.spin;
    e += playground_tick::kMass * playground_tick::kGravity * r.point_.y;
    e += 0.5 * playground_tick::kMass * dot(r.velocity_, r.velocity_);
    return e;
}
int main() {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
    r.ball_.previous = r.ball_.position;
    r.point_ = {0.30 + 0.12, 1.40, 0};
    r.velocity_ = {}; r.pointFrozen_ = false; r.previous_ = r.point_;
    const double start = energy(r);
    double peak = start; int peakTick = 0;
    for (int t = 1; t <= 200; ++t) {
        r.advance(1.0 / 20.0);
        const double e = energy(r);
        if (e > peak) { peak = e; peakTick = t; }
        if (t >= 9 && t <= 11)
            std::printf("t%2d E%+.4f rise%+.4f | dot(%.4f,%.4f) v(%+.3f,%+.3f) | ball(%.4f,%.4f) v(%+.3f,%+.3f) frz%d | cube v(%+.3f,%+.3f) frz%d | tri v(%+.3f,%+.3f) frz%d\n",
                        t, e, e - start, r.point_.x, r.point_.y, r.velocity_.x, r.velocity_.y,
                        r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, int(r.ball_.frozen),
                        r.bodies_[0].velocity.x, r.bodies_[0].velocity.y, int(r.bodies_[0].frozen),
                        r.bodies_[1].velocity.x, r.bodies_[1].velocity.y, int(r.bodies_[1].frozen));
    }
    std::printf("peak +%.4f J at tick %d\n", peak - start, peakTick);
    return 0;
}
