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
        const double gap =
            ball_shape::outlineGap(r.ball_.position, s.position.data(), s.position.data(), s.count, 1.0).gap - r.ball_.radius;
        const double startGap =
            ball_shape::outlineGap(r.ball_.previous, s.previous.data(), s.previous.data(), s.count, 1.0).gap - r.ball_.radius;
        for (std::size_t i = 0; i < s.appliedCount; ++i) {
            const auto& a = s.applied[i];
            std::printf("      APPLY k%d at (%+.4f,%+.4f) j(%+.4f,%+.4f) |j| %.2f N\n", int(a.kind), a.at.x, a.at.y,
                        a.impulse.x, a.impulse.y, length(a.impulse) / 0.05);
        }
        std::printf("      plank");
        for (std::size_t i = 0; i < s.count; ++i) std::printf(" (%+.3f,%+.3f)", s.position[i].x, s.position[i].y);
        std::printf("  ball (%+.3f,%+.3f)\n", r.ball_.position.x, r.ball_.position.y);
        std::printf("%3d  %+.5f  %+.6f  %+.3f      %zu applied, ball load %.2f N   ball y %.4f vy %+.3f gap %+.5f "
                    "start %+.5f spin %+.4f\n",
                    k, s.angle, step, accel, s.appliedCount, load, r.ball_.position.y, r.ball_.velocity.y, gap, startGap,
                    s.spin);
    }
    return 0;
}
