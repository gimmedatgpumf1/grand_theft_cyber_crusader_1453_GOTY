#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cstdio>
int main() {
    constexpr double kDt = 1.0 / 20.0;
    PlaygroundRoom r; r.reset();
    r.ball_.position = {-4.6, 1.6, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    for (int k = 0; k < 40; ++k) {
        r.advance(kDt);
        std::printf("t%02d y %.5f vy %+.4f h %.4f frozen %d touching %d standing %d\n", k, r.ball_.position.y,
                    r.ball_.velocity.y, r.ball_.position.y - r.ball_.radius, int(r.ball_.frozen),
                    int(r.ball_.touching), int(r.ball_.standing));
    }
    return 0;
}
