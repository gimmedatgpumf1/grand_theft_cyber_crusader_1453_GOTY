// A body with speed in it that it never spends.
//
// The King's picture has a 19.7 N arrow standing out of the middle of a shape that
// is not going anywhere, and that colour is the air. The air can only press that
// hard on something moving through it at metres a second. So the question is
// whether a body can end a tick holding a speed while its own place has not moved:
// speed in the state, travel refused by the step, and everything that reads the
// speed -- the air, the energy readout, the arrows -- told about motion that never
// happened.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr double kDt = 1.0 / 20.0;
}

int main() {
    PlaygroundRoom r;
    r.reset();
    r.toggleForceArrows();
    r.toggleForceNewtons();
    for (int i = 0; i < 40; ++i) r.advance(kDt);
    auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
    auto& t = r.bodies_[PlaygroundRoom::kTriangle];
    // The King's arrangement: the triangle tipped over onto one corner, and the
    // plank let go across its slope.
    t.angle = -2.40;
    PlaygroundRoom::placeCorners(t);
    t.previous = t.position;
    t.previousCentre = t.centre;
    t.previousAngle = t.angle;
    t.frozen = false;
    s.angle = 0.60;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    s.frozen = false;
    double worstPhantom = 0.0, worstDrag = 0.0, worstEnergyLie = 0.0;
    int worstTick = -1;
    std::size_t worstBody = 0;
    for (int k = 0; k < 1200; ++k) {
        std::array<Vec3, PlaygroundRoom::kBodyCount> was{};
        std::array<double, PlaygroundRoom::kBodyCount> wasAngle{};
        for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
            was[i] = r.bodies_[i].centre;
            wasAngle[i] = r.bodies_[i].angle;
        }
        r.advance(kDt);
        for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
            const auto& b = r.bodies_[i];
            if (!b.count) continue;
            const double moved = length(b.centre - was[i]);
            const double turned = std::abs(b.angle - wasAngle[i]);
            const double speed = length(b.velocity) + std::abs(b.spin) * 1.0;
            // Speed it holds, travel it did not get.
            if (moved < 1e-6 && turned < 1e-6 && speed > worstPhantom) {
                worstPhantom = speed;
                worstTick = k;
                worstBody = i;
            }
        }
        // And what the ledger says the air is doing about it.
        double air = 0.0;
        for (std::size_t i = 0; i < r.forceCount_; ++i)
            if (r.forces_[i].kind == PlaygroundRoom::ForceKind::Drag) air = std::max(air, length(r.forces_[i].force));
        if (air > worstDrag) worstDrag = air;
    }
    std::printf("worst speed held by a body that did not move at all: %.3f m/s (t%d body %zu, plank %+.3f w%+.3f)\n",
                worstPhantom, worstTick, worstBody, r.bodies_[PlaygroundRoom::kSeesaw].angle,
                r.bodies_[PlaygroundRoom::kSeesaw].spin);
    std::printf("worst air-drag arrow drawn at any point in the run: %.2f N\n", worstDrag);
    std::printf("last tick, every force with its owner and kind:\n");
    double kinetic = 0.0, potential = 0.0;
    r.energyBudget(kinetic, potential);
    std::printf("  E %.3f J (KE %.3f, PE %.3f)\n", kinetic + potential, kinetic, potential);
    for (std::size_t i = 0; i < r.forceCount_; ++i) {
        const auto& f = r.forces_[i];
        std::printf("  kind %d owner %zu at (%+.3f,%+.3f) f (%+.3f,%+.3f) |f| %.3f\n", int(f.kind), f.owner, f.at.x,
                    f.at.y, f.force.x, f.force.y, length(f.force));
    }
    std::printf("  bodies: plank a%+.4f w%+.4f v(%+.3f,%+.3f) frz%d | tri a%+.4f w%+.4f v(%+.3f,%+.3f) frz%d\n",
                r.bodies_[2].angle, r.bodies_[2].spin, r.bodies_[2].velocity.x, r.bodies_[2].velocity.y,
                int(r.bodies_[2].frozen), r.bodies_[1].angle, r.bodies_[1].spin, r.bodies_[1].velocity.x,
                r.bodies_[1].velocity.y, int(r.bodies_[1].frozen));
    (void)worstEnergyLie;
    return 0;
}
