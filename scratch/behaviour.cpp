// How the seesaw behaves: the plank pushed and left to swing, a cube sliding down it,
// and the ball dropped on the raised end. The same three things on every build.
#include "common.h"
#include "client/playground_shapes.h"
#include <cstdio>
int main() {
    const double dt = 1.0 / 20.0;
    // 1. The plank pushed and left: where is it, tick by tick?
    {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 40; ++i) r.advance(dt);
        auto& p = r.bodies_[2];
        p.frozen = false;
        p.spin = 2.0;
        std::printf("1. plank pushed to 2 rad/s:");
        for (int i = 0; i < 40; ++i) {
            r.advance(dt);
            if (i % 4 == 3) std::printf(" %+.3f", p.angle);
        }
        std::printf("   (rad every 0.2 s)\n");
    }
    // 2. A cube put on the raised end and let slide.
    {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 40; ++i) r.advance(dt);
        auto& s = r.bodies_[2];
        auto& c = r.bodies_[0];
        s.angle = 0.5;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.frozen = false;
        c.frozen = false;
        c.centre = {s.centre.x + 0.75 * std::cos(0.5), s.centre.y + 0.75 * std::sin(0.5) + 0.40, 0};
        c.angle = 0.5;
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        std::printf("2. cube slid down the ramp:");
        for (int i = 0; i < 100; ++i) {
            r.advance(dt);
            if (i % 20 == 19) std::printf(" %+.3f", c.centre.x);
        }
        std::printf("   (x every second)\n");
    }
    // 3. The ball dropped on the raised end: how fast does the plank answer?
    {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 40; ++i) r.advance(dt);
        auto& s = r.bodies_[2];
        s.angle = 0.45;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.frozen = false;
        r.ball_.position = {s.centre.x + 0.75, s.centre.y + 0.95, 0};
        r.ball_.velocity = {};
        r.ball_.spin = 0.0;
        r.ball_.frozen = false;
        r.ball_.previous = r.ball_.position;
        std::printf("3. ball on the raised end, plank angle:");
        for (int i = 0; i < 60; ++i) {
            r.advance(dt);
            if (i % 6 == 5) std::printf(" %+.3f", s.angle);
        }
        std::printf("   (rad every 0.3 s)\n");
    }
    return 0;
}
