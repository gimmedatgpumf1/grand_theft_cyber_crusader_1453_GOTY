// What the arrows add up to, and where the biggest one comes from.
//
// The King's picture has a 19.7 N arrow standing out of the top of a shape. That
// colour is the air. This walks a wide set of the room's own arrangements, and for
// every tick it asks two things:
//
//   * does each solid's ledger sum to zero -- is anything drawn being pulled or
//     pushed with nothing on the other end of it?
//   * what is the largest single arrow drawn, and what is the body doing when it is
//     drawn? An arrow out of a body that is not moving is a conjured force, whatever
//     the arithmetic that produced it.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr double kDt = 1.0 / 20.0;
}

int main() {
    double worstNet = 0.0;
    int worstNetBody = -1, worstNetTrial = -1, worstNetTick = -1;
    double worstDrag = 0.0, worstDragSpeed = 0.0;
    int worstDragBody = -1, worstDragTrial = -1;
    for (int trial = 0; trial < 120; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 30; ++i) r.advance(kDt);
        // The King's arrangement, varied: the triangle tipped onto a corner, the
        // plank let go across it, the cube thrown in for good measure.
        auto& t = r.bodies_[PlaygroundRoom::kTriangle];
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        t.angle = -2.4 + 0.5 * double(trial % 7) / 6.0;
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        t.previousAngle = t.angle;
        t.frozen = false;
        s.angle = 0.2 + 0.9 * double(trial % 5) / 4.0;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.frozen = false;
        auto& c = r.bodies_[PlaygroundRoom::kCube];
        c.frozen = false;
        c.centre = {s.centre.x + 0.9 * std::cos(s.angle), s.centre.y + 0.9 * std::sin(s.angle) + 0.9, 0};
        c.angle = s.angle * 0.5;
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        for (int k = 0; k < 300; ++k) {
            r.advance(kDt);
            std::array<Vec3, PlaygroundRoom::kBodyCount + 2> net{};
            for (std::size_t i = 0; i < r.forceCount_; ++i) {
                const auto& f = r.forces_[i];
                if (f.couple) continue;  // a moment is not a force
                if (f.owner < net.size()) net[f.owner] = net[f.owner] + f.force;
            }
            for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
                if (!r.bodies_[i].count) continue;
                const double magnitude = length(net[i]);
                if (magnitude > worstNet) {
                    worstNet = magnitude;
                    worstNetBody = int(i);
                    worstNetTrial = trial;
                    worstNetTick = k;
                }
            }
            for (std::size_t i = 0; i < r.forceCount_; ++i) {
                const auto& f = r.forces_[i];
                if (f.kind != PlaygroundRoom::ForceKind::Drag) continue;
                const double magnitude = length(f.force);
                if (magnitude > worstDrag) {
                    worstDrag = magnitude;
                    worstDragBody = int(f.owner);
                    worstDragTrial = trial;
                    if (f.owner < PlaygroundRoom::kBodyCount)
                        worstDragSpeed = length(r.bodies_[f.owner].velocity) +
                                         std::abs(r.bodies_[f.owner].spin) * 1.0;
                }
            }
        }
    }
    std::printf("worst net force drawn on one solid: %.3f N (body %d, trial %d tick %d)\n", worstNet, worstNetBody,
                worstNetTrial, worstNetTick);
    std::printf("  -- a solid at rest must show 0.000 N net, or the picture has a force with no partner\n");
    std::printf("largest air-drag arrow drawn: %.2f N (body %d, trial %d, that body moving %.2f m/s)\n", worstDrag,
                worstDragBody, worstDragTrial, worstDragSpeed);
    return 0;
}
