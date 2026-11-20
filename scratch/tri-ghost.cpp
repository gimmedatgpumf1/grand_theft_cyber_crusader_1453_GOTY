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
    std::printf("%-10s tri v(%+.9f,%+.9f) w%+.9f frz%d plane%d app%d | cube v(%+.9f,%+.9f) app%d | ball v(%+.9f,%+.9f) touched%zu\n", tag,
                r.bodies_[1].velocity.x, r.bodies_[1].velocity.y, r.bodies_[1].spin, int(r.bodies_[1].frozen),
                int(r.bodies_[1].touchingPlane), int(r.bodies_[1].appliedCount),
                r.bodies_[0].velocity.x, r.bodies_[0].velocity.y, int(r.bodies_[0].appliedCount),
                r.ball_.velocity.x, r.ball_.velocity.y, r.ballTouchCount_);
}
int main() {
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.90, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    PlaygroundRoom::placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {-0.25 + 1.4, 0.7, 0};
    PlaygroundRoom::placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {0.21, 0.25, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < 1400; ++k) r.advance(kDt);
    dump(r, "tick start");
    // one tick by hand
    r.ballBlows_ = {}; r.ball_.arrival = r.ball_.velocity; r.clearForces();
    for (auto& b : r.bodies_) b.woken = false;
    r.tickBodies(); dump(r, "bodies");
    r.tickBall(); dump(r, "ball");
    r.separateBall(); dump(r, "separate");
    r.freezeCheck(); dump(r, "freeze");
    r.writeImpulses(); dump(r, "impulses");
    std::printf("tri applied forces:\n");
    for (std::size_t i = 0; i < r.bodies_[1].appliedCount; ++i)
        std::printf("   kind %d J(%+.9f,%+.9f) at (%.9f,%.9f)\n", int(r.bodies_[1].applied[i].kind),
                    r.bodies_[1].applied[i].impulse.x, r.bodies_[1].applied[i].impulse.y,
                    r.bodies_[1].applied[i].at.x, r.bodies_[1].applied[i].at.y);
    std::printf("tri corners: ");
    for (std::size_t i = 0; i < r.bodies_[1].count; ++i)
        std::printf("(%.9f,%.9f) ", r.bodies_[1].position[i].x, r.bodies_[1].position[i].y);
    std::printf("\n");
    return 0;
}
