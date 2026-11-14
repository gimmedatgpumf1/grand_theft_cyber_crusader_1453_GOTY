#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
int main(int argc, char** argv) {
    constexpr double kDt = 1.0 / 20.0;
    const double up = argc > 1 ? std::atof(argv[1]) : 2.0;
    const double along = argc > 2 ? std::atof(argv[2]) : 0.5;
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    const Vec3 onFace{-2.60 + 2.10 * along, 0.00 + 2.10 * along, 0};
    const Vec3 normal{0.7071067811865476, 0.7071067811865476, 0.0};
    r.ball_.position = onFace + normal * (r.ball_.radius + up);
    r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    for (int k = 0; k < 70; ++k) {
        r.advance(kDt);
        const auto g = ball_shape::outlineGap(r.ball_.position, r.bodies_[1].position.data(),
                                               r.bodies_[1].position.data(), r.bodies_[1].count, 1.0);
        std::printf("t%02d pos (%+.4f,%+.4f) v (%+.3f,%+.3f) w %+.3f | gap to tri %.5f (surface %.5f) | frozen %d\n", k,
                    r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin,
                    g.gap, g.gap - r.ball_.radius, int(r.ball_.frozen));
    }
    return 0;
}
