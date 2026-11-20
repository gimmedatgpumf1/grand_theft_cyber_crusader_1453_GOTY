// Does the ball's circle ever get through an outline, anywhere -- and how deep?
// The sweep is supposed to make that impossible, so every scenario here reports
// the worst penetration it can find, the drawn position included.
#include "client/playground_room.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace gtc::native;

static double pointSegmentDistance(double x, double y, double ax, double ay, double bx, double by) {
    const double sx = bx - ax, sy = by - ay;
    const double len2 = sx * sx + sy * sy;
    const double t = len2 > 0.0 ? std::clamp(((x - ax) * sx + (y - ay) * sy) / len2, 0.0, 1.0) : 0.0;
    const double cx = ax + sx * t, cy = ay + sy * t;
    return std::hypot(x - cx, y - cy);
}

static bool insideOutline(double x, double y, const Vec3* c, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 a = c[i], b = c[(i + 1) % n];
        const double sx = b.x - a.x, sy = b.y - a.y;
        const double len = std::hypot(sx, sy);
        const double ox = sy / len, oy = -sx / len;
        // Whichever winding this is, a point inside is on the same side of every
        // edge as the outline's own middle.
        const double sign = (x - a.x) * ox + (y - a.y) * oy;
        const double mid = 0.0;
        (void)mid;
        (void)sign;
    }
    // Fall back on the winding-free test: count crossings of a ray.
    int crossings = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 a = c[i], b = c[(i + 1) % n];
        if ((a.y > y) != (b.y > y)) {
            const double t = (y - a.y) / (b.y - a.y);
            if (x < a.x + t * (b.x - a.x)) ++crossings;
        }
    }
    return crossings % 2 == 1;
}

// How far the ball's circle is through the outline: positive is through it.
static double penetration(const PlaygroundRoom& room, Vec3 centre, double radius) {
    double worst = 0.0;
    for (std::size_t b = 0; b < 2; ++b) {
        Vec3 c[8];
        const std::size_t n = room.bodyCorners(b);
        for (std::size_t i = 0; i < n; ++i) c[i] = room.bodySimulatedCorner(b, i);
        double nearest = 1e30;
        for (std::size_t i = 0; i < n; ++i) {
            const Vec3 p = c[i], q = c[(i + 1) % n];
            nearest = std::min(nearest, pointSegmentDistance(centre.x, centre.y, p.x, p.y, q.x, q.y));
        }
        const bool within = insideOutline(centre.x, centre.y, c, n);
        const double through = within ? nearest + radius : radius - nearest;
        worst = std::max(worst, through);
    }
    return worst;
}

static void run(PlaygroundRoom& r, int ticks) {
    for (int i = 0; i < ticks; ++i) r.advance(1.0 / 20.0);
}

static void park(PlaygroundRoom& r, std::size_t body, double x, double y) {
    const Vec3 middle = r.bodyMiddle(body);
    r.setDragRay({middle.x, middle.y, 5}, {0, 0, -1});
    r.beginDrag();
    r.setDragRay({x, y, 5}, {0, 0, -1});
    r.advance(1.0 / 20.0);
    r.endDrag();
}

// The mouse grabs what is under it, so the ball is picked up where it is and put
// down where it is wanted: a click straight onto the destination would grab the
// body that is already there.
static void placeBall(PlaygroundRoom& r, double x, double y) {
    const Vec3 at = r.ballPosition();
    r.setDragRay({at.x, at.y, 5}, {0, 0, -1});
    r.beginDrag();
    r.setDragRay({x, y, 5}, {0, 0, -1});
    r.advance(1.0 / 20.0);
    r.endDrag();
}

int main() {
    double worstOverall = 0.0;
    {
        // Does this probe actually see a ball inside a body? A circle sitting on
        // the triangle's own middle is through every face it is near, so the
        // number it reads here is the number to trust the zeros below against.
        PlaygroundRoom r;
        r.reset();
        run(r, 40);
        const Vec3 middle = r.bodyMiddle(1);
        const double seen = penetration(r, middle, 0.12);
        std::printf("sanity: a circle on the triangle's middle reads %.4f m through\n", seen);
        if (!(seen > 0.5)) {
            std::printf("the probe cannot see a penetration, so its zeros mean nothing\n");
            return 2;
        }
        // And the room's own answer to a ball the mouse has put inside a body: it
        // must not stay there.
        placeBall(r, -1.50, 0.30);
        run(r, 4);
        const double after = penetration(r, r.ballPosition(), 0.12);
        std::printf("sanity: after the tick, a ball put inside reads %.6f m through\n", after);
        if (after > 1e-6) {
            std::printf("a ball put inside a body stayed inside it\n");
            return 3;
        }
    }
    // The ball is put on the slope and let go, from a spread of heights, with the
    // cube parked out of the way. It runs down the hypotenuse, off its foot, and
    // then along the floor beside the base -- which is the line the base sits on.
    for (int start = 0; start < 24; ++start) {
        PlaygroundRoom r;
        r.reset();
        r.resetPoint({-4.60, 0.0, 0});
        run(r, 200);
        park(r, 0, -4.60, 0.90);
        const double height = 0.60 + 0.09 * double(start);
        placeBall(r, -0.55 - (height - 0.12) * 0.6, height);
        double worst = 0.0;
        int worstTick = -1;
        Vec3 worstCentre{};
        for (int tick = 0; tick < 200; ++tick) {
            run(r, 1);
            const Vec3 p = r.ballPosition();
            const double through = penetration(r, p, 0.12);
            if (through > worst) {
                worst = through;
                worstTick = tick;
                worstCentre = p;
            }
            // The drawn ball, too: the drawing shows the step between the tick it
            // left and the tick it arrived at, and that must be outside as well.
            const Vec3 shown = r.ballShown();
            const double drawn = penetration(r, shown, 0.12);
            if (drawn > worst) {
                worst = drawn;
                worstTick = tick;
                worstCentre = shown;
            }
        }
        worstOverall = std::max(worstOverall, worst);
        if (worst > 1e-6)
            std::printf("start height %.2f: worst penetration %.6f m at tick %d, centre (%.4f, %.4f)\n", height,
                        worst, worstTick, worstCentre.x, worstCentre.y);
    }
    // The cube dropped onto a ball lying on the floor, from a spread of heights.
    for (int start = 0; start < 12; ++start) {
        PlaygroundRoom r;
        r.reset();
        r.resetPoint({-4.60, 0.0, 0});
        run(r, 200);
        park(r, 1, -4.60, 0.90);
        placeBall(r, 0.95, 0.60);
        run(r, 20);
        const double height = 1.0 + 0.30 * double(start);
        park(r, 0, 0.90, height);
        double worst = 0.0;
        for (int tick = 0; tick < 200; ++tick) {
            run(r, 1);
            const double through = penetration(r, r.ballPosition(), 0.12);
            if (through > 1e-6)
                std::printf("  cube from %.2f, tick %d: ball (%.3f,%.3f) through %.4f\n", height, tick,
                            r.ballPosition().x, r.ballPosition().y, through);
            worst = std::max(worst, through);
        }
        worstOverall = std::max(worstOverall, worst);
        if (worst > 1e-6) std::printf("cube dropped from %.2f: worst penetration %.6f m\n", height, worst);
    }
    // Dropped straight onto the triangle's sharp corners: the apex, and the foot
    // where the hypotenuse meets the floor. A corner is a corner, and the ball's
    // circle has to sit on it, not through it.
    for (int start = 0; start < 14; ++start) {
        PlaygroundRoom r;
        r.reset();
        r.resetPoint({-4.60, 0.0, 0});
        run(r, 150);
        park(r, 0, -4.20, 0.90);
        const double x = -2.60 + 0.22 * double(start);
        placeBall(r, x, 0.40 + 0.25 * double(start % 5));
        double worst = 0.0;
        for (int tick = 0; tick < 120; ++tick) {
            run(r, 1);
            worst = std::max(worst, penetration(r, r.ballPosition(), 0.12));
        }
        worstOverall = std::max(worstOverall, worst);
        if (worst > 1e-6) std::printf("dropped at x %.2f: worst penetration %.6f m\n", x, worst);
    }
    // The cube let go off-centre onto a ball on the floor, so it has to topple off
    // it rather than balance on it.
    for (int start = 0; start < 10; ++start) {
        PlaygroundRoom r;
        r.reset();
        r.resetPoint({-4.60, 0.0, 0});
        run(r, 150);
        park(r, 1, -4.20, 0.90);
        placeBall(r, 0.40, 0.60);
        run(r, 20);
        park(r, 0, 0.40 + 0.06 * double(start), 1.60);
        double worst = 0.0;
        for (int tick = 0; tick < 160; ++tick) {
            run(r, 1);
            worst = std::max(worst, penetration(r, r.ballPosition(), 0.12));
        }
        worstOverall = std::max(worstOverall, worst);
        if (worst > 1e-6)
            std::printf("cube dropped off centre by %.2f: worst penetration %.6f m\n", 0.06 * double(start),
                        worst);
    }
    // The dot dropped onto a ball lying on the floor: the ball is a surface under
    // it and neither may be inside the other.
    {
        PlaygroundRoom r;
        r.reset();
        r.resetPoint({-4.60, 0.0, 0});
        run(r, 150);
        park(r, 0, -4.20, 0.90);
        park(r, 1, -3.40, 0.90);
        placeBall(r, 0.40, 0.60);
        run(r, 20);
        r.resetPoint({0.40, 1.60, 0});
        for (int tick = 0; tick < 160; ++tick) {
            run(r, 1);
            const double gap = std::hypot(r.point().x - r.ballPosition().x, r.point().y - r.ballPosition().y);
            const double through = 0.07 + 0.12 - gap;
            if (through > 1e-6) std::printf("dot on ball, tick %d: through %.6f m\n", tick, through);
            worstOverall = std::max(worstOverall, through);
        }
    }
    std::printf("worst penetration over every scenario: %.9f m\n", worstOverall);
    return worstOverall > 1e-6 ? 1 : 0;
}
