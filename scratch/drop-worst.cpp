// The one case where a shape dropped on the ball goes deepest into it: tick by tick,
// with the ball's own speed and the shape's, and how many steps the room clamped.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace gtc::native;
static double segDist(double x, double y, double ax, double ay, double bx, double by) {
    const double sx = bx - ax, sy = by - ay, l2 = sx * sx + sy * sy;
    const double t = l2 > 0.0 ? std::clamp(((x - ax) * sx + (y - ay) * sy) / l2, 0.0, 1.0) : 0.0;
    return std::hypot(x - (ax + sx * t), y - (ay + sy * t));
}
static bool insidePoly(double x, double y, const Vec3* c, std::size_t n) {
    int cr = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 a = c[i], b = c[(i + 1) % n];
        if ((a.y > y) != (b.y > y)) { const double t = (y - a.y) / (b.y - a.y); if (x < a.x + t * (b.x - a.x)) ++cr; }
    }
    return cr % 2 == 1;
}
static double throughBody(const PlaygroundRoom& r, std::size_t body, Vec3 centre, double radius) {
    Vec3 c[8]; const std::size_t n = r.bodyCorners(body);
    for (std::size_t i = 0; i < n; ++i) c[i] = r.bodySimulatedCorner(body, i);
    double nearest = 1e30;
    for (std::size_t i = 0; i < n; ++i) nearest = std::min(nearest, segDist(centre.x, centre.y, c[i].x, c[i].y, c[(i+1)%n].x, c[(i+1)%n].y));
    return insidePoly(centre.x, centre.y, c, n) ? nearest + radius : radius - nearest;
}
int main() {
    PlaygroundRoom r; r.reset(); r.resetPoint({-4.60, 0.0, 0});
    for (int i = 0; i < 150; ++i) r.advance(1.0/20.0);
    auto park = [&](std::size_t b, double x, double y) {
        const Vec3 m = r.bodyMiddle(b);
        r.setDragRay({m.x, m.y, 5}, {0,0,-1}); r.beginDrag();
        r.setDragRay({x, y, 5}, {0,0,-1}); r.advance(1.0/20.0); r.endDrag();
    };
    park(1, -4.20, 0.90); park(0, -3.20, 1.60);
    { const Vec3 at = r.ballPosition();
      r.setDragRay({at.x, at.y, 5}, {0,0,-1}); r.beginDrag();
      r.setDragRay({1.20, 0.60, 5}, {0,0,-1}); r.advance(1.0/20.0); r.endDrag(); }
    for (int i = 0; i < 20; ++i) r.advance(1.0/20.0);
    park(0, 1.25, 3.85);
    std::printf("dropped; ball at (%+.4f,%+.4f)\n", r.ball_.position.x, r.ball_.position.y);
    int clamps = 0;
    for (int tick = 0; tick < 40; ++tick) {
        const int before = r.bodyStepClamped_;
        r.advance(1.0/20.0);
        const int now = r.bodyStepClamped_ - before;
        clamps += now;
        const double th = throughBody(r, 0, r.ball_.position, r.ball_.radius);
        Vec3 way{}, at{};
        const double roomGap = r.bodyBallGap(0, way, at);
        std::fprintf(stderr, "t%02d thru %+.5f roomgap %+.5f | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) w%+.2f frz%d touch%d | cube mid (%+.4f,%+.4f) ang %+.3f v(%+.3f,%+.3f) spin %+.3f frz%d | clamps %d\n",
                    tick, th, roomGap, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y, r.ball_.spin,
                    int(r.ball_.frozen), int(r.ball_.touching), r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].angle,
                    r.bodies_[0].velocity.x, r.bodies_[0].velocity.y, r.bodies_[0].spin, int(r.bodies_[0].frozen), now);
    }
    return 0;
}
