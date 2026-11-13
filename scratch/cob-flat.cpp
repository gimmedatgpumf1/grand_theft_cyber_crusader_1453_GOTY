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
    for (int t = 1; t <= 14; ++t) {
        const double b = cube.velocity.y;
        r.advance(1.0 / 20.0);
        std::printf("t%-3d cube v.y %+.3f -> %+.3f c %+.4f | ball v(%+.3f,%+.3f) y %+.4f\n", t, b, cube.velocity.y,
                    cube.centre.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.position.y);
    }
}
