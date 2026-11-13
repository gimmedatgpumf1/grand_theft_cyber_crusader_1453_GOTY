// A cube dropped onto a ball on the floor, with every force the room writes.
#include "common.h"
int main() {
    PlaygroundRoom r; r.reset();
    r.toggleForceArrows(); r.toggleForceNewtons();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {1.395, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {1.395, 0.36 + 1.0, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    for (int t = 1; t <= 60; ++t) {
        r.advance(1.0 / 20.0);
        double worst = 0.0;
        for (std::size_t i = 0; i < r.forceCount_; ++i) worst = std::max(worst, length(r.forces_[i].force));
        if (worst < 60.0) continue;
        std::printf("t%-3d ball (%+.4f,%+.4f) v(%+.3f,%+.3f) frz %d touch %d stand %d sup(%+.2f,%+.2f) | cube (%+.4f,%+.4f) v(%+.3f,%+.3f)\n",
                    t, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                    (int)r.ball_.frozen, (int)r.ball_.touching, (int)r.ball_.standing, r.ball_.supportNormal.x,
                    r.ball_.supportNormal.y, cube.centre.x, cube.centre.y, cube.velocity.x, cube.velocity.y);
        for (std::size_t i = 0; i < r.forceCount_; ++i) {
            const auto& f = r.forces_[i];
            if (length(f.force) < 15.0) continue;
            std::printf("    owner %zu kind %d at (%+.3f,%+.3f) F(%+8.2f,%+8.2f)\n", f.owner, (int)f.kind, f.at.x,
                        f.at.y, f.force.x, f.force.y);
        }
    }
    return 0;
}
