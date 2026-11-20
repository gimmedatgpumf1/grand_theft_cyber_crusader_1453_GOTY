#include "common.h"
#include "client/playground_shapes.h"
#include <cstdio>
int main() {
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(0.05);
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
    for (int i = 0; i < 600; ++i) r.advance(0.05);
    double low = 1e9;
    for (std::size_t i = 0; i < c.count; ++i) low = std::min(low, c.position[i].y);
    std::printf("cube centre (%+.3f,%+.3f) lowest corner %.4f frozen %d spin %+.4f v(%+.4f,%+.4f)\n", c.centre.x,
                c.centre.y, low, int(c.frozen), c.spin, c.velocity.x, c.velocity.y);
    std::printf("plank a %+.4f ends", s.angle);
    for (std::size_t i = 0; i < s.count; ++i) std::printf(" (%+.3f,%+.3f)", s.position[i].x, s.position[i].y);
    std::printf("\n");
    std::printf("cube: restTicks %d touchingPlane %d onBody %d supportFrozen %d target %d | y %.6f vy %.6f\n",
                int(c.restTicks), int(c.touchingPlane), int(c.onBody), int(c.supportFrozen), int(r.dragTarget_),
                c.centre.y, c.velocity.y);
    std::printf("ball (%+.3f,%+.3f)\n", r.ball_.position.x, r.ball_.position.y);
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& b = r.bodies_[i];
        std::printf("body %zu:", i);
        for (std::size_t c2 = 0; c2 < b.count; ++c2)
            std::printf(" (%+.3f,%+.3f)", b.position[c2].x, b.position[c2].y);
        std::printf("  frozen %d\n", int(b.frozen));
    }
    // The gap from the cube to each other body, as the room measures it.
    for (std::size_t i = 1; i < 3; ++i) {
        double best = 1e9;
        for (std::size_t c2 = 0; c2 < c.count; ++c2)
            for (std::size_t e = 0; e < r.bodies_[i].count; ++e) {
                const Vec3 a = r.bodies_[i].position[e], b2 = r.bodies_[i].position[(e + 1) % r.bodies_[i].count];
                const Vec3 side = b2 - a;
                const double l2 = side.x * side.x + side.y * side.y;
                const double t = l2 > 0 ? std::clamp(dot(c.position[c2] - a, side) / l2, 0.0, 1.0) : 0.0;
                best = std::min(best, length(c.position[c2] - (a + side * t)));
            }
        std::printf("cube's nearest corner to body %zu: %.4f m\n", i, best);
    }
    double k, p;
    r.energyBudget(k, p);
    std::printf("KE %.5f PE %.3f\n", k, p);
    return 0;
}
