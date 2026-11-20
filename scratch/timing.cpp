// How long a tick costs, and how the plank behaves while it costs it.
#include "common.h"
#include "client/playground_shapes.h"
#include <chrono>
#include <cstdio>
int main() {
    using clock = std::chrono::steady_clock;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    auto& s = r.bodies_[2];
    auto& c = r.bodies_[0];
    c.frozen = false;
    c.centre = {s.centre.x + 0.82, 1.62, 0};
    c.angle = 0.0;
    c.velocity = {};
    c.spin = 0.0;
    PlaygroundRoom::placeCorners(c);
    c.previous = c.position;
    c.previousCentre = c.centre;
    c.previousAngle = c.angle;
    auto start = clock::now();
    for (int i = 0; i < 600; ++i) r.advance(1.0 / 20.0);
    const double ms = std::chrono::duration<double, std::milli>(clock::now() - start).count();
    std::printf("600 ticks in %.1f ms  (%.3f ms/tick, %.0f ticks/s)\n", ms, ms / 600.0, 600000.0 / ms);
    std::printf("plank a%+.4f w%+.4f | cube (%+.4f,%+.4f)\n", s.angle, s.spin, c.centre.x, c.centre.y);
    // And the plank's own response: free, alone, pushed once.
    PlaygroundRoom q;
    q.reset();
    for (int i = 0; i < 40; ++i) q.advance(1.0 / 20.0);
    auto& p = q.bodies_[2];
    p.spin = 2.0;
    start = clock::now();
    for (int i = 0; i < 40; ++i) q.advance(1.0 / 20.0);
    std::printf("2 s after a 2 rad/s push, plank is at %+.5f rad, w%+.5f\n", p.angle, p.spin);
    return 0;
}
