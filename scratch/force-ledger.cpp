// What forces does the room write, and are they what the weights say?
#include "common.h"
static void dump(PlaygroundRoom& r, const char* when) {
    std::printf("--- %s: %zu force samples\n", when, r.forceCount_);
    for (std::size_t i = 0; i < r.forceCount_; ++i) {
        const auto& f = r.forces_[i];
        const double mag = length(f.force);
        if (mag < 3.0) continue;
        std::printf("   owner %zu kind %d at (%+.3f,%+.3f) F(%+.2f,%+.2f) |F| %6.2f N %s\n", f.owner, (int)f.kind,
                    f.at.x, f.at.y, f.force.x, f.force.y, mag, f.couple ? "(couple)" : "");
    }
}
int main() {
    PlaygroundRoom r; r.reset();
    r.toggleForceArrows();
    r.toggleForceNewtons();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    // The King's picture: a ball on the floor with a cube set down on top of it.
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {1.395, 0.35 + 2.0, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    r.ball_.position = {1.395, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int t = 1; t <= 120; ++t) {
        r.advance(1.0 / 20.0);
        if (t % 20 == 0 || t < 4) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "tick %d", t);
            std::printf("ball (%+.4f,%+.4f) v(%+.3f,%+.3f) frz %d | cube (%+.4f,%+.4f) v(%+.3f,%+.3f) frz %d\n",
                        r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                        (int)r.ball_.frozen, cube.centre.x, cube.centre.y, cube.velocity.x, cube.velocity.y,
                        (int)cube.frozen);
            dump(r, buf);
        }
    }
    return 0;
}
