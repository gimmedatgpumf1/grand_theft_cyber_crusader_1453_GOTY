// What is being done to a creeping body and a creeping ball, force by force.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
using namespace gtc::native;
static const char* kindName(PlaygroundRoom::ForceKind k) {
    switch (k) {
        case PlaygroundRoom::ForceKind::Gravity: return "gravity";
        case PlaygroundRoom::ForceKind::Contact: return "CONTACT";
        case PlaygroundRoom::ForceKind::Friction: return "friction";
        case PlaygroundRoom::ForceKind::Pair: return "pair";
        case PlaygroundRoom::ForceKind::Ball: return "ball";
        case PlaygroundRoom::ForceKind::Drag: return "drag";
        case PlaygroundRoom::ForceKind::Rolling: return "rolling";
        default: return "?";
    }
}
int main() {
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(1.0 / 20.0);
    auto& c = r.bodies_[0];
    c.centre = {-0.90, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    PlaygroundRoom::placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {-0.25 + 1.4, 0.7, 0};
    PlaygroundRoom::placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {0.33, 0.14, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < 1600; ++k) r.advance(1.0 / 20.0);
    std::printf("velocities: ball (%+.4e,%+.4e) w%+.4e | cube (%+.4e,%+.4e) w%+.4e | tri (%+.4e,%+.4e) w%+.4e\n",
                r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin, r.bodies_[0].velocity.x, r.bodies_[0].velocity.y,
                r.bodies_[0].spin, r.bodies_[1].velocity.x, r.bodies_[1].velocity.y, r.bodies_[1].spin);
    for (std::size_t i = 0; i <= PlaygroundRoom::kBodyCount; ++i) {
        const char* name = i == PlaygroundRoom::kBallOwner ? "ball" : i == PlaygroundRoom::kDotOwner ? "dot" : "body";
        Vec3 sum{}, moment{};
        std::printf("-- owner %zu %s\n", i, name);
        for (std::size_t f = 0; f < r.forceCount_; ++f) {
            const PlaygroundRoom::ForceSample& s = r.forces_[f];
            if (s.owner != i) continue;
            const double n = length(s.force);
            if (n < 1e-12) continue;
            sum = sum + s.force;
            std::printf("   %-9s at (%+9.6f,%+9.6f) F (%+12.6e,%+12.6e) = %11.4e N\n", kindName(s.kind), s.at.x, s.at.y,
                        s.force.x, s.force.y, n);
        }
        std::printf("   SUM F (%+12.6e,%+12.6e) N  -> a (%+12.6e,%+12.6e) m/s^2\n", sum.x, sum.y, sum.x / 1.0, sum.y / 1.0);
    }
    return 0;
}
