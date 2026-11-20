// Where does the energy come from?
//
// The King's picture is a triangle that has been knocked over onto its side with the
// seesaw's plank lying across it and a cube on the plank -- and the room is drawing a
// 19.7 N grey arrow out of the triangle's long face. Grey is the air. The air can
// only press that hard on something going through it at three metres a second, so
// the triangle is being launched, and the question is by what.
//
// This finds it: a wide sweep of that arrangement, and for each one the total energy
// the room holds is followed tick by tick against what it started with. Anything that
// climbs is energy that came from nowhere -- a contact adding momentum, a sweep that
// shortens a step without taking the speed, a shape put back somewhere with the speed
// that took it there still in it. The tick it happens on is printed with the state.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr double kDt = 1.0 / 20.0;

double energy(const PlaygroundRoom& r) {
    double kinetic = 0.0, potential = 0.0;
    r.energyBudget(kinetic, potential);
    return kinetic + potential;
}
}  // namespace

int main() {
    double worstGain = 0.0;
    int worstTrial = -1, worstTick = -1;
    double worstDragArrow = 0.0;
    int dragTrial = -1;
    for (int trial = 0; trial < 240; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 30; ++i) r.advance(kDt);
        auto& t = r.bodies_[PlaygroundRoom::kTriangle];
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        auto& c = r.bodies_[PlaygroundRoom::kCube];
        // The triangle knocked over onto a corner: it is a right triangle, and what the
        // King's picture shows is it lying on one of its two short sides.
        t.angle = -0.9 - 0.55 * double(trial % 9) / 8.0;
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        t.previousAngle = t.angle;
        t.frozen = false;
        // The plank let go across its slope.
        s.angle = -0.9 + 1.5 * double((trial / 9) % 7) / 6.0;
        s.centre = s.pivotPoint;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.frozen = false;
        // And the cube set down on it, higher up each time.
        c.frozen = false;
        c.centre = {s.centre.x + 0.30, s.centre.y + 0.80 + 0.12 * double((trial / 63) % 4), 0};
        c.angle = 0.3 * double(trial % 5);
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        const double start = energy(r);
        double peak = start;
        const bool watch = std::getenv("WATCH") && trial == std::atoi(std::getenv("WATCH"));
        double before = start;
        for (int k = 0; k < 400; ++k) {
            before = energy(r);
            std::array<Vec3, PlaygroundRoom::kBodyCount> wasSpeed{};
            for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) wasSpeed[i] = r.bodies_[i].velocity;
            r.advance(kDt);
            const double now = energy(r);
            if (watch && now - before > 0.5) {
                std::printf("t%3d JUMP %+.3f J (now %.2f)\n", k, now - before, now);
                for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
                    const auto& b = r.bodies_[i];
                    std::printf("   body %zu a%+.4f v(%+.3f,%+.3f) w%+.3f | was v(%+.3f,%+.3f) frz%d | c(%+.3f,%+.3f)\n",
                                i, b.angle, b.velocity.x, b.velocity.y, b.spin, wasSpeed[i].x, wasSpeed[i].y,
                                int(b.frozen), b.centre.x, b.centre.y);
                }
                for (std::size_t i = 0; i < r.forceCount_; ++i) {
                    const auto& f = r.forces_[i];
                    if (length(f.force) < 1.0 && !f.couple) continue;
                    std::printf("     kind %d owner %zu at (%+.3f,%+.3f) f (%+.2f,%+.2f) |f| %.2f %s\n", int(f.kind),
                                f.owner, f.at.x, f.at.y, f.force.x, f.force.y, length(f.force),
                                f.couple ? "COUPLE" : "");
                }
            }
            if (now - start > worstGain) {
                worstGain = now - start;
                worstTrial = trial;
                worstTick = k;
            }
            peak = std::max(peak, now);
            for (std::size_t i = 0; i < r.forceCount_; ++i) {
                if (r.forces_[i].kind != PlaygroundRoom::ForceKind::Drag) continue;
                const double magnitude = length(r.forces_[i].force);
                if (magnitude > worstDragArrow) {
                    worstDragArrow = magnitude;
                    dragTrial = trial;
                }
            }
        }
        if (worstGain > 5.0 && std::getenv("SHOW")) {
            std::printf("trial %d: energy climbed %.2f J by tick %d (start %.2f, peak %.2f)\n", trial, worstGain,
                        worstTick, start, peak);
        }
    }
    std::printf("worst energy gain over the start, any arrangement: %+.3f J (trial %d tick %d)\n", worstGain,
                worstTrial, worstTick);
    std::printf("worst air-drag arrow drawn anywhere: %.2f N (trial %d)\n", worstDragArrow, dragTrial);
    return 0;
}
