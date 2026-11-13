// What load does each of the ball's contacts think it is carrying?
#include "common.h"
static void run(const char* what, bool withCube) {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    PlaygroundRoom::Body& cube = r.bodies_[0];
    if (withCube) {
        cube.centre = {0.30, r.ball_.radius + 0.12 + 0.35, 0};
        cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
        PlaygroundRoom::placeCorners(cube);
        cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
        for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    }
    // One more tick, with the contacts read as they are gathered.
    r.ball_.spin = 20.0;
    r.ball_.velocity = {};
    r.ball_.frozen = false;
    r.ballTouchCount_ = 0;
    r.advance(1.0 / 20.0);
    std::printf("%-30s ball (%+.4f,%+.4f) v(%+.3f,%+.3f) spin %+.3f\n", what, r.ball_.position.x, r.ball_.position.y,
                r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin);
    for (std::size_t i = 0; i < r.ballTouchCount_; ++i) {
        const auto& t = r.ballTouches_[i];
        std::printf("    touch %zu n(%+.2f,%+.2f) surface v(%+.3f,%+.3f) pressed %.5f N s -> %.2f N\n", i, t.normal.x,
                    t.normal.y, t.surfaceVelocity.x, t.surfaceVelocity.y, t.pressed, t.pressed / playground_tick::kTick);
    }
    std::printf("    (ballTouchCount_ %zu)\n", r.ballTouchCount_);
}
int main() {
    run("ball alone on the floor", false);
    run("ball with a cube resting on it", true);
    return 0;
}
