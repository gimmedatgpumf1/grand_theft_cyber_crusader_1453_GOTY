// A cube dropped onto a ball on the floor: does the ball get anything?
#include "common.h"
static void drop(const char* what, double dx, double height) {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    // The ball is put where it can be seen and then *left to settle there*, so it
    // is a ball on the floor with the floor under it -- not a ball teleported into
    // place with the support it had somewhere else.
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {0.30 + dx, r.ball_.radius + 0.12 + 0.35 + height, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    double best = 0.0, bestSpin = 0.0, cubeBounce = 0.0;
    double prevCubeV = 0.0;
    for (int t = 1; t <= 200; ++t) {
        prevCubeV = cube.velocity.y;
        r.advance(1.0 / 20.0);
        best = std::max(best, length(r.ball_.velocity));
        bestSpin = std::max(bestSpin, std::abs(r.ball_.spin));
        if (prevCubeV < -0.5 && cube.velocity.y > 0.1) cubeBounce = std::max(cubeBounce, cube.velocity.y / -prevCubeV);
    }
    std::printf("%-40s dx %+.2f h %.2f | ball fastest %.3f m/s spin %.2f | moved to (%+.3f,%+.3f) | cube bounced at %.3f of arrival\n",
                what, dx, height, best, bestSpin, r.ball_.position.x, r.ball_.position.y, cubeBounce);
}
int main() {
    drop("cube flat on the crown", 0.0, 0.30);
    drop("cube flat on the crown", 0.0, 1.00);
    drop("cube edge on the shoulder", 0.16, 0.50);
    drop("cube corner on the side", 0.30, 0.50);
    drop("cube corner on the side", 0.34, 0.80);
    return 0;
}
