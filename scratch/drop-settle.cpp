// A ball dropped from a metre: what it does, bounce by bounce, in metres.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
using namespace gtc::native;
int main(int argc, char** argv) {
    const double ht = argc > 1 ? std::atof(argv[1]) : 1.0;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 40; ++k) r.advance(1.0 / 20.0);
    r.ball_.position = {-0.15, ht, 0};
    r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    double low = 1e9, high = -1e9;
    int lastPeak = 0;
    double peakHeight = 0;
    bool falling = true;
    double wasY = r.ball_.position.y;
    for (int k = 0; k < 1600; ++k) {
        r.advance(1.0 / 20.0);
        const double y = r.ball_.position.y;
        low = std::min(low, y); high = std::max(high, y);
        if (k >= 1400) {
            // range over the last 10 seconds
        }
        // detect local top
        if (y > wasY && !falling) falling = true;      // rising
        if (y < wasY && falling) { if (peakHeight > 0) std::printf("peak %7.3f m at t %5.2f s (speed %.4f)\n", peakHeight, (k - lastPeak) * 0.05, 0.0); falling = false; }
        if (y > wasY && !falling) {}
        wasY = y;
        static double prev = y, prevprev = 0;
        (void)prev; (void)prevprev;
    }
    // simpler: print height at each local maximum
    PlaygroundRoom s; s.reset();
    for (int k = 0; k < 40; ++k) s.advance(1.0 / 20.0);
    s.ball_.position = {-0.15, ht, 0}; s.ball_.previous = s.ball_.position;
    s.ball_.velocity = {}; s.ball_.spin = 0; s.ball_.frozen = false;
    double a = s.ball_.position.y, b = a;
    int tops = 0;
    for (int k = 0; k < 1600; ++k) {
        s.advance(1.0 / 20.0);
        const double c = s.ball_.position.y;
        if (b > a && b >= c) {
            std::printf("bounce top %7.4f m at t %5.2f s\n", b, (k - 1) * 0.05);
            if (++tops >= 22) break;
        }
        a = b; b = c;
    }
    double lo = 1e9, hi = -1e9;
    for (int k = 0; k < 400; ++k) {
        s.advance(1.0 / 20.0);
        lo = std::min(lo, s.ball_.position.y);
        hi = std::max(hi, s.ball_.position.y);
    }
    std::printf("last 20 s: ball y range %.9f .. %.9f (%.3e m) v(%+.2e,%+.2e) w%+.2e\n", lo, hi, hi - lo,
                s.ball_.velocity.x, s.ball_.velocity.y, s.ball_.spin);
    return 0;
}
