// The King's 19.7 N arrow: air drawn against a shape that is standing still.
//
// Grey is the air. Air is the only force in this room that is a function of speed,
// so a big grey arrow is the room saying "this shape is travelling at three metres a
// second". If the shape has not moved, the room is drawing a force from a speed that
// has no journey behind it -- and that is a shape holding momentum it never spends,
// which is the pump.
//
// This drives the room the way it gets driven in play -- throws, drops, plank swings,
// a cube on the slope -- and reports the worst case of exactly that: the largest air
// arrow drawn against a body that has not moved a millimetre this tick.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr double kDt = 1.0 / 20.0;

struct Finding {
    double worstArrow = 0.0;
    double bodySpeed = 0.0;
    double bodyMoved = 0.0;
    int trial = -1, tick = -1;
    std::size_t body = 0;
};

Finding sweep() {
    Finding found;
    for (int trial = 0; trial < 72; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 30; ++i) r.advance(kDt);
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        auto& c = r.bodies_[PlaygroundRoom::kCube];
        auto& t = r.bodies_[PlaygroundRoom::kTriangle];
        const int mode = trial % 6;
        // A plank swung down hard, over a pile of the room's other shapes.
        t.centre = {-1.9, 2.4 + 0.2 * double(trial % 4), 0};
        t.angle = -0.2 * double(trial % 5);
        PlaygroundRoom::placeCorners(t);
        {
            double low = 1e9;
            for (std::size_t i = 0; i < t.count; ++i) low = std::min(low, t.position[i].y);
            if (low < 0.0) t.centre.y -= low;
            PlaygroundRoom::placeCorners(t);
            t.previous = t.position;
            t.previousCentre = t.centre;
            t.previousAngle = t.angle;
            t.velocity = {};
            t.spin = 0.0;
            t.frozen = false;
        }
        c.centre = {-2.5, 2.0, 0};
        c.angle = 0.4 * double(trial % 5);
        c.velocity = {};
        c.spin = 0.0;
        c.frozen = false;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        s.spin = (mode == 0 ? -9.0 : (mode == 1 ? 9.0 : 0.0));
        s.angle = 0.5 * double(trial % 3);
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.frozen = false;
        if (mode >= 2) r.ball_.position = {-2.2, 2.1, 0}, r.ball_.velocity = {0.0, -3.0, 0}, r.ball_.frozen = false;
        for (int k = 0; k < 260; ++k) {
            std::array<Vec3, PlaygroundRoom::kBodyCount> was{};
            for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) was[i] = r.bodies_[i].centre;
            r.advance(kDt);
            for (std::size_t i = 0; i < r.forceCount_; ++i) {
                const auto& f = r.forces_[i];
                if (f.kind != PlaygroundRoom::ForceKind::Drag) continue;
                if (f.owner >= PlaygroundRoom::kBodyCount) continue;
                const auto& body = r.bodies_[f.owner];
                if (!body.count) continue;
                const double moved = length(body.centre - was[f.owner]);
                const double arrow = length(f.force);
                // Air on a shape that did not move: the only thing that can be is a
                // speed it is holding and not spending.
                if (moved > 1e-4) continue;
                if (arrow > found.worstArrow) {
                    found.worstArrow = arrow;
                    found.bodySpeed = length(body.velocity) + std::abs(body.spin);
                    found.bodyMoved = moved;
                    found.trial = trial;
                    found.tick = k;
                    found.body = f.owner;
                }
            }
        }
    }
    return found;
}
}  // namespace

int main() {
    const Finding f = sweep();
    std::printf("largest air arrow drawn on a shape that did not move: %.2f N\n", f.worstArrow);
    std::printf("  trial %d tick %d, body %zu, which was holding %.2f m/s (and moved %.6f m)\n", f.trial, f.tick,
                f.body, f.bodySpeed, f.bodyMoved);
    return 0;
}
