// Does every newton-second the room writes down actually land on the thing it is
// written on? Sum the ledger per owner across a tick and compare with m*dv.
#include "common.h"
struct Case { const char* name; double dx, dy, drop; };
static void run(const Case& c) {
    PlaygroundRoom r; r.reset();
    r.toggleForceArrows(); r.toggleForceNewtons();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {1.395, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {1.395 + c.dx, c.dy + c.drop, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    double worstBall = 0.0, worstCube = 0.0;
    int worstAt = 0;
    for (int t = 1; t <= 200; ++t) {
        Vec3 vBall = r.ball_.velocity, vCube = cube.velocity;
        const double ballFrozen = r.ball_.frozen, cubeFrozen = cube.frozen;
        r.advance(1.0 / 20.0);
        // The ledger for the tick just run; advance() clears it at the start.
        Vec3 jBall{}, jCube{};
        for (std::size_t i = 0; i < r.forceCount_; ++i) {
            const auto& f = r.forces_[i];
            const Vec3 impulse = f.force * playground_tick::kTick;
            if (f.owner == 2) jBall = jBall + impulse;
            if (f.owner == 0) jCube = jCube + impulse;
        }
        // Weight is a force, not an impulse on this account, so take it out of both
        // sides: compare only the contact part, against the change it caused.
        const Vec3 dBall = r.ball_.velocity - vBall;
        const Vec3 dCube = cube.velocity - vCube;
        if (!ballFrozen) {
            const double miss = length(dBall - jBall * (1.0 / r.ball_.mass)) -
                                length(Vec3{0, -playground_tick::kGravity * playground_tick::kTick, 0});
            if (miss > worstBall) { worstBall = miss; worstAt = t; }
        }
        if (!cubeFrozen) {
            const double miss = length(dCube - jCube * (1.0 / cube.mass));
            if (miss > worstCube) worstCube = miss;
        }
    }
    std::printf("%-30s dx %+.2f dy %+.2f drop %.1f | ball ledger miss %8.4f m/s (t%d) | cube ledger miss %8.4f m/s\n",
                c.name, c.dx, c.dy, c.drop, worstBall, worstAt, worstCube);
}
int main() {
    run({"cube set down on the ball", 0.00, 0.36, 0.0});
    run({"cube dropped 0.5 m onto the ball", 0.00, 0.36, 0.5});
    run({"cube dropped 1.0 m onto the ball", 0.00, 0.36, 1.0});
    run({"cube dropped onto the shoulder", 0.16, 0.36, 0.5});
    run({"nothing at all", 0.00, 0.36, 0.0});
    return 0;
}
