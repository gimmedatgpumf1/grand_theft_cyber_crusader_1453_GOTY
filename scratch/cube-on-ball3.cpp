// A cube dropped onto a ball on the floor, traced tick by tick around the meeting.
#include "common.h"
static void drop(const char* what, double dx, double height) {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
    r.ball_.previous = r.ball_.position;
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {0.30 + dx, r.ball_.radius + 0.12 + 0.35 + height, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    std::printf("--- %s dx %+.2f h %.2f\n", what, dx, height);
    double best = 0.0, bestSpin = 0.0;
    int hit = -1;
    for (int t = 1; t <= 60; ++t) {
        const double vyBefore = cube.velocity.y;
        const double ballBefore = r.ball_.position.x;
        r.advance(1.0 / 20.0);
        best = std::max(best, length(r.ball_.velocity));
        bestSpin = std::max(bestSpin, std::abs(r.ball_.spin));
        if (hit < 0 && length(r.ball_.velocity) > 0.0) hit = t;
        if (t >= 1 && (std::abs(cube.velocity.y - vyBefore) > 0.4 || length(r.ball_.velocity) > 0.0))
            std::printf("  t%-3d cube v.y %+.3f->%+.3f c(%+.4f,%+.4f) a%+.3f | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) spin %+.2f\n", t,
                        vyBefore, cube.velocity.y, cube.centre.x, cube.centre.y, cube.angle,
                        r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin);
        (void)ballBefore;
    }
    std::printf("  ball fastest %.3f m/s spin %.2f, came to (%+.4f,%+.4f)\n\n", best, bestSpin,
                r.ball_.position.x, r.ball_.position.y);
}
int main() {
    drop("cube flat on the crown", 0.00, 0.30);
    drop("cube flat on the crown", 0.00, 1.00);
    drop("edge on the shoulder", 0.16, 0.50);
    drop("corner on the side", 0.30, 0.50);
    drop("corner on the side", 0.34, 0.80);
    return 0;
}
