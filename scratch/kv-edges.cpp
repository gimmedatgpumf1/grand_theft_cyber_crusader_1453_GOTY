// The King's corner, at rest: exactly how far the ball is from every face, and
// exactly what speed is left in it, tick by tick.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace { constexpr double kDt = 1.0 / 20.0; }
int main(int argc, char** argv) {
    const double footX = argc > 1 ? std::atof(argv[1]) : -0.25;
    const double ballX = argc > 2 ? std::atof(argv[2]) : -0.13;
    const int ticks = argc > 3 ? std::atoi(argv[3]) : 400;
    gtc::native::PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    r.placeCorners(c);
    c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {footX + 1.4, 0.7, 0};
    r.placeCorners(t);
    t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {ballX, 0.13, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k < ticks; ++k) {
        r.advance(kDt);
        const bool show = k >= ticks - 3;
        if (!show) continue;
        std::printf("t%d PREV ball (%.9f,%.9f) -> now (%.9f,%.9f)  cube (%.9f,%.9f)->(%.9f,%.9f) tri (%.9f,%.9f)->(%.9f,%.9f)\n", k,
                    r.ball_.previous.x, r.ball_.previous.y, r.ball_.position.x, r.ball_.position.y,
                    r.bodies_[0].previousCentre.x, r.bodies_[0].previousCentre.y, r.bodies_[0].centre.x, r.bodies_[0].centre.y,
                    r.bodies_[1].previousCentre.x, r.bodies_[1].previousCentre.y, r.bodies_[1].centre.x, r.bodies_[1].centre.y);
        std::printf("t%d ball (%.9f,%.9f) v(%+.7f,%+.7f) w%+.5f touching%d standing%d touches%zu\n", k,
                    r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                    r.ball_.spin, int(r.ball_.touching), int(r.ball_.standing), r.ballTouchCount_);
        for (std::size_t b = 0; b < gtc::native::PlaygroundRoom::kBodyCount; ++b) {
            const auto& body = r.bodies_[b];
            for (std::size_t e = 0; e < body.count; ++e) {
                const std::size_t top = (e + 1) % body.count;
                const gtc::native::Vec3 a = body.position[e], bb = body.position[top];
                const gtc::native::Vec3 side = bb - a;
                const double l2 = side.x * side.x + side.y * side.y;
                const double tt = l2 > 0 ? std::clamp(dot(r.ball_.position - a, side) / l2, 0.0, 1.0) : 0.0;
                const gtc::native::Vec3 near = a + side * tt;
                const gtc::native::Vec3 gap = r.ball_.position - near;
                const double d = length(gap);
                const gtc::native::Vec3 n = gap / d;
                const gtc::native::Vec3 sv = r.pointSpeed(body, near);
                const double closing = dot(r.ball_.velocity - sv, n);
                std::printf("   b%zu e%zu near(%+.6f,%+.6f) dist%.9f spare%+.9f n(%+.4f,%+.4f) closing%+.7f sv(%+.7f,%+.7f)\n",
                            b, e, near.x, near.y, d, d - r.ball_.radius, n.x, n.y, closing, sv.x, sv.y);
            }
        }
    }
    return 0;
}
