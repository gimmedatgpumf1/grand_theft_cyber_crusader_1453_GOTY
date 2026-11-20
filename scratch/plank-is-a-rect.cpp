// The King's question: why is the seesaw a special thing? Take the axle away and the
// plank is a rectangle. Same corners, same mass, same moment, same contacts, same
// friction, same normal forces as the square -- because it is the same code.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace { constexpr double kDt = 1.0 / 20.0; }
static void describe(const PlaygroundRoom& r, std::size_t i, const char* what) {
    const auto& b = r.bodies_[i];
    std::printf("  %-34s mass %.2f  moment %.4f  corners %zu  pinned %d  friction %.2f/%.2f\n", what, b.mass,
                b.moment, b.count, int(b.pivoted), b.staticFriction, b.kineticFriction);
}
int main() {
    PlaygroundRoom r;
    r.reset();
    describe(r, 0, "the square");
    describe(r, 1, "the triangle");
    describe(r, 2, "the seesaw, on its axle");
    // The plank's moment, worked out from its own four corners by the same function the
    // square's and the triangle's are: a uniform plate, from its outline.
    const auto& plate = r.bodies_[2];
    const double byHand = playground_detail::polygonMoment(plate.local, plate.count, plate.mass);
    std::printf("  the plank's moment from the same outline formula: %.4f\n", byHand);

    // Now take the axle away and let it fall. A rectangle does what a rectangle does.
    PlaygroundRoom q;
    q.reset();
    for (int k = 0; k < 10; ++k) q.advance(kDt);
    auto& s = q.bodies_[2];
    s.pivoted = false;
    s.centre = {0.60, 3.20, 0};
    s.angle = 0.6;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position; s.previousCentre = s.centre; s.previousAngle = s.angle;
    s.velocity = {}; s.spin = 0.0; s.frozen = false;
    std::printf("\n  the plank, unpinned, let go from 3.2 m at 34 degrees:\n");
    for (int k = 0; k < 120; ++k) {
        q.advance(kDt);
        if (k % 20 == 0 || k == 119)
            std::printf("    t%3d  centre (%+.3f,%+.3f) angle %+.3f  v (%+.3f,%+.3f) w %+.3f\n", k, s.centre.x,
                        s.centre.y, s.angle, s.velocity.x, s.velocity.y, s.spin);
    }
    double lowest = 1e30;
    for (std::size_t i = 0; i < s.count; ++i) lowest = std::min(lowest, s.position[i].y);
    std::printf("    final lowest corner %+.6f m, asleep %d\n", lowest, int(s.frozen));
    return 0;
}
