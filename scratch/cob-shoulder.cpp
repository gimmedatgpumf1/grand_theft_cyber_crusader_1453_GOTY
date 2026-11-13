#include "common.h"
int main() {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {0.30 + 0.16, r.ball_.radius + 0.12 + 0.35 + 0.50, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    for (int t = 1; t <= 16; ++t) {
        r.advance(1.0 / 20.0);
        std::printf("t%-3d cube c(%+.4f,%+.4f) a%+.3f v(%+.3f,%+.3f) | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) spin %+.2f frz %d\n", t,
                    cube.centre.x, cube.centre.y, cube.angle, cube.velocity.x, cube.velocity.y,
                    r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin,
                    (int)r.ball_.frozen);
    }
}
