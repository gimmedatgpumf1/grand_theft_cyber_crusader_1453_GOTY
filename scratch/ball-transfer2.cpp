#include "common.h"
static void tickByTick(const char* what, PlaygroundRoom& r, int ticks) {
    std::printf("%s\n", what);
    for (int t = 1; t <= ticks; ++t) {
        const Vec3 pv = r.velocity_, bv = r.ball_.velocity;
        r.advance(1.0 / 20.0);
        std::printf("  t%-3d dot (%+.3f,%+.3f) v(%+.3f,%+.3f) | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) spin %+.2f frozen %d\n",
                    t, r.point_.x, r.point_.y, r.velocity_.x, r.velocity_.y, r.ball_.position.x, r.ball_.position.y,
                    r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin, (int)r.ball_.frozen);
        (void)pv; (void)bv;
    }
}
int main() {
    // The dot onto a ball hanging in mid-air: nothing else in the world can hold it,
    // so whatever happens here is the pair on their own.
    {
        PlaygroundRoom r; r.reset();
        for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
        r.ball_.position = {1.0, 3.0, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
        r.ball_.previous = r.ball_.position;
        r.point_ = {1.0, 3.55, 0};
        r.velocity_ = {}; r.pointFrozen_ = false; r.previous_ = r.point_;
        tickByTick("dot dropped on a ball in mid-air", r, 14);
    }
    // The cube onto a ball on the floor: does the cube bounce off it at all?
    {
        PlaygroundRoom r; r.reset();
        for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
        r.ball_.position = {1.0, r.ball_.radius, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
        r.ball_.previous = r.ball_.position;
        PlaygroundRoom::Body& cube = r.bodies_[0];
        cube.centre = {1.0, r.ball_.radius + 0.12 + 0.35 + 0.30, 0};
        cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
        PlaygroundRoom::placeCorners(cube);
        cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
        std::printf("cube dropped on the ball on the floor\n");
        for (int t = 1; t <= 20; ++t) {
            r.advance(1.0 / 20.0);
            std::printf("  t%-3d cube y %+.4f v(%+.3f,%+.3f) | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) frozen %d\n", t,
                        cube.centre.y, cube.velocity.x, cube.velocity.y, r.ball_.position.x, r.ball_.position.y,
                        r.ball_.velocity.x, r.ball_.velocity.y, (int)r.ball_.frozen);
        }
    }
    return 0;
}
