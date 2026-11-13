// Does the account close? Every force the room writes down for a solid, summed over
// one tick, has to be that solid's change of momentum -- m*dv, exactly.
#include "common.h"
static void run(const char* name, double dx, double dy, double drop) {
    PlaygroundRoom r; r.reset();
    r.toggleForceArrows(); r.toggleForceNewtons();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {1.395, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {1.395 + dx, dy + drop, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    double worstBall = 0.0, worstCube = 0.0;
    int atBall = 0, atCube = 0;
    for (int t = 1; t <= 200; ++t) {
        const Vec3 vBall = r.ball_.velocity, vCube = cube.velocity;
        r.advance(1.0 / 20.0);
        Vec3 jBall{}, jCube{};
        for (std::size_t i = 0; i < r.forceCount_; ++i) {
            const auto& f = r.forces_[i];
            // A couple is a moment and not a force: its `force` is only how it is
            // drawn. Summing it as an impulse would book rolling resistance as a
            // shove the ball never felt.
            if (f.couple) continue;
            const Vec3 impulse = f.force * playground_tick::kTick;
            if (f.owner == 2) jBall = jBall + impulse;
            if (f.owner == 0) jCube = jCube + impulse;
        }
        const double mb = length((r.ball_.velocity - vBall) * r.ball_.mass - jBall);
        const double mc = length((cube.velocity - vCube) * cube.mass - jCube);
        if (mb > worstBall) { worstBall = mb; atBall = t; }
        if (mc > worstCube) { worstCube = mc; atCube = t; }
    }
    std::printf("%-30s | ball account off by %9.5f N s (t%-3d) | cube account off by %9.5f N s (t%d)\n", name,
                worstBall, atBall, worstCube, atCube);
}
int main() {
    run("cube set down on the ball", 0.00, 0.36, 0.0);
    run("cube dropped 0.5 m onto it", 0.00, 0.36, 0.5);
    run("cube dropped 1.0 m onto it", 0.00, 0.36, 1.0);
    run("cube dropped onto the shoulder", 0.16, 0.36, 0.5);
    run("nothing at all, just resting", 0.00, 0.36, 0.0);
    return 0;
}
