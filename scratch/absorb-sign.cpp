// Every contact arrow on one body, tick by tick, while it comes down onto the floor
// and while it rests there. A contact can only push; a body's arrows must add to the
// change in its momentum and to nothing else.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
    const double dt = 1.0 / 20.0;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(dt);
    const std::size_t which = argc > 1 ? std::size_t(std::atoi(argv[1])) : 1;  // the triangle
    auto& b = r.bodies_[which];
    b.frozen = false;
    b.velocity = {};
    b.spin = 0.0;
    const int mode = argc > 2 ? std::atoi(argv[2]) : 0;
    if (mode == 1) {
        // The plank's end lying on the triangle while the triangle stands on the floor:
        // the shape is wedged, and the room has to say what is holding it.
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        s.frozen = false;
        s.angle = -0.62;
        s.spin = 0.0;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        b.frozen = false;
        b.angle = 0.0;
        b.velocity = {};
        b.spin = 0.0;
        b.centre = {-3.30, b.centre.y, 0.0};
        PlaygroundRoom::placeCorners(b);
        b.previous = b.position;
        b.previousCentre = b.centre;
        b.previousAngle = b.angle;
        r.bodies_[0].frozen = true;
        r.bodies_[0].centre = {5.0, 3.0, 0.0};
        PlaygroundRoom::placeCorners(r.bodies_[0]);
    } else {
    b.centre.y += 0.30;  // lift it a hand's width and let it fall
    }
    PlaygroundRoom::placeCorners(b);
    b.previous = b.position;
    b.previousCentre = b.centre;
    b.previousAngle = b.angle;
    std::printf("%4s %9s %9s | %s\n", "tick", "vy before", "vy after", "contact arrows on it");
    for (int k = 0; k < 40; ++k) {
        const double before = b.velocity.y;
        r.advance(dt);
        std::printf("%4d %+9.4f %+9.4f |", k, before, b.velocity.y);
        Vec3 sum{};
        for (std::size_t i = 0; i < r.forceCount_; ++i) {
            const auto& s = r.forces_[i];
            if (s.owner != which) continue;
            if (s.kind != PlaygroundRoom::ForceKind::Contact) continue;
            if (!(length(s.force) > 1e-6)) continue;
            sum = sum + s.force;
            std::printf(" (%+.1f,%+.1f)@(%+.2f,%+.2f)", s.force.x, s.force.y, s.at.x, s.at.y);
        }
        std::printf("  contact sum y %+8.2f N | mass %.1f kg, weight %.1f N | frozen %d\n", sum.y, b.mass,
                    b.mass * 9.8, int(b.frozen));
    }
    return 0;
}
