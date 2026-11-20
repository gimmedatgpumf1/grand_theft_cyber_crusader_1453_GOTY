// Everything the room can be put through, including the hand.
//
// Every probe so far set shapes down and let physics run. A player does something
// else as well: he picks things up with the mouse. The drag path writes a body's
// place directly and zeroes its speed, and the tick after a release starts from a
// pose that no step ever took it to -- which is exactly the sort of seam a pump can
// hide in.
//
// So: a long fuzz of drags, releases, throws, ball shots and plank swings. On every
// tick it looks for the one thing the King's picture shows -- a body drawn with a
// real force that comes from its speed, while its own place has not moved. That is a
// body holding momentum it never spent, and the moment it is found the whole state
// is printed.
#include "common.h"
#include "client/playground_pair.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr double kDt = 1.0 / 20.0;

unsigned nextRandom(unsigned& state) {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}
double uniform(unsigned& state, double lo, double hi) {
    return lo + (hi - lo) * (double(nextRandom(state) % 100000) / 100000.0);
}

struct Caught {
    bool found = false;
    double arrow = 0.0;
    double speed = 0.0;
    double moved = 0.0;
    double turned = 0.0;
    int tick = -1;
    std::size_t body = 0;
    double ticksHeld = 0.0;
};
}  // namespace

int main(int argc, char** argv) {
    const int runs = argc > 1 ? std::atoi(argv[1]) : 40;
    const int ticksPerRun = argc > 2 ? std::atoi(argv[2]) : 1500;
    Caught caught;
    unsigned state = 12345;
    int drags = 0;
    for (int run = 0; run < runs; ++run) {
        PlaygroundRoom r;
        r.reset();
        int heldTicks = 0;
        for (int k = 0; k < ticksPerRun; ++k) {
            // Now and then, a hand: pick something up, carry it, put it down.
            if (r.dragTarget_ == PlaygroundRoom::DragTarget::None && nextRandom(state) % 23 == 0) {
                r.setDragRay({uniform(state, -4.8, 4.8), uniform(state, 0.15, 2.4), 3.0}, {0.0, 0.0, -1.0});
                r.beginDrag();
                ++drags;
                heldTicks = 0;
            }
            if (r.dragTarget_ != PlaygroundRoom::DragTarget::None) {
                // The cursor wanders, the way a hand does.
                r.setDragRay({uniform(state, -4.8, 4.8), uniform(state, 0.05, 2.6), 3.0}, {0.0, 0.0, -1.0});
                if (++heldTicks > 12) r.endDrag();
            }
            if (nextRandom(state) % 211 == 0) {
                r.ball_.position = {uniform(state, -4.5, 4.5), uniform(state, 1.0, 2.5), 0};
                r.ball_.velocity = {uniform(state, -6.0, 6.0), uniform(state, -6.0, 1.0), 0};
                r.ball_.spin = uniform(state, -12.0, 12.0);
                r.ball_.frozen = false;
                r.ball_.previous = r.ball_.position;
            }
            if (nextRandom(state) % 307 == 0) {
                auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
                s.spin = uniform(state, -9.0, 9.0);
                s.frozen = false;
            }
            std::array<Vec3, PlaygroundRoom::kBodyCount> was{};
            std::array<double, PlaygroundRoom::kBodyCount> wasAngle{};
            for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
                was[i] = r.bodies_[i].centre;
                wasAngle[i] = r.bodies_[i].angle;
            }
            r.advance(kDt);
            // Look for what the picture shows: a force that is a function of speed,
            // drawn on something whose place has not moved.
            for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
                const auto& b = r.bodies_[i];
                if (!b.count) continue;
                double arrow = 0.0;
                for (std::size_t f = 0; f < r.forceCount_; ++f)
                    if (r.forces_[f].owner == i && r.forces_[f].kind == PlaygroundRoom::ForceKind::Drag)
                        arrow = std::max(arrow, length(r.forces_[f].force));
                const double moved = length(b.centre - was[i]);
                const double turned = std::abs(b.angle - wasAngle[i]);
                const double speed = length(b.velocity) + std::abs(b.spin);
                if (arrow > 6.0 && moved < 1e-3 && speed > 1.0) {
                    if (arrow > caught.arrow) {
                        caught = Caught{true, arrow, speed, moved, turned, k, i, 0.0};
                        std::printf("CAUGHT at run %d tick %d: body %zu drawn with %.2f N of air,\n", run, k, i, arrow);
                        std::printf("  holding %.3f m/s (v %+.3f,%+.3f spin %+.3f) while its centre moved %.9f m\n",
                                    speed, b.velocity.x, b.velocity.y, b.spin, moved);
                        std::printf("  body a%+.4f c(%+.4f,%+.4f) frz%d\n", b.angle, b.centre.x, b.centre.y,
                                    int(b.frozen));
                        for (std::size_t q = 0; q < PlaygroundRoom::kBodyCount; ++q) {
                            const auto& o = r.bodies_[q];
                            if (!o.count) continue;
                            std::printf("    body %zu a%+.4f v(%+.3f,%+.3f) w%+.3f frz%d |", q, o.angle, o.velocity.x,
                                        o.velocity.y, o.spin, int(o.frozen));
                            for (std::size_t c = 0; c < o.count; ++c)
                                std::printf(" (%+.3f,%+.3f)", o.position[c].x, o.position[c].y);
                            std::printf("\n");
                        }
                        std::printf("    ball (%+.3f,%+.3f) v(%+.3f,%+.3f) | drag %d\n", r.ball_.position.x,
                                    r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, int(r.dragTarget_));
                        for (std::size_t f = 0; f < r.forceCount_; ++f) {
                            const auto& fx = r.forces_[f];
                            if (length(fx.force) < 0.5 && !fx.couple) continue;
                            std::printf("      kind %d owner %zu at (%+.3f,%+.3f) f (%+.1f,%+.1f)\n", int(fx.kind),
                                        fx.owner, fx.at.x, fx.at.y, fx.force.x, fx.force.y);
                        }
                    }
                }
            }
        }
    }
    (void)drags;
    if (!caught.found)
        std::printf("nothing caught: %d drags, %d ticks, and no body was ever drawn with air it did not earn\n",
                   drags, runs * ticksPerRun);
    return 0;
}
