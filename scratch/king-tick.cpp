// The King's picture, tick by tick: the cube coming down onto the triangle's slope,
// with every force the room draws printed at the place it acts.
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
    const int from = argc > 1 ? std::atoi(argv[1]) : 10, to = argc > 2 ? std::atoi(argv[2]) : 40;
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 5; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    // Flush on the triangle's hypotenuse: the face's outward normal is (0.707,0.707),
    // so the cube's own lower face has to face the other way.
    c.centre = {-1.550 + 0.7071 * 0.352, 1.050 + 0.7071 * 0.352, 0};
    c.angle = -2.3561945;
    PlaygroundRoom::placeCorners(c);
    c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    c.velocity = {}; c.spin = 0.0; c.frozen = false;
    for (int k = 0; k <= to; ++k) {
        r.advance(kDt);
        if (k < from) continue;
        std::printf("=== tick %d\n", k);
        for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
            const auto& b = r.bodies_[i];
            if (b.count == 0) continue;
            std::printf("  %-8s c(%+.3f,%+.3f) a%+.3f v(%+.3f,%+.3f) w%+.3f frz%d\n", bodyName(i), b.centre.x,
                        b.centre.y, b.angle, b.velocity.x, b.velocity.y, b.spin, int(b.frozen));
            for (std::size_t f = 0; f < r.forceCount_; ++f) {
                const auto& s = r.forces_[f];
                if (s.owner != i) continue;
                if (s.couple) { std::printf("     couple %+.3f at (%+.3f,%+.3f)\n", s.moment, s.at.x, s.at.y); continue; }
                if (length(s.force) < 1e-9) continue;
                std::printf("     %-8s (%+7.3f,%+7.3f) at (%+.3f,%+.3f)\n", kindName(int(s.kind)), s.force.x,
                            s.force.y, s.at.x, s.at.y);
            }
        }
    }
    return 0;
}
