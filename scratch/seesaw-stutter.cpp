// Is the plank's motion one smooth swing, or does it stop and start?
//
// The eye says stop-move-stop-move. A tick is a fiftieth of a second and a real
// swing is smooth, so each tick's angle should differ from the last by a little.
// Anything that moves, stops, moves, stops is a stutter, and this prints it.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>
int main() {
    const double dt = 1.0 / 20.0;
    for (int which = 0; which < 2; ++which) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 40; ++i) r.advance(dt);
        auto& s = r.bodies_[2];
        s.frozen = false;
        s.angle = 0.40;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.spin = 0.0;
        s.velocity = {};
        if (which == 1) {
            r.ball_.frozen = false;
            r.ball_.position = {-3.1600, 1.2721 + 0.2400, 0};
            r.ball_.velocity = {};
            r.ball_.spin = 0.0;
            r.ball_.previous = r.ball_.position;
        }
        std::printf("%s\n", which == 0 ? "A: plank alone, let go at 0.40 rad" : "B: plank with a ball on the raised end");
        double prev = s.angle;
        int refused = 0;
        for (int k = 0; k < 60; ++k) {
            const double spinWas = s.spin;
            r.advance(dt);
            const double step = s.angle - prev;
            prev = s.angle;
            const double predicted = spinWas * dt;
            const bool short1 = std::abs(predicted) > 1e-9 && std::abs(step / predicted) < 0.9;
            if (short1) ++refused;
            std::printf("  t%2d  a %+.5f  step %+.6f  (spin said %+.6f, %4.0f%%)  spin %+.3f %s\n", k, s.angle, step,
                        predicted, std::abs(predicted) > 1e-9 ? 100.0 * step / predicted : 100.0, s.spin,
                        short1 ? "<-- REFUSED" : "");
        }
        std::printf("  ticks refused a part of their turn: %d of 60\n", refused);
    }
    return 0;
}
