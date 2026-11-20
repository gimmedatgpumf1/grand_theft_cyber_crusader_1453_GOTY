// A cube left on the plank while the plank swings. Does the cube ride the face,
// slide down it the way a real cube would, or do something a real cube cannot do?
#include "common.h"
#include "client/playground_shapes.h"
using namespace gtc::native::playground_detail;
#include <algorithm>
#include <cmath>
#include <cstdio>

int main() {
    const double dt = 1.0 / 20.0;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(dt);
    auto& s = r.bodies_[2];  // the plank
    auto& c = r.bodies_[0];  // the square

    // Start the plank at +0.20 rad (left end low) and stand the cube on the plank a
    // third of the way along from the axle, resting flush on the face.
    s.frozen = false;
    s.angle = 0.20;
    s.spin = 0.0;
    s.velocity = {};
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;

    const double along = 0.55;  // metres from the axle, toward the low end
    c.frozen = false;
    c.angle = s.angle;
    c.velocity = {};
    c.spin = 0.0;
    const Vec3 up{-std::sin(s.angle), std::cos(s.angle), 0};
    const Vec3 dir{std::cos(s.angle), std::sin(s.angle), 0};
    c.centre = s.centre + dir * along + up * (kSeesawHalfThick + kHalfSize + 1e-6);
    PlaygroundRoom::placeCorners(c);
    c.previous = c.position;
    c.previousCentre = c.centre;
    c.previousAngle = c.angle;

    std::printf("%4s %8s %8s | %8s %8s %8s | %7s %7s %7s | %8s\n", "tick", "plankA", "plankW", "cubeX", "cubeY",
                "cubeA", "vx", "vy", "w", "gap");
    for (int k = 0; k < 120; ++k) {
        r.advance(dt);
        // the cube's own frame, relative to the plank face
        const Vec3 u{-std::sin(s.angle), std::cos(s.angle), 0};
        const Vec3 d{std::cos(s.angle), std::sin(s.angle), 0};
        const Vec3 rel = c.centre - s.centre;
        const double along2 = dot(rel, d);
        const double perp = dot(rel, u) - kSeesawHalfThick - kHalfSize;
        const double gap = gapToBody(r, 0);
        (void)along2;
        std::printf("%4d %+8.4f %+8.4f | %+8.4f %+8.4f %+8.4f | %+7.4f %+7.4f %+7.4f | %+8.4f", k,
                    s.angle, s.spin, c.centre.x, c.centre.y, c.angle, c.velocity.x, c.velocity.y, c.spin, gap);
        std::printf(" | frz %d onBody %d applied", int(c.frozen), int(c.onBody));
        for (std::size_t a = 0; a < c.appliedCount; ++a)
            std::printf(" %d:(%+.3f,%+.3f)", int(c.applied[a].kind), c.applied[a].impulse.x, c.applied[a].impulse.y);
        std::printf("\n");
        if (k % 8 == 7) std::printf("      along %+7.4f  perp %+9.6f  relToPlankAngle %+7.4f\n", along2, perp,
                                    c.angle - s.angle);
    }
    return 0;
}
