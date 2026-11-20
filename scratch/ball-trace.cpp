#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cstdio>
int main() {
    constexpr double kDt = 1.0 / 20.0;
    PlaygroundRoom r; r.reset();
    r.ball_.position = {2.2, 1.6, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    for (int k = 0; k < 100; ++k) {
        r.advance(kDt);
        std::printf("t%02d h %+.4f vy %+.3f\n", k, r.ball_.position.y - r.ball_.radius, r.ball_.velocity.y);
    }
    return 0;
}
