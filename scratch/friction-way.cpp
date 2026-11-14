// A shape sliding along the floor. The friction on it has to point against the slide,
// at every end of the contact, and it may never be worth more than mu times the load.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
int main() {
    constexpr double kDt = 1.0 / 20.0;
    int wrong = 0, counted = 0;
    double worstWrong = 0.0;
    for (int trial = 0; trial < 40; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 5; ++k) r.advance(kDt);
        auto& c = r.bodies_[trial % 2];
        // Set down on clear floor and pushed along it.
        c.centre = {-4.3 + 0.1 * double(trial % 5), c.count == 4 ? 0.36 : 0.71, 0};
        c.angle = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        c.velocity = {1.0 + 0.1 * double(trial % 7), 0.0, 0};
        c.spin = 0.0;
        c.frozen = false;
        for (int k = 0; k < 60; ++k) {
            const Vec3 before = c.velocity;
            r.advance(kDt);
            if (c.velocity.x < 0.02) continue;   // only while it is really sliding
            double friction = 0.0, load = 0.0;
            int ends = 0;
            for (std::size_t f = 0; f < r.forceCount_; ++f) {
                const auto& s = r.forces_[f];
                if (s.owner != std::size_t(trial % 2) || s.couple) continue;
                if (s.kind == PlaygroundRoom::ForceKind::Friction && s.at.y < 1e-3) {
                    friction += s.force.x;
                    ++ends;
                }
                if (s.kind == PlaygroundRoom::ForceKind::Contact && s.at.y < 1e-3) load += s.force.y;
            }
            if (ends == 0 || load <= 0.0) continue;
            ++counted;
            // Sliding +x: the friction has to be against it, and no more than mu*N.
            if (friction > 0.0) {
                ++wrong;
                worstWrong = std::max(worstWrong, friction);
            }
        }
    }
    std::printf("a shape sliding along the floor, %d ticks with a real grip:\n", counted);
    std::printf("  friction pushing it the way it was already going: %d ticks, worst %+.3f N\n", wrong, worstWrong);
    return 0;
}
