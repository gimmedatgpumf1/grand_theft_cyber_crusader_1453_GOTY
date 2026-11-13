// Where does the room write a force that no weight in it can account for?
#include "common.h"
static double worstStatic(PlaygroundRoom& r, std::size_t owner, const char* what) {
    double best = 0.0;
    Vec3 at{};
    int kind = 0;
    for (std::size_t i = 0; i < r.forceCount_; ++i) {
        const auto& f = r.forces_[i];
        if (f.owner != owner) continue;
        const double mag = length(f.force);
        if (mag > best) { best = mag; at = f.at; kind = int(f.kind); }
    }
    if (best > 60.0)
        std::printf("   %-34s owner %zu worst %6.2f N kind %d at (%+.3f,%+.3f)\n", what, owner, best, kind, at.x, at.y);
    return best;
}
int main() {
    // A: cube set down resting on a ball on the floor.
    // B: cube resting on the floor with the ball shoved under its edge.
    // C: cube dropped onto a ball and left to settle.
    for (int variant = 0; variant < 5; ++variant) {
        PlaygroundRoom r; r.reset();
        r.toggleForceArrows(); r.toggleForceNewtons();
        for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
        PlaygroundRoom::Body& cube = r.bodies_[0];
        double dx = 0.0, dy = 0.0, drop = 0.0;
        if (variant == 0) { dx = 0.00; dy = 0.36; }        // cube resting on the ball
        if (variant == 1) { dx = 0.20; dy = 0.36; }        // cube resting near the ball's side
        if (variant == 2) { dx = 0.00; dy = 0.60; }        // cube dropped half a metre onto it
        if (variant == 3) { dx = 0.30; dy = 0.00; }        // cube on the floor, ball under its edge
        if (variant == 4) { dx = 0.00; dy = 0.36; drop = 1.0; }
        r.ball_.position = {1.395, r.ball_.radius, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
        r.ball_.previous = r.ball_.position;
        for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
        cube.centre = {1.395 + dx, dy + drop, 0};
        cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
        PlaygroundRoom::placeCorners(cube);
        cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
        double peak = 0.0;
        for (int t = 1; t <= 300; ++t) {
            r.advance(1.0 / 20.0);
            for (std::size_t i = 0; i < r.forceCount_; ++i)
                if (length(r.forces_[i].force) > 60.0) peak = std::max(peak, length(r.forces_[i].force));
        }
        std::printf("variant %d dx %+.2f dy %+.2f drop %.1f: peak force written %.2f N | settled: ball (%+.4f,%+.4f) frz %d"
                    " cube (%+.4f,%+.4f) a%+.3f frz %d\n", variant, dx, dy, drop, peak,
                    r.ball_.position.x, r.ball_.position.y, (int)r.ball_.frozen, cube.centre.x, cube.centre.y,
                    cube.angle, (int)cube.frozen);
        worstStatic(r, 0, "cube, at rest");
        worstStatic(r, 2, "ball, at rest");
    }
    return 0;
}
