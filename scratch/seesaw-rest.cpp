// The King's seesaw: a ball let go *on* the plank, not dropped onto it. The plank
// should tip smoothly under a load that is riding it.
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
    // On the plank, halfway out along the raised end, resting: one radius above the
    // top face, which is the local line y = +kSeesawHalfThick.
    const double c = std::cos(s.angle), sn = std::sin(s.angle);
    const double lx = 0.55, ly = gtc::native::playground_detail::kSeesawHalfThick + r.ball_.radius;
    r.ball_.position = {s.centre.x + c * lx - sn * ly, s.centre.y + sn * lx + c * ly, 0.0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    std::printf("tick  angle     d(step)   accel      applied   ball load   ball y     vy      gap\n");
    double prevStep = 0.0, prevAngle = s.angle;
    for (int k = 0; k < 40; ++k) {
        r.advance(0.05);
        const double step = s.angle - prevAngle;
        prevAngle = s.angle;
        const double accel = (step - prevStep) / 0.05;
        prevStep = step;
        double load = 0.0;
        for (std::size_t i = 0; i < s.appliedCount; ++i)
            if (s.applied[i].kind == PlaygroundRoom::ForceKind::Ball)
                load = std::max(load, length(s.applied[i].impulse) / 0.05);
        const double gap =
            ball_shape::outlineGap(r.ball_.position, s.position.data(), s.position.data(), s.count, 1.0).gap - r.ball_.radius;
        std::printf("%3d  %+.5f  %+.6f  %+.3f      %zu        %.2f N    %.4f  %+.3f  %+.5f  spin %+.4f ballv(%+.3f,%+.3f) bs %+.3f clamped %zu\n",
                    k, s.angle, step, accel, s.appliedCount, load, r.ball_.position.y, r.ball_.velocity.y, gap,
                    s.spin, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin, r.bodyStepClamped_);
    }
    return 0;
}
