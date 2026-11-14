// The King's picture, built exactly as it looks, and watched until it breaks.
//
// What the picture shows: the triangle upside down (horizontal edge on top, one
// vertex down on the floor), the plank lying across it, the cube on the plank -- and
// a 19.7 N grey arrow standing out of the triangle's top face. Grey is the air, and
// the air is the only force in the room that is a function of speed, so that arrow
// says the triangle is doing about three metres a second. The triangle is not going
// anywhere, which leaves one possibility: it is holding speed and spending none of
// it.
//
// This hunts for exactly that, in that arrangement, and prints the first tick it
// happens on with everything that was moving.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr double kDt = 1.0 / 20.0;

double energyOf(const PlaygroundRoom& r) {
    double kinetic = 0.0, potential = 0.0;
    r.energyBudget(kinetic, potential);
    return kinetic + potential;
}

struct Hold {
    double speed = 0.0;
    double moved = 0.0;
    double turned = 0.0;
    double arrow = 0.0;
    int trial = -1, tick = -1;
    std::size_t body = 0;
};
}  // namespace

int main() {
    Hold worst;
    int trials = 0;
    // The triangle upside down: angle near pi, resting on the vertex opposite its
    // right angle. The plank across it at a range of angles. The cube on the plank.
    for (int ti = 0; ti < 9; ++ti) {
        for (int pi = 0; pi < 9; ++pi) {
            PlaygroundRoom r;
            r.reset();
            for (int i = 0; i < 30; ++i) r.advance(kDt);
            auto& t = r.bodies_[PlaygroundRoom::kTriangle];
            auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
            auto& c = r.bodies_[PlaygroundRoom::kCube];
            const double triAngle = 2.70 + 0.10 * double(ti);
            t.angle = triAngle;
            t.centre = {-2.30, 1.60, 0};
            PlaygroundRoom::placeCorners(t);
            // Down onto the floor, never through it -- and if the floor would be
            // crossed, lift it until it is not.
            double low = 1e9;
            for (std::size_t i = 0; i < t.count; ++i) low = std::min(low, t.position[i].y);
            t.centre.y -= low;
            PlaygroundRoom::placeCorners(t);
            t.previous = t.position;
            t.previousCentre = t.centre;
            t.previousAngle = t.angle;
            t.velocity = {};
            t.spin = 0.0;
            t.frozen = false;
            const double plankAngle = -1.00 + 0.25 * double(pi);
            s.angle = plankAngle;
            PlaygroundRoom::placeCorners(s);
            s.previous = s.position;
            s.previousCentre = s.centre;
            s.previousAngle = s.angle;
            s.spin = 0.0;
            s.frozen = false;
            // The cube, put down on whichever part of the plank is clear.
            c.frozen = false;
            c.angle = 0.30 * double(ti % 4);
            c.velocity = {};
            c.spin = 0.0;
            c.centre = {s.centre.x + 0.70 * std::cos(plankAngle), s.centre.y + 0.70 * std::sin(plankAngle) + 0.45, 0};
            PlaygroundRoom::placeCorners(c);
            c.previous = c.position;
            c.previousCentre = c.centre;
            c.previousAngle = c.angle;
            // A setup with shapes already inside each other is not the room's doing.
            bool legal = true;
            for (std::size_t a = 0; a < PlaygroundRoom::kBodyCount && legal; ++a)
                for (std::size_t b = 0; b < PlaygroundRoom::kBodyCount && legal; ++b) {
                    if (a == b || !r.bodies_[a].count || !r.bodies_[b].count) continue;
                    for (std::size_t k = 0; k < r.bodies_[a].count; ++k)
                        if (ball_shape::outlineGap(r.bodies_[a].position[k], r.bodies_[b].position.data(),
                                                   r.bodies_[b].position.data(), r.bodies_[b].count, 1.0)
                                .gap < -1e-3)
                            legal = false;
                }
            if (!legal) continue;
            ++trials;
            for (int k = 0; k < 400; ++k) {
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
                    const double speed = length(b.velocity) + std::abs(b.spin);
                    if (speed < 0.3) continue;
                    if (moved > 1e-3 || turned > 1e-3) continue;
                    // Speed it is holding, travel it did not get: find the arrow that
                    // speed is making him draw.
                    double arrow = 0.0;
                    for (std::size_t f = 0; f < r.forceCount_; ++f)
                        if (r.forces_[f].owner == i && r.forces_[f].kind == PlaygroundRoom::ForceKind::Drag)
                            arrow = std::max(arrow, length(r.forces_[f].force));
                    if (speed > worst.speed) {
                        worst = Hold{speed, moved, turned, arrow, ti * 9 + pi, k, i};
                    }
                }
            }
        }
    }
    std::printf("arrangements that were legal to set up: %d\n", trials);
    std::printf("worst body holding speed it never spent: %.3f m/s for %.6f m of travel\n", worst.speed, worst.moved);
    std::printf("  trial %d tick %d body %zu, and the air arrow drawn on it: %.2f N\n", worst.trial, worst.tick,
                worst.body, worst.arrow);
    return 0;
}
