#include "common.h"
int main() {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = true;
    r.ball_.previous = r.ball_.position;
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {0.30, r.ball_.radius + 0.12 + 0.35 + 1.00, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    for (int t = 1; t <= 40; ++t) {
        const double before = cube.velocity.y;
        r.advance(1.0 / 20.0);
        const double gap = r.bodyBallGap(0, *new Vec3{}, *new Vec3{});
        std::printf("t%-3d cube c(%+.4f,%+.4f) v(%+.3f->%+.3f) met %d | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) frozen %d | gap %+.4f\n",
                    t, cube.centre.x, cube.centre.y, before, cube.velocity.y, cube.met, r.ball_.position.x,
                    r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, (int)r.ball_.frozen, gap);
    }
    return 0;
}
