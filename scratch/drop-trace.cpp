#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cstdio>
#include <cstdlib>
using namespace gtc::native;
int main(int argc, char** argv) {
    const double ht = argc > 1 ? std::atof(argv[1]) : 1.0;
    const double bx = argc > 3 ? std::atof(argv[3]) : -0.15;
    const int ticks = argc > 2 ? std::atoi(argv[2]) : 120;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 40; ++k) r.advance(1.0 / 20.0);
    r.ball_.position = {bx, ht, 0};
    r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < ticks; ++k) {
        r.advance(1.0 / 20.0);
        std::printf("t%3d (%5.2f s) x %.4f y %.9f v(%+.4e,%+.4e) w%+.3e\n", k + 1, (k + 1) * 0.05, r.ball_.position.x, r.ball_.position.y,
                    r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin);
    }
    return 0;
}
