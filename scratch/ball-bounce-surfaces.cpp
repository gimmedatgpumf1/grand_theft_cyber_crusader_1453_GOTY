// The same ball dropped the same height onto: the floor, the flat top of the cube, the
// triangle's upright side, and the triangle's 45 degree hypotenuse. How much of the
// arrival comes back, measured on each surface's own normal.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace {
constexpr double kDt = 1.0 / 20.0;
struct Out { double arrived = 0.0, left = 0.0; };
Out drop(PlaygroundRoom& r, Vec3 at, Vec3 normal) {
    r.ball_.position = at;
    r.ball_.previous = at;
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    double best = 0.0, arrived = 0.0, deepest = 1e30;
    bool hit = false;
    for (int k = 0; k < 160; ++k) {
        const double before = dot(r.ball_.velocity, normal);
        r.advance(kDt);
        const double after = dot(r.ball_.velocity, normal);
        if (!hit) {
            // The arrival is the fastest the ball was ever closing with the surface.
            deepest = std::min(deepest, before);
            if (after > 0.0 && deepest < -0.2) {
                hit = true;
                arrived = deepest;
            }
        }
        if (hit) best = std::max(best, after);
    }
    return Out{arrived, best};
}
}
int main() {
    {
        PlaygroundRoom r; r.reset();
        for (int k = 0; k < 20; ++k) r.advance(kDt);
        const auto o = drop(r, {2.20, 1.60, 0}, {0, 1, 0});
        std::printf("  the floor:        arrived %+.2f m/s, came back %+.2f m/s (%.0f%%)\n", o.arrived, o.left,
                    100.0 * o.left / -o.arrived);
    }
    {
        PlaygroundRoom r; r.reset();
        for (int k = 0; k < 20; ++k) r.advance(kDt);
        const auto& c = r.bodies_[0];
        const auto o = drop(r, {c.centre.x, c.centre.y + 0.35 + 0.12 + 0.60, 0}, {0, 1, 0});
        std::printf("  the cube's top:   arrived %+.2f m/s, came back %+.2f m/s (%.0f%%)\n", o.arrived, o.left,
                    100.0 * o.left / -o.arrived);
    }
    {
        PlaygroundRoom r; r.reset();
        for (int k = 0; k < 20; ++k) r.advance(kDt);
        const auto o = drop(r, {-2.98, 0.12 + 0.60, 0}, {0, 1, 0});  // onto the triangle's flat foot
        std::printf("  the triangle's foot: arrived %+.2f m/s, came back %+.2f m/s (%.0f%%)\n", o.arrived, o.left,
                    100.0 * o.left / -o.arrived);
    }
    {
        PlaygroundRoom r; r.reset();
        for (int k = 0; k < 20; ++k) r.advance(kDt);
        const Vec3 n{0.7071067811865476, 0.7071067811865476, 0.0};
        const Vec3 onFace{-1.55, 1.05, 0};
        const auto o = drop(r, onFace + n * (0.12 + 0.60), n);
        std::printf("  the hypotenuse:   arrived %+.2f m/s, came back %+.2f m/s (%.0f%%)\n", o.arrived, o.left,
                    100.0 * o.left / -o.arrived);
    }
    return 0;
}
