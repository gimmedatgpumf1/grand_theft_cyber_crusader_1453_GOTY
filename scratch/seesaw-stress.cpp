// Everything I can throw at the seesaw, with a record of every breach of the floor.
//
// Two hundred arrangements, three hundred ticks each: the plank started anywhere a
// plank can legally start, spinning either way, with a cube and a triangle dropped on
// it and the ball hurled at it. The deepest any corner of any of them ever got below
// the floor is the number under the table -- it must not be more than the room's own
// resolution, a micrometre.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

int main() {
    constexpr double S = PlaygroundRoom::kSeesaw;
    double worstPlank = 0.0, worstCube = 0.0, worstTri = 0.0, worstBall = 0.0;
    int ticks = 0;
    for (int trial = 0; trial < 200; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 10; ++k) r.advance(1.0 / 20.0);
        auto& s = r.bodies_[S];
        // Legal starts only: anywhere the whole plank is above the floor at rest.
        s.angle = (double(trial) / 200.0) * 1.7 - 0.85;
        s.spin = (double((trial * 37) % 60) - 30.0) * 0.7;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        auto& c = r.bodies_[0];
        auto& t = r.bodies_[1];
        const double dropX = -1.0 + 2.0 * double(trial % 7) / 6.0;
        c.centre = {s.centre.x + dropX, 1.2 + double(trial % 5) * 0.3, 0};
        c.angle = double(trial % 4) * 0.7;
        c.velocity = {};
        c.spin = 0.0;
        c.frozen = false;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        t.centre = {s.centre.x - dropX, 1.8, 0};
        t.angle = 0.0;
        t.velocity = {};
        t.spin = 0.0;
        t.frozen = false;
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        t.previousAngle = t.angle;
        r.ball_.position = {s.centre.x + 2.0, 0.12 + double(trial % 3) * 0.5, 0};
        r.ball_.velocity = {-2.0 - double(trial % 9), double(trial % 3) - 1.0, 0};
        r.ball_.spin = 0.0;
        r.ball_.frozen = false;
        r.ball_.previous = r.ball_.position;
        for (int k = 0; k < 300; ++k) {
            r.advance(1.0 / 20.0);
            ++ticks;
            for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
                const auto& b = r.bodies_[i];
                for (std::size_t j = 0; j < b.count; ++j) {
                    const double under = -b.position[j].y;
                    if (i == S) worstPlank = std::max(worstPlank, under);
                    if (i == 0) worstCube = std::max(worstCube, under);
                    if (i == 1) worstTri = std::max(worstTri, under);
                }
            }
            worstBall = std::max(worstBall, -r.ball_.position.y + r.ball_.radius);
        }
    }
    std::printf("over %d ticks of throwing things at it:\n", ticks);
    std::printf("  deepest the plank's corner went below the floor: %.6f m\n", worstPlank);
    std::printf("  cube %.6f m | triangle %.6f m | ball %.6f m\n", worstCube, worstTri, worstBall);
    return 0;
}
