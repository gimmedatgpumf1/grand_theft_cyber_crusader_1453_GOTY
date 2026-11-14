// The King's picture: a cube standing on the floor, the triangle turned so its long
// slope comes down to the floor beside it, and the ball in the notch where the cube's
// side, the slope and the floor all meet. It never comes to rest -- it shakes.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace {
constexpr double kDt = 1.0 / 20.0;
void placeTriangle(PlaygroundRoom& r, Vec3 foot) {
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846;  // right angle bottom-right, slope down to `foot`
    t.centre = {foot.x + 1.4, 0.7, 0};
    r.placeCorners(t);
    t.previous = t.position;
    t.previousCentre = t.centre;
    t.previousAngle = t.angle;
}
}
int main(int argc, char** argv) {
    const double footX = argc > 1 ? std::atof(argv[1]) : -0.25;
    const double ballX = argc > 2 ? std::atof(argv[2]) : -0.13;
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0};
    c.angle = 0.0;
    c.velocity = {};
    c.spin = 0.0;
    c.frozen = false;
    r.placeCorners(c);
    c.previous = c.position;
    c.previousCentre = c.centre;
    c.previousAngle = c.angle;
    placeTriangle(r, {footX, 0.0});
    r.ball_.position = {ballX, 0.13, 0};
    r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    double worstEver = 0.0;
    for (int k = 0; k < 400; ++k) {
        r.advance(kDt);
        double worst = 0.0;
        Vec3 at{};
        int kind = -1;
        for (std::size_t i = 0; i < r.forceCount_; ++i) {
            const auto& s = r.forces_[i];
            if (s.owner != PlaygroundRoom::kBallOwner || s.couple) continue;
            if (length(s.force) > worst) { worst = length(s.force); at = s.at; kind = int(s.kind); }
        }
        worstEver = std::max(worstEver, worst);
        if (k % 20 != 0 && k < 390) continue;
        std::printf("t%03d ball (%+.5f,%+.5f) v(%+.5f,%+.5f) |v| %.6f w%+.3f frz%d touch%d touches %zu | worst arrow %.3f N kind %d at (%+.3f,%+.3f)\n",
                    k, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                    length(r.ball_.velocity), r.ball_.spin, int(r.ball_.frozen), int(r.ball_.touching),
                    r.ballTouchCount_, worst, kind, at.x, at.y);
    }
    std::printf("worst arrow on the ball over the whole run: %.1f N\n", worstEver);
    return 0;
}
