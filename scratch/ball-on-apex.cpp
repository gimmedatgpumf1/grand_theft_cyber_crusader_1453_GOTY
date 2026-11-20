// The ball let go straight down onto the triangle's apex, and either side of it, from
// the height the King drops it from. Read tick by tick: where it goes, and how fast.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
int main() {
    constexpr double kDt = 1.0 / 20.0;
    // The apex is at (-2.60, 2.10). The hypotenuse runs right and down from it.
    for (double dx : {-0.20, -0.05, 0.00, 0.05, 0.20, 0.40}) {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 20; ++k) r.advance(kDt);
        const double x = -2.60 + dx;
        r.ball_.position = {x, 2.10 + 1.60, 0};
        r.ball_.previous = r.ball_.position;
        r.ball_.velocity = {};
        r.ball_.spin = 0.0;
        r.ball_.frozen = false;
        double arrivedVy = 0.0, peakUp = 0.0, peakX = 0.0;
        int contactTick = -1;
        for (int k = 0; k < 90; ++k) {
            r.advance(kDt);
            if (contactTick < 0 && r.ball_.touching) {
                contactTick = k;
                arrivedVy = r.ball_.velocity.y;
            }
            if (contactTick >= 0) {
                peakUp = std::max(peakUp, r.ball_.velocity.y);
                peakX = std::max(peakX, std::abs(r.ball_.velocity.x));
            }
        }
        std::printf("  apex %+0.2f m across: met at tick %2d, left it at vy %+.2f m/s (vx up to %+.2f), "
                    "ended at (%+.2f,%+.2f)\n",
                    dx, contactTick, peakUp, peakX, r.ball_.position.x, r.ball_.position.y);
    }
    return 0;
}
