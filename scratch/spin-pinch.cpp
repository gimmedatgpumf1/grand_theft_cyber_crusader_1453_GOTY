// A ball pinched between the floor and a weight on top, given a spin: rotation is
// impossible in that pinch, so the spin has to die at once. A ball with only the
// floor under it is the control: there the spin survives, because rolling is free.
#include "common.h"
static void run(const char* what, bool withCube, bool asleepCube) {
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
        cube.angle = 0.0;
        cube.velocity = {};
        cube.spin = 0.0;
        cube.frozen = asleepCube;
        PlaygroundRoom::placeCorners(cube);
        cube.previous = cube.position;
        cube.previousCentre = cube.centre;
        cube.previousAngle = cube.angle;
        for (int i = 0; i < 60; ++i) r.advance(1.0 / 20.0);
        cube.frozen = asleepCube;
    }
    // The spin, and nothing else: the ball is not travelling anywhere.
    r.ball_.frozen = false;
    r.ball_.spin = 20.0;
    r.ball_.velocity = {};
    std::printf("%-32s spin 20 rad/s ->", what);
    for (int t = 1; t <= 8; ++t) {
        r.advance(1.0 / 20.0);
        std::printf(" t%d %6.3f", t, r.ball_.spin);
    }
    std::printf("   | contacts %zu\n", r.ballTouchCount_);
}
int main() {
    run("ball alone on the floor", false, false);
    run("ball with a cube resting on it", true, false);
    run("ball with a sleeping cube on it", true, true);
    return 0;
}
