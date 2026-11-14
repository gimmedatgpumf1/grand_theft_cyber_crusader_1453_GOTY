#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
using namespace gtc::native;
namespace { constexpr double kDt = 1.0 / 20.0; }
static void dump(PlaygroundRoom& r, const char* tag) {
    std::printf("%-12s ball (%.12f,%.12f) v(%+.9f,%+.9f) w%+.9f a%+.9f | tri v(%+.9f,%+.9f) w%+.9f\n", tag,
                r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin, r.ball_.angle,
                r.bodies_[1].velocity.x, r.bodies_[1].velocity.y, r.bodies_[1].spin);
}
int main(int argc, char** argv) {
    const double bx = argc > 1 ? std::atof(argv[1]) : -0.15;
    const double by = argc > 2 ? std::atof(argv[2]) : 0.40;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.90, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    PlaygroundRoom::placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {-0.25 + 1.4, 0.7, 0};
    PlaygroundRoom::placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {bx, by, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < 1400; ++k) r.advance(kDt);
    dump(r, "tick start");
    r.ballBlows_ = {}; r.ball_.arrival = r.ball_.velocity; r.clearForces();
    for (auto& b : r.bodies_) b.woken = false;
    r.tickBodies(); dump(r, "bodies");
    r.tickBall(); dump(r, "ball");
    r.separateBall(); dump(r, "separate");
    r.freezeCheck(); dump(r, "freeze");
    return 0;
}
