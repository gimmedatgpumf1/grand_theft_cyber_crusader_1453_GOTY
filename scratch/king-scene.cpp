// The scene in the King's picture: the floor, the triangle standing on it, the cube
// come down onto its slope, and the plank on its axle. Nothing is placed by hand --
// the room is reset and left to run, and this is what it settles into.
//
// Every force the room draws is printed with the place it acts, so the picture can be
// checked against the physics: a normal force is square to the face it acts on, a
// friction lies along it, and on a body standing still the whole set of them adds up
// to nothing.
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
    switch (k) {
        case 0: return "gravity";
        case 1: return "contact";
        case 2: return "friction";
        case 3: return "pair";
        case 4: return "ball";
        case 5: return "drag";
        case 6: return "rolling";
        default: return "?";
    }
}
const char* bodyName(std::size_t i) { return i == 0 ? "cube" : i == 1 ? "triangle" : i == 2 ? "plank" : "?"; }
}  // namespace

int main(int argc, char** argv) {
    const int ticks = argc > 1 ? std::atoi(argv[1]) : 400;
    PlaygroundRoom r;
    r.reset();
    // The King's arrangement: the cube let go above the triangle's slope, so it comes
    // down onto the hypotenuse and is left to settle where the physics puts it.
    if (argc > 2 && std::atoi(argv[2]) == 1) {
        auto& c = r.bodies_[0];
        c.centre = {-0.85, 1.55, 0};
        c.angle = 0.30;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
    }
    for (int k = 0; k < ticks; ++k) r.advance(kDt);
    std::printf("after %d ticks (%.1f s):\n", ticks, ticks * kDt);
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
        const auto& b = r.bodies_[i];
        std::printf("\n  %-8s centre (%+.3f,%+.3f) angle %+.4f frozen %d pivoted %d contacts %d mass %.2f\n",
                    bodyName(i), b.centre.x, b.centre.y, b.angle, int(b.frozen), int(b.pivoted), b.contacts, b.mass);
        std::printf("    corners:");
        for (std::size_t q = 0; q < b.count; ++q) std::printf(" (%+.3f,%+.3f)", b.position[q].x, b.position[q].y);
        std::printf("\n");
        Vec3 sum{};
        for (std::size_t f = 0; f < r.forceCount_; ++f) {
            const auto& s = r.forces_[f];
            if (s.owner != i) continue;
            if (s.couple) {
                std::printf("    %-9s moment %+.3f N.m at (%+.3f,%+.3f)\n", "couple", s.moment, s.at.x, s.at.y);
                continue;
            }
            if (length(s.force) < 1e-9) continue;
            std::printf("    %-9s F (%+8.3f,%+8.3f) at (%+.3f,%+.3f)  |F| %7.3f\n", kindName(int(s.kind)), s.force.x,
                        s.force.y, s.at.x, s.at.y, length(s.force));
            sum = sum + s.force;
        }
        std::printf("    SUM      (%+8.3f,%+8.3f)\n", sum.x, sum.y);
    }
    // What the cube is touching and along which way, read off the outlines.
    std::printf("\n  geometry:\n");
    for (std::size_t a = 0; a < PlaygroundRoom::kBodyCount; ++a) {
        for (std::size_t b2 = a + 1; b2 < PlaygroundRoom::kBodyCount; ++b2) {
            const auto& A = r.bodies_[a];
            const auto& B = r.bodies_[b2];
            if (A.count == 0 || B.count == 0) continue;
            double best = 1e30;
            for (std::size_t i = 0; i < A.count; ++i) {
                const auto gap =
                    ball_shape::outlineGap(A.position[i], B.position.data(), B.position.data(), B.count, 1.0);
                best = std::min(best, gap.gap);
            }
            for (std::size_t i = 0; i < B.count; ++i) {
                const auto gap =
                    ball_shape::outlineGap(B.position[i], A.position.data(), A.position.data(), A.count, 1.0);
                best = std::min(best, gap.gap);
            }
            std::printf("    %s / %s: corner-to-outline %.6f m\n", bodyName(a), bodyName(b2), best);
        }
    }
    return 0;
}
