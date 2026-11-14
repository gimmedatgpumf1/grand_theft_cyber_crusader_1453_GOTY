// Is the plank's turn smooth tick to tick, or does its acceleration switch on and
// off every other tick? A seesaw loaded by a ball should accelerate evenly.
#include "common.h"
#include "client/playground_pair.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
int main() {
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(0.05);
    auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
    s.frozen = false;
    s.angle = 0.40;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    s.spin = 0.0;
    s.velocity = {};
    r.ball_.frozen = false;
    r.ball_.position = {-3.1600, 1.5121, 0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.previous = r.ball_.position;
    std::printf("tick  angle     d(step)   accel      ball-on-plank  ball y\n");
    double prevStep = 0.0, prevAngle = s.angle;
    for (int k = 0; k < 30; ++k) {
        r.advance(0.05);
        const double step = s.angle - prevAngle;
        prevAngle = s.angle;
        const double accel = (step - prevStep) / 0.05;
        prevStep = step;
        // Was the ball pressing on the plank this tick, as the ledger saw it?
        double load = 0.0;
        for (std::size_t i = 0; i < s.appliedCount; ++i)
            if (s.applied[i].kind == PlaygroundRoom::ForceKind::Ball)
                load = std::max(load, length(s.applied[i].impulse) / 0.05);
        std::printf("%3d  %+.5f  %+.6f  %+.3f      %zu applied, ball load %.2f N   ball y %.4f\n", k, s.angle, step,
                    accel, s.appliedCount, load, r.ball_.position.y);
    }
    return 0;
}
