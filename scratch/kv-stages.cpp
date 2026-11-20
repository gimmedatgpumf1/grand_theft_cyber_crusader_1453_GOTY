#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace { constexpr double kDt = 1.0 / 20.0; }
using namespace gtc::native;
static void dump(const char* tag, PlaygroundRoom& r) {
    std::printf("%-14s ball (%.9f,%.9f) v(%+.6f,%+.6f) w%+.5f | cube c(%.9f,%.9f) v(%+.6f,%+.6f) w%+.5f | tri c(%.9f,%.9f) v(%+.6f,%+.6f) w%+.5f\n",
                tag, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin,
                r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].velocity.x, r.bodies_[0].velocity.y, r.bodies_[0].spin,
                r.bodies_[1].centre.x, r.bodies_[1].centre.y, r.bodies_[1].velocity.x, r.bodies_[1].velocity.y, r.bodies_[1].spin);
}
int main(int argc, char** argv) {
    const double footX = argc > 1 ? std::atof(argv[1]) : -0.25;
    const double ballX = argc > 2 ? std::atof(argv[2]) : -0.18;
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    r.placeCorners(c);
    c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {footX + 1.4, 0.7, 0};
    r.placeCorners(t);
    t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {ballX, 0.13, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < 398; ++k) r.advance(kDt);
    // One tick, by hand, in the order advance() runs it.
    const double tick = kDt;
    (void)tick;
    dump("before", r);
    r.ballBlows_ = {};
    r.ball_.arrival = r.ball_.velocity;
    r.clearForces();
    for (auto& b : r.bodies_) b.woken = false;
    r.tickBodies();
    dump("after bodies", r);
    r.tickBall();
    dump("after ball", r);
    r.separateBall();
    dump("after separate", r);
    r.freezeCheck();
    dump("after freeze", r);
    return 0;
}
