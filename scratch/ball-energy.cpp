// Energy must not go up when the dot, or a shape, is dropped on the ball.
#include "common.h"
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
    double worst = -1e9;
    // The dot onto the ball, straight on its crown and on its shoulder.
    for (double dx : {0.0, 0.06, 0.12}) {
        PlaygroundRoom r; r.reset();
        for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
        r.ball_.position = {0.30, r.ball_.radius, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
        r.ball_.previous = r.ball_.position;
        r.point_ = {0.30 + dx, 1.40, 0};
        r.velocity_ = {}; r.pointFrozen_ = false; r.previous_ = r.point_;
        const double start = energy(r);
        double peak = start;
        for (int t = 1; t <= 200; ++t) { r.advance(1.0 / 20.0); peak = std::max(peak, energy(r)); }
        const double rise = peak - start;
        worst = std::max(worst, rise);
        std::printf("dot dropped %+.2f m off the ball's crown: worst rise %+.6f J\n", dx, rise);
    }
    // A cube onto the ball.
    {
        PlaygroundRoom r; r.reset();
        for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
        r.ball_.position = {0.30, r.ball_.radius, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
        r.ball_.previous = r.ball_.position;
        PlaygroundRoom::Body& cube = r.bodies_[0];
        cube.centre = {0.78, 2.0, 0};
        cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
        PlaygroundRoom::placeCorners(cube);
        cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
        const double start = energy(r);
        double peak = start;
        for (int t = 1; t <= 200; ++t) { r.advance(1.0 / 20.0); peak = std::max(peak, energy(r)); }
        const double rise = peak - start;
        worst = std::max(worst, rise);
        std::printf("cube dropped on the ball:                        worst rise %+.6f J\n", rise);
    }
    std::printf("worst of all: %+.6f J\n", worst);
    return 0;
}
