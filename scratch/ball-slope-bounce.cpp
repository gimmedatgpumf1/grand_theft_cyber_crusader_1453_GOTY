// The ball let go above the triangle's hypotenuse: does it come off the face, and how
// far, and how much of its arrival is given back along the face's own normal.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
int main() {
    constexpr double kDt = 1.0 / 20.0;
    const Vec3 n{0.7071067811865476, 0.7071067811865476, 0.0};
    for (double along = 0.25; along <= 0.76; along += 0.25) {
        for (double up : {0.30, 0.90, 1.80}) {
            PlaygroundRoom r;
            r.reset();
            for (int k = 0; k < 20; ++k) r.advance(kDt);
            const Vec3 onFace{-2.60 + 2.10 * along, 0.00 + 2.10 * along, 0};
            r.ball_.position = onFace + n * (r.ball_.radius + up);
            r.ball_.previous = r.ball_.position;
            r.ball_.velocity = {};
            r.ball_.spin = 0.0;
            r.ball_.frozen = false;
            double arrived = 0.0, left = 0.0, best = 0.0;
            bool hit = false;
            int hitTick = -1;
            for (int k = 0; k < 90; ++k) {
                const double before = dot(r.ball_.velocity, n);
                r.advance(kDt);
                const double after = dot(r.ball_.velocity, n);
                if (!hit && r.ball_.touching && before < 0.0) {
                    hit = true;
                    hitTick = k;
                    arrived = before;
                }
                if (hit && after > left) left = after;
                // How far off the face's line the ball gets in the first few ticks
                // after the meeting: that is the bounce, in metres, on the screen.
                if (hit && k <= hitTick + 6) best = std::max(best, dot(r.ball_.position - onFace, n) - r.ball_.radius);
            }
            std::printf("  along %.2f from %.1f m up: arrived %+.2f, left %+.2f m/s along the normal (%.0f%%), "
                        "lifted %+.3f m off the face line\n",
                        along, up, arrived, left, arrived < 0 ? 100.0 * left / -arrived : 0.0, best);
        }
    }
    return 0;
}
