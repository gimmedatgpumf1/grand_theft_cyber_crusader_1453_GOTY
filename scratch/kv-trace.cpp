#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace {
constexpr double kDt = 1.0 / 20.0;
void placeTriangle(PlaygroundRoom& r, Vec3 foot) {
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846;
    t.centre = {foot.x + 1.4, 0.7, 0};
    r.placeCorners(t);
    t.previous = t.position;
    t.previousCentre = t.centre;
    t.previousAngle = t.angle;
}
}
int main(int argc, char** argv) {
    const double footX = argc > 1 ? std::atof(argv[1]) : -0.25;
    const double ballX = argc > 2 ? std::atof(argv[2]) : -0.13;
    const int ticks = argc > 3 ? std::atoi(argv[3]) : 45;
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0};
    c.angle = 0.0; c.velocity = {}; c.spin = 0.0; c.frozen = false;
    r.placeCorners(c);
    c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    placeTriangle(r, {footX, 0.0});
    r.ball_.position = {ballX, 0.13, 0};
    r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    for (int k = 0; k < ticks; ++k) {
        r.advance(kDt);
        Vec3 way{}, at{};
        const double g0 = r.bodyBallGap(0, way, at);
        const double g1 = r.bodyBallGap(1, way, at);
        const double g2 = r.bodyBallGap(2, way, at);
        std::printf("t%02d pos(%+.7f,%+.7f) v(%+.4f,%+.4f) w%+.2f frz%d touch%d touches%zu stand%d | gaps cube%+.4f tri%+.4f plank%+.4f | floor%+.5f\n",
                    k, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin,
                    int(r.ball_.frozen), int(r.ball_.touching), r.ballTouchCount_, int(r.ball_.standing),
                    g0, g1, g2, r.ball_.position.y - r.ball_.radius);
    }
    return 0;
}
