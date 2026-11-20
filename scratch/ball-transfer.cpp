// Something dropped on the ball: does the ball take any of it?
#include "common.h"
static void report(const char* what, PlaygroundRoom& r, double worstBall) {
    std::printf("%-46s ball v(%+.4f,%+.4f) spin %+.3f frozen %d moving-most %+.4f m/s\n", what, r.ball_.velocity.x,
                r.ball_.velocity.y, r.ball_.spin, (int)r.ball_.frozen, worstBall);
}
int main() {
    // 1. The dot dropped on the ball, which is resting on the floor.
    {
        PlaygroundRoom r; r.reset();
        for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
        // Put the ball on the floor away from everything, then the dot above it.
        r.ball_.position = {1.0, r.ball_.radius, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
        r.ball_.previous = r.ball_.position;
        r.point_ = {1.02, 1.60, 0};
        r.velocity_ = {}; r.pointFrozen_ = false;
        r.previous_ = r.point_;
        double worst = 0.0;
        for (int t = 1; t <= 60; ++t) {
            r.advance(1.0 / 20.0);
            worst = std::max(worst, length(r.ball_.velocity));
        }
        report("dot dropped on the ball on the floor", r, worst);
        std::printf("    dot ended (%+.4f,%+.4f) v(%+.3f,%+.3f)\n", r.point_.x, r.point_.y, r.velocity_.x, r.velocity_.y);
    }
    // 2. The dot dropped on the ball sitting on the cube.
    {
        PlaygroundRoom r; r.reset();
        for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
        const double top = 0.35 + 0.12;
        r.ball_.position = {r.bodies_[0].centre.x, top, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
        r.ball_.previous = r.ball_.position;
        r.point_ = {r.bodies_[0].centre.x + 0.02, top + 1.3, 0};
        r.velocity_ = {}; r.pointFrozen_ = false;
        r.previous_ = r.point_;
        double worst = 0.0;
        for (int t = 1; t <= 60; ++t) {
            r.advance(1.0 / 20.0);
            worst = std::max(worst, length(r.ball_.velocity));
        }
        report("dot dropped on the ball on the cube", r, worst);
    }
    // 3. The cube dropped on the ball sitting on the floor.
    {
        PlaygroundRoom r; r.reset();
        for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
        r.ball_.position = {1.0, r.ball_.radius, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
        r.ball_.previous = r.ball_.position;
        PlaygroundRoom::Body& cube = r.bodies_[0];
        cube.centre = {1.0, r.ball_.radius + 0.12 + 0.35 + 0.9, 0};
        cube.angle = 0.0;
        cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
        PlaygroundRoom::placeCorners(cube);
        cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
        double worst = 0.0;
        for (int t = 1; t <= 60; ++t) {
            r.advance(1.0 / 20.0);
            worst = std::max(worst, length(r.ball_.velocity));
        }
        report("cube dropped on the ball on the floor", r, worst);
        std::printf("    cube ended c(%+.4f,%+.4f) a%+.4f v(%+.3f,%+.3f)\n", cube.centre.x, cube.centre.y,
                    cube.angle, cube.velocity.x, cube.velocity.y);
    }
    return 0;
}
