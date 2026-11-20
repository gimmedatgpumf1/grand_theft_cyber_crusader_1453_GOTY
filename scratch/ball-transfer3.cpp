#include "common.h"
static void run(const char* what, PlaygroundRoom& r, int ticks) {
    std::printf("%s\n", what);
    double best = 0.0;
    for (int t = 1; t <= ticks; ++t) {
        r.advance(1.0 / 20.0);
        best = std::max(best, length(r.ball_.velocity));
        if (t <= 12)
            std::printf("  t%-3d dot (%+.3f,%+.3f) v(%+.2f,%+.2f) | ball v(%+.3f,%+.3f) spin %+.2f (%.4f,%+.4f) frozen %d\n",
                        t, r.point_.x, r.point_.y, r.velocity_.x, r.velocity_.y, r.ball_.velocity.x,
                        r.ball_.velocity.y, r.ball_.spin, r.ball_.position.x, r.ball_.position.y, (int)r.ball_.frozen);
    }
    std::printf("  --> the ball's fastest this run: %.4f m/s\n", best);
}
static PlaygroundRoom settled() {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    return r;
}
int main() {
    // 1. The dot onto the ball that is resting on the cube -- the King's picture.
    {
        PlaygroundRoom r = settled();
        const double top = 0.35 + 0.12;
        r.ball_.position = {r.bodies_[0].centre.x, top, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
        r.ball_.previous = r.ball_.position;
        r.point_ = {r.bodies_[0].centre.x, top + 1.2, 0};
        r.velocity_ = {}; r.pointFrozen_ = false; r.previous_ = r.point_;
        run("dot dropped straight onto the ball resting on the cube", r, 40);
    }
    // 2. The dot onto the shoulder of a ball on the floor: it should be shoved aside.
    {
        PlaygroundRoom r = settled();
        r.ball_.position = {1.0, r.ball_.radius, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
        r.ball_.previous = r.ball_.position;
        r.point_ = {1.10, 1.00, 0};
        r.velocity_ = {}; r.pointFrozen_ = false; r.previous_ = r.point_;
        run("dot dropped on the shoulder of a ball on the floor", r, 40);
    }
    // 3. A cube onto the shoulder of the ball: the ball must be driven across.
    {
        PlaygroundRoom r = settled();
        r.ball_.position = {1.0, r.ball_.radius, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
        r.ball_.previous = r.ball_.position;
        PlaygroundRoom::Body& cube = r.bodies_[0];
        cube.centre = {1.16, 0.24 + 0.35, 0};
        cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
        PlaygroundRoom::placeCorners(cube);
        cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
        std::printf("cube resting on the shoulder of the ball\n");
        for (int t = 1; t <= 60; ++t) {
            r.advance(1.0 / 20.0);
            if (t <= 10 || t % 10 == 0)
                std::printf("  t%-3d cube c(%+.4f,%+.4f) | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) spin %+.2f\n", t,
                            cube.centre.x, cube.centre.y, r.ball_.position.x, r.ball_.position.y,
                            r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin);
        }
    }
    return 0;
}
