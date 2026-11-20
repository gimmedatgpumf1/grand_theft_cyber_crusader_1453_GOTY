// The King's own move: put the ball on the floor, grab a shape and drop it on the
// ball. Both shapes, a spread of heights and offsets. What matters is the deepest
// the ball's circle ever gets into the shape, and where things end up.
#include "client/playground_room.h"
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
        if ((a.y > y) != (b.y > y)) {
            const double t = (y - a.y) / (b.y - a.y);
            if (x < a.x + t * (b.x - a.x)) ++cr;
        }
    }
    return cr % 2 == 1;
}
// How far the ball's circle is through an outline, in metres. Positive is through.
static double throughBody(const PlaygroundRoom& r, std::size_t body, Vec3 centre, double radius) {
    Vec3 c[8];
    const std::size_t n = r.bodyCorners(body);
    for (std::size_t i = 0; i < n; ++i) c[i] = r.bodySimulatedCorner(body, i);
    double nearest = 1e30;
    for (std::size_t i = 0; i < n; ++i) nearest = std::min(nearest, segDist(centre.x, centre.y, c[i].x, c[i].y, c[(i + 1) % n].x, c[(i + 1) % n].y));
    return insidePoly(centre.x, centre.y, c, n) ? nearest + radius : radius - nearest;
}
static void run(PlaygroundRoom& r, int ticks) { for (int i = 0; i < ticks; ++i) r.advance(1.0 / 20.0); }
static void placeBall(PlaygroundRoom& r, double x, double y) {
    const Vec3 at = r.ballPosition();
    r.setDragRay({at.x, at.y, 5}, {0, 0, -1}); r.beginDrag();
    r.setDragRay({x, y, 5}, {0, 0, -1}); r.advance(1.0 / 20.0); r.endDrag();
}
static void park(PlaygroundRoom& r, std::size_t body, double x, double y) {
    const Vec3 m = r.bodyMiddle(body);
    r.setDragRay({m.x, m.y, 5}, {0, 0, -1}); r.beginDrag();
    r.setDragRay({x, y, 5}, {0, 0, -1}); r.advance(1.0 / 20.0); r.endDrag();
}

int main() {
    double worstAll = 0.0;
    for (int body = 0; body < 2; ++body) {
        const char* name = body == 0 ? "cube" : "triangle";
        for (int h = 0; h < 6; ++h) {
            for (int off = -2; off <= 2; ++off) {
                PlaygroundRoom r;
                r.reset();
                r.resetPoint({-4.60, 0.0, 0});
                run(r, 150);
                // Both shapes out of the way first: the one that is not being
                // dropped stays there, and the one that is has to be clear of the
                // spot the ball is put down on, or the ball is not put down at all
                // and the whole case tests a ball that is somewhere else.
                park(r, std::size_t(1 - body), -4.20, 0.90);
                park(r, std::size_t(body), -3.20, 1.60);
                placeBall(r, 1.20, 0.60);
                run(r, 20);
                const double dropY = 1.10 + 0.55 * double(h);
                park(r, std::size_t(body), 1.20 + 0.05 * double(off), dropY);
                double worst = 0.0;
                int worstTick = -1;
                Vec3 worstBall{};
                Vec3 worstCorners[4];
                std::size_t worstCount = 0;
                Vec3 worstVelocity{};
                for (int tick = 0; tick < 200; ++tick) {
                    run(r, 1);
                    const double through = throughBody(r, std::size_t(body), r.ballPosition(), 0.12);
                    if (through > worst) {
                        worst = through;
                        worstTick = tick;
                        worstBall = r.ballPosition();
                        worstVelocity = r.ballVelocity();
                        worstCount = r.bodyCorners(std::size_t(body));
                        for (std::size_t i = 0; i < worstCount; ++i)
                            worstCorners[i] = r.bodySimulatedCorner(std::size_t(body), i);
                    }
                }
                if (worst > 1e-6) {
                    std::printf("  at the worst tick %d: ball (%.3f,%.3f) v(%.2f,%.2f) %s", worstTick, worstBall.x,
                                worstBall.y, worstVelocity.x, worstVelocity.y, name);
                    for (std::size_t i = 0; i < worstCount; ++i)
                        std::printf(" (%.3f,%.3f)", worstCorners[i].x, worstCorners[i].y);
                    std::printf("\n");
                }
                worstAll = std::max(worstAll, worst);
                if (worst > 1e-6) {
                    double lowest = 1e9;
                    for (std::size_t i = 0; i < r.bodyCorners(std::size_t(body)); ++i)
                        lowest = std::min(lowest, r.bodySimulatedCorner(std::size_t(body), i).y);
                    std::printf("%-8s from %.2f offset %+.2f: through %.5f m at tick %d | ball (%.3f,%.3f) v(%.2f,%.2f) | %s low %.3f mid x %.3f\n",
                                name, dropY, 0.05 * double(off), worst, worstTick, r.ballPosition().x, r.ballPosition().y,
                                r.ballVelocity().x, r.ballVelocity().y, name, lowest, r.bodyMiddle(std::size_t(body)).x);
                }
            }
        }
    }
    std::printf("worst overlap when a shape is dropped on the ball: %.6f m\n", worstAll);
    return worstAll > 1e-6 ? 1 : 0;
}
