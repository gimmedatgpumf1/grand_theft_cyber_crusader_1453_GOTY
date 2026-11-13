// The ball left overlapping the bottom edge of a cube that is standing on the floor.
#include "common.h"
static void run(const char* what, double bx, double by) {
    PlaygroundRoom r; r.reset();
    r.toggleForceArrows(); r.toggleForceNewtons();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {1.395, 0.35, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    r.ball_.position = {bx, by, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    std::printf("--- %s: ball put at (%+.3f,%+.3f)\n", what, bx, by);
    for (int t = 1; t <= 40; ++t) {
        r.advance(1.0 / 20.0);
        double worst = 0.0;
        Vec3 at{}, f{};
        std::size_t who = 0;
        for (std::size_t i = 0; i < r.forceCount_; ++i) {
            const auto& s = r.forces_[i];
            if (length(s.force) > worst) { worst = length(s.force); at = s.at; f = s.force; who = s.owner; }
        }
        Vec3 way{}, pt{};
        const double gap = r.bodyBallGap(0, way, pt);
        if (worst > 60.0)
            std::printf("  t%-3d worst %6.2f N owner %zu F(%+.1f,%+.1f) at (%+.3f,%+.3f) | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) frz %d gap %+.5f\n",
                        t, worst, who, f.x, f.y, at.x, at.y, r.ball_.position.x, r.ball_.position.y,
                        r.ball_.velocity.x, r.ball_.velocity.y, (int)r.ball_.frozen, gap);
    }
}
int main() {
    run("ball half into the cube's bottom face", 1.395, 0.10);
    run("ball at the cube's bottom-left corner, awake cube", 1.045, 0.10);
    run("ball into the cube's left side", 1.15, 0.20);
    run("ball centred in the cube (deep inside)", 1.395, 0.35);
    return 0;
}
