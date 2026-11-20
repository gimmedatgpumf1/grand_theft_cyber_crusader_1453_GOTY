// A ball dropped from a height where it is *fast* when it lands. Where does it
// bounce, and how far above the face it hit is that?
#include "common.h"
static void drop(double speed) {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    // Straight above the middle of the cube's top face.
    const double v = speed;
    const double h = v * v / (2.0 * 9.8);
    r.ball_.position = {1.395, 0.35 + 0.12 + h + 0.02, 0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    std::printf("drop from %.3f m above the face (%.1f m/s at contact)\n", h, v);
    double prevV = 0.0;
    int bounces = 0;
    for (int t = 1; t <= 120; ++t) {
        prevV = r.ball_.velocity.y;
        r.advance(1.0 / 20.0);
        if (prevV < -0.4 && r.ball_.velocity.y > 0.1) {
            const double gap = ball_shape::outlineGap(r.ball_.position, r.bodies_[0].previous.data(),
                                                      r.bodies_[0].position.data(), r.bodies_[0].count, 1.0)
                                   .gap;
            std::printf("  bounce %d t%-3d arrived %+7.3f left %+6.3f (ratio %.3f) | ball y %+.4f | gap to cube %+.5f"
                        " (radius %.3f, so %.5f of air)\n",
                        ++bounces, t, prevV, r.ball_.velocity.y, r.ball_.velocity.y / -prevV,
                        r.ball_.position.y, gap, r.ball_.radius, gap - r.ball_.radius);
        }
    }
}
int main() {
    drop(4.0);
    drop(8.0);
    drop(12.0);
    return 0;
}
