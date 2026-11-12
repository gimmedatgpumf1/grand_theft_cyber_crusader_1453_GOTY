#include "common.h"
static void drop(const char* what, double x, double y) {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {x, y, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    double prevV = 0.0;
    int bounces = 0;
    std::printf("%s\n", what);
    for (int t = 1; t <= 200; ++t) {
        prevV = r.ball_.velocity.y;
        r.advance(1.0 / 20.0);
        if (prevV < -0.4 && r.ball_.velocity.y > 0.1) {
            std::printf("  cube: touchingPlane %d onBody %d frozen %d contacts %d met %d\n",
                        (int)r.bodies_[0].touchingPlane, (int)r.bodies_[0].onBody,
                        (int)r.bodies_[0].frozen, r.bodies_[0].contacts, r.bodies_[0].met);
            std::printf("  bounce %d: arrived %+.3f m/s, left %+.3f m/s (ratio %.3f) at y %+.4f\n",
                        ++bounces, prevV, r.ball_.velocity.y, r.ball_.velocity.y / -prevV, r.ball_.position.y);
        }
    }
}
int main() {
    drop("ball on the floor from 1.5 m", 0.0, 1.62);
    drop("ball on the square from 1.6 m above its face", 1.395, 2.30);
    return 0;
}
