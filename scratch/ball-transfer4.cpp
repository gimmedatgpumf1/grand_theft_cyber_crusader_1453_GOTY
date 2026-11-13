#include "common.h"
static PlaygroundRoom settled() {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    // Empty floor on the left of the room, well clear of every shape.
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
    r.ball_.previous = r.ball_.position;
    return r;
}
int main() {
    // A dot dropped on the shoulder of a ball on the floor, off to one side.
    {
        PlaygroundRoom r = settled();
        r.point_ = {0.42, 0.95, 0};
        r.velocity_ = {}; r.pointFrozen_ = false; r.previous_ = r.point_;
        std::printf("dot onto the shoulder of a ball on the floor\n");
        for (int t = 1; t <= 30; ++t) {
            r.advance(1.0 / 20.0);
            if (t <= 12 || t % 6 == 0)
                std::printf("  t%-3d dot (%+.3f,%+.3f) v(%+.2f,%+.2f) | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) spin %+.2f frozen %d\n",
                            t, r.point_.x, r.point_.y, r.velocity_.x, r.velocity_.y, r.ball_.position.x,
                            r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin,
                            (int)r.ball_.frozen);
        }
    }
    // A cube dropped onto the shoulder of a ball on the floor.
    {
        PlaygroundRoom r = settled();
        PlaygroundRoom::Body& cube = r.bodies_[0];
        cube.centre = {0.78, 2.0, 0};
        cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
        PlaygroundRoom::placeCorners(cube);
        cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
        std::printf("cube dropped so its CORNER lands on the side of a ball on the floor\n");
        for (int t = 1; t <= 40; ++t) {
            r.advance(1.0 / 20.0);
            if (t <= 30 && t % 2 == 0)
                std::printf("  t%-3d cube c(%+.4f,%+.4f) a%+.4f | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) spin %+.2f frozen %d\n",
                            t, cube.centre.x, cube.centre.y, cube.angle, r.ball_.position.x, r.ball_.position.y,
                            r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin, (int)r.ball_.frozen);
        }
    }
    return 0;
}
