// The scene as the room opens: where is the ball, and what does the plank do?
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
    auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
    std::printf("reset: plank angle %+.4f frozen %d | ball (%.4f,%.4f) frozen %d | pin (%.3f,%.3f)\n", s.angle,
                s.frozen ? 1 : 0, r.ball_.position.x, r.ball_.position.y, r.ball_.frozen ? 1 : 0, s.centre.x, s.centre.y);
    const double gap =
        ball_shape::outlineGap(r.ball_.position, s.position.data(), s.position.data(), s.count, 1.0).gap - r.ball_.radius;
    std::printf("ball gap to plank at reset: %+.5f\n", gap);
    double prevStep = 0.0, prevAngle = s.angle;
    for (int k = 0; k < 60; ++k) {
        r.advance(1.0 / 20.0);
        const double step = s.angle - prevAngle;
        prevAngle = s.angle;
        const double accel = (step - prevStep) / 0.05;
        prevStep = step;
        std::printf("%3d angle %+.5f step %+.6f accel %+.3f | ball (%.4f,%.4f) v(%+.3f,%+.3f) frozen %d\n", k, s.angle,
                    step, accel, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                    r.ball_.frozen ? 1 : 0);
    }
    return 0;
}
