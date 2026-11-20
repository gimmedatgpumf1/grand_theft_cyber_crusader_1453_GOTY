// Is the seesaw actually reacting?
//
// A plank on an axle has to answer what is put on it. This drops a cube on the raised
// end from three heights, rolls a ball onto it, and sets a load gently down, and prints
// the angle tick by tick together with the fraction of its turn the axle is allowing.
// A fraction that is nought while the plank is free is the plank refusing to move.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr double S = PlaygroundRoom::kSeesaw;
constexpr double kDt = 1.0 / 20.0;

PlaygroundRoom settled() {
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 20; ++i) r.advance(kDt);
    return r;
}

void show(const PlaygroundRoom& r, int t) {
    const auto& s = r.bodies_[S];
    const auto& c = r.bodies_[0];
    const auto& b = r.ball_;
    std::printf("t%-3d plank a%+.5f w%+8.4f | allowed(+%.4f rad/s) %.6f -%.6f | cube (%+.3f,%+.3f) v(%+.2f,%+.2f) | ball (%+.3f,%+.3f)\n",
                t, s.angle, s.spin, s.spin * kDt, r.pivotTurnAllowed(s, s.spin * kDt),
                r.pivotTurnAllowed(s, -s.spin * kDt), c.centre.x, c.centre.y, c.velocity.x, c.velocity.y, b.position.x,
                b.position.y);
}
}  // namespace

int main() {
    std::printf("--- a cube dropped on the raised end from 0.3 m\n");
    {
        PlaygroundRoom r = settled();
        auto& s = r.bodies_[S];
        auto& c = r.bodies_[0];
        c.frozen = false;
        c.centre = {s.centre.x + 0.80, s.centre.y + 0.80 * std::sin(0.0) + 0.36 + 0.30, 0};
        c.angle = 0.0;
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        for (int t = 0; t < 40; ++t) {
            r.advance(kDt);
            if (t % 2 == 0) show(r, t);
        }
    }
    std::printf("\n--- a load set down on the raised end, already touching\n");
    {
        PlaygroundRoom r = settled();
        auto& s = r.bodies_[S];
        auto& c = r.bodies_[0];
        c.frozen = false;
        c.centre = {s.centre.x + 0.80, s.centre.y + 0.36 + 0.001, 0};
        c.angle = 0.0;
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        for (int t = 0; t < 24; ++t) {
            r.advance(kDt);
            if (t % 2 == 0) show(r, t);
        }
    }
    std::printf("\n--- the ball rolled onto the raised end\n");
    {
        PlaygroundRoom r = settled();
        auto& s = r.bodies_[S];
        r.ball_.position = {s.centre.x + 0.80, s.centre.y + 0.25, 0};
        r.ball_.velocity = {};
        r.ball_.spin = 0.0;
        r.ball_.frozen = false;
        r.ball_.previous = r.ball_.position;
        for (int t = 0; t < 40; ++t) {
            r.advance(kDt);
            if (t % 2 == 0) show(r, t);
        }
    }
    std::printf("\n--- the ball dropped on the raised end, and left for 20 s\n");
    {
        PlaygroundRoom r = settled();
        auto& s = r.bodies_[S];
        r.ball_.position = {s.centre.x + 0.80, 1.60, 0};
        r.ball_.velocity = {};
        r.ball_.spin = 0.0;
        r.ball_.frozen = false;
        r.ball_.previous = r.ball_.position;
        for (int t = 0; t < 400; ++t) {
            r.advance(kDt);
            if (t % 40 == 0) show(r, t);
        }
        const auto& b = r.ball_;
        std::printf("after 20 s: ball (%+.5f,%+.5f) v(%+.5f,%+.5f) w%+.5f frz%d | plank %+.5f w%+.5f\n", b.position.x,
                    b.position.y, b.velocity.x, b.velocity.y, b.spin, int(b.frozen), s.angle, s.spin);
    }
    return 0;
}
