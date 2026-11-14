// A cube resting on a slope, held there by friction, with every arrow the room draws.
// The one arrangement where the answer is known exactly: the normal is square to the
// slope and worth W cos(a), the friction lies along it and is worth W sin(a), and the
// two of them add up to the weight.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace {
constexpr double kDt = 1.0 / 20.0;
const char* kindName(int k) {
    switch (k) { case 0: return "gravity"; case 1: return "contact"; case 2: return "friction";
                 case 3: return "pair"; case 4: return "ball"; case 5: return "drag"; default: return "rolling"; }
}
const char* bodyName(std::size_t i) { return i == 0 ? "cube" : i == 1 ? "triangle" : i == 2 ? "plank" : "?"; }
}
int main(int argc, char** argv) {
    const double deg = argc > 1 ? std::atof(argv[1]) : 30.0;
    const double a = deg * 3.14159265358979323846 / 180.0;
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 40; ++k) r.advance(kDt);
    // Tip the triangle so its hypotenuse is a slope of `deg`, and set the cube down on
    // that face, square to it.
    auto& t = r.bodies_[1];
    t.angle = -0.7853981634 + a;   // the hypotenuse was at -45 deg
    PlaygroundRoom::placeCorners(t);
    t.previous = t.position;
    t.previousCentre = t.centre;
    t.previousAngle = t.angle;
    // The ramp is held still. A free triangle rotated onto its corner would simply
    // fall back onto its face -- the room is right to do that, and it is not the
    // question being asked here. What is being asked is what the arrows say about a
    // shape resting on a slope, so the slope is made a wall.
    t.frozen = true;
    t.velocity = {};
    t.spin = 0.0;
    const Vec3 normal{-std::sin(t.angle - 0.7853981634), std::cos(t.angle - 0.7853981634), 0.0};
    auto& c = r.bodies_[0];
    // The middle of the hypotenuse, moved out along its own normal by half a cube.
    const Vec3 mid{(t.position[1].x + t.position[2].x) * 0.5, (t.position[1].y + t.position[2].y) * 0.5, 0};
    c.centre = mid + normal * (0.35 + 0.002);
    c.angle = t.angle - 0.7853981634;
    PlaygroundRoom::placeCorners(c);
    c.previous = c.position;
    c.previousCentre = c.centre;
    c.previousAngle = c.angle;
    c.velocity = {};
    c.spin = 0.0;
    c.frozen = false;
    for (int k = 0; k < 60; ++k) r.advance(kDt);
    std::printf("a %g deg slope, 3 s later: cube centre (%+.4f,%+.4f) angle %+.4f v (%+.4f,%+.4f)\n", deg, c.centre.x,
                c.centre.y, c.angle, c.velocity.x, c.velocity.y);
    std::printf("  the face's outward normal (%+.4f,%+.4f)\n", normal.x, normal.y);
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
        const auto& b = r.bodies_[i];
        Vec3 sum{};
        std::printf("  %-8s:\n", bodyName(i));
        for (std::size_t f = 0; f < r.forceCount_; ++f) {
            const auto& s = r.forces_[f];
            if (s.owner != i || s.couple) continue;
            if (length(s.force) < 1e-9) continue;
            std::printf("    %-8s F (%+8.3f,%+8.3f) at (%+.3f,%+.3f)", kindName(int(s.kind)), s.force.x, s.force.y,
                        s.at.x, s.at.y);
            if (s.kind == PlaygroundRoom::ForceKind::Pair || s.kind == PlaygroundRoom::ForceKind::Contact) {
                const double along = s.force.x * normal.x + s.force.y * normal.y;
                std::printf("   | along the face normal %+7.3f, across it %+7.3f", along,
                            s.force.x * normal.y - s.force.y * normal.x);
            }
            std::printf("\n");
            sum = sum + s.force;
        }
        std::printf("    SUM (%+8.3f,%+8.3f)\n", sum.x, sum.y);
    }
    return 0;
}
