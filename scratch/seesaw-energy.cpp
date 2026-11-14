// Is the swing loaded by a ball losing energy, holding it, or making it? Total
// mechanical energy of the plank and the ball, tick by tick, in the drop scene.
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
    const double g = 9.8;
    const auto energy = [&]() {
        double e = 0.5 * s.mass * dot(s.velocity, s.velocity) + 0.5 * s.moment * s.spin * s.spin +
                   s.mass * g * s.centre.y;
        const double ib = 0.4 * r.ball_.mass * r.ball_.radius * r.ball_.radius;
        e += 0.5 * r.ball_.mass * dot(r.ball_.velocity, r.ball_.velocity) + 0.5 * ib * r.ball_.spin * r.ball_.spin +
             r.ball_.mass * g * r.ball_.position.y;
        return e;
    };
    const double e0 = energy();
    std::printf("plank mass %.3f kg  moment %.4f  ball mass %.3f\n", s.mass, s.moment, r.ball_.mass);
    std::printf("tick   E (J)      E-E0      spin      omega*I\n");
    for (int k = 0; k < 40; ++k) {
        r.advance(0.05);
        const double e = energy();
        std::printf("%3d  %+9.4f  %+9.4f  %+8.4f  %+8.4f\n", k, e, e - e0, s.spin, s.moment * s.spin);
    }
    return 0;
}
