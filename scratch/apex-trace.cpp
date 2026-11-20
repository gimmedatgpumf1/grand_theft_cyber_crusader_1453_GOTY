#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cstdio>
int main() {
    constexpr double kDt = 1.0 / 20.0;
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    r.ball_.position = {-2.40, 3.70, 0};
    r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    for (int k = 0; k < 22; ++k) {
        r.advance(kDt);
        std::printf("t%02d ball (%+.3f,%+.3f) v (%+.3f,%+.3f) w %+.2f\n", k, r.ball_.position.x, r.ball_.position.y,
                    r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin);
    }
    return 0;
}
