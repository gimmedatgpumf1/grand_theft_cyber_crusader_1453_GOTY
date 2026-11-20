#include "common.h"
#include <cstdio>
#include <cstdlib>
using namespace gtc::native;
int main(int argc, char** argv) {
    const int from = argc > 1 ? std::atoi(argv[1]) : 0;
    const int total = argc > 2 ? std::atoi(argv[2]) : 200;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 40; ++k) r.advance(1.0 / 20.0);
    r.ball_.position = {3.10, 0.12, 0};
    r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    r.resetPoint({3.10, 1.40, 0});
    for (int k = 0; k < total; ++k) {
        r.advance(1.0 / 20.0);
        if (k + 1 < from) continue;
        std::printf("t%4d dot (%+.9f,%+.9f) v(%+.4e,%+.4e) frozen %d | ball (%+.9f,%+.9f) v(%+.3e,%+.3e) w%+.3e frz %d\n", k + 1,
                    r.point().x, r.point().y, r.velocity().x, r.velocity().y, r.pointFrozen() ? 1 : 0, r.ball_.position.x,
                    r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin, r.ball_.frozen ? 1 : 0);
    }
    return 0;
}
