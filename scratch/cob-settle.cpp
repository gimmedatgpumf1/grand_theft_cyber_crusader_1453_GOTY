// Where does everything end up, and is anything ever inside anything?
#include "common.h"
static void drop(const char* what, double dx, double height) {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {0.30 + dx, r.ball_.radius + 0.12 + 0.35 + height, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    double worst = 0.0, maxEnergy = 0.0;
    double startEnergy = r.totalEnergy();
    for (int t = 1; t <= 400; ++t) {
        r.advance(1.0 / 20.0);
        Vec3 way{}, at{};
        for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
            if (r.bodies_[i].count == 0) continue;
            worst = std::min(worst, r.bodyBallGap(i, way, at));
        }
        maxEnergy = std::max(maxEnergy, r.totalEnergy());
    }
    std::printf("%-34s dx %+.2f h %.2f | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) frz %d | cube (%+.4f,%+.4f) a%+.3f frz %d"
                " | closest ball-to-shape %+.6f m | energy now %.3f of the start (peak %.3f)\n",
                what, dx, height, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                (int)r.ball_.frozen, cube.centre.x, cube.centre.y, cube.angle, (int)cube.frozen, worst,
                r.totalEnergy() / startEnergy, maxEnergy / startEnergy);
}
int main() {
    drop("flat on the crown", 0.00, 0.30);
    drop("flat on the crown", 0.00, 1.00);
    drop("edge on the shoulder", 0.16, 0.50);
    drop("corner over the side", 0.30, 0.50);
    drop("corner over the side", 0.34, 0.80);
    return 0;
}
