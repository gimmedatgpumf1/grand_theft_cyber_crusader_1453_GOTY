#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
using namespace gtc::native;
int main() {
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(1.0/20);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    r.placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5*3.14159265358979323846; t.centre = {-0.25+1.4, 0.7, 0};
    r.placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {0.06, 0.60, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < 400; ++k) {
        r.advance(1.0/20);
        const bool bad = !std::isfinite(r.ball_.position.x) || !std::isfinite(r.ball_.velocity.x) ||
                         !std::isfinite(r.bodies_[0].centre.x) || !std::isfinite(r.bodies_[1].centre.x) ||
                         !std::isfinite(r.bodies_[1].spin);
        if (bad || true)
            std::printf("t%03d ball (%+.5f,%+.5f) v(%+.5f,%+.5f) w%+.3f | cube c(%+.4f,%+.4f) v(%+.4f,%+.4f) w%+.4f | tri c(%+.4f,%+.4f) a%+.5f v(%+.4f,%+.4f) w%+.5f\n",
                k, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin,
                r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].velocity.x, r.bodies_[0].velocity.y, r.bodies_[0].spin,
                r.bodies_[1].centre.x, r.bodies_[1].centre.y, r.bodies_[1].angle, r.bodies_[1].velocity.x, r.bodies_[1].velocity.y, r.bodies_[1].spin);
        if (bad) { std::printf("  ^ first bad tick\n"); break; }
    }
    return 0;
}
