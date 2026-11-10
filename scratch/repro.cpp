#include "client/playground_room.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace gtc::native;

static void run(PlaygroundRoom& r, int ticks) {
    for (int i = 0; i < ticks; ++i) r.advance(1.0 / 20.0);
}

static bool inside(double x, double y, const Vec3* c, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 a = c[i], b = c[(i + 1) % n];
        const double sx = b.x - a.x, sy = b.y - a.y;
        const double len = std::hypot(sx, sy);
        // outward normal, assuming the same winding the room uses
        const double ox = sy / len, oy = -sx / len;
        const double ox2 = (oy < 0.0 || ox == 0.0) ? ox : ox;
        (void)ox2;
        if ((x - a.x) * ox + (y - a.y) * oy > 0.0) return false;
    }
    return true;
}

int main() {
    // The ball is put on the floor to the right of the triangle and rolled left
    // into it, which is where a ball can slide along the base, because the base
    // and the floor are the same line.
    PlaygroundRoom r; r.reset(); r.resetPoint({-4.60, 0.0, 0});
    run(r, 200);
    const Vec3 middle = r.bodyMiddle(0);
    r.setDragRay({middle.x, middle.y, 5}, {0, 0, -1}); r.beginDrag();
    r.setDragRay({-4.60, 0.90, 5}, {0, 0, -1}); r.advance(1.0 / 20.0); r.endDrag();
    run(r, 40);
    r.setDragRay({3.10, 0.12, 5}, {0, 0, -1}); r.beginDrag();
    r.setDragRay({-1.20, 2.60, 5}, {0, 0, -1}); r.advance(1.0 / 20.0); r.endDrag();

    int insideTicks = 0;
    for (int i = 0; i < 200; ++i) {
        run(r, 1);
        bool in = false;
        for (std::size_t b = 0; b < 2; ++b) {
            Vec3 corners[8];
            const std::size_t n = r.bodyCorners(b);
            for (std::size_t c = 0; c < n; ++c) corners[c] = r.bodySimulatedCorner(b, c);
            if (inside(r.ballPosition().x, r.ballPosition().y, corners, n)) in = true;
        }
        if (in) ++insideTicks;
        if (i < 40 || in)
            std::printf("%3d ball(%7.3f,%7.3f) v=(%6.2f,%6.2f) %s%s | triangle mid (%6.3f,%6.3f) %s\n", i,
                        r.ballPosition().x, r.ballPosition().y, r.ballVelocity().x, r.ballVelocity().y,
                        r.ballFrozen() ? "FROZEN " : "moving ", in ? "INSIDE A BODY" : "",
                        r.bodyMiddle(1).x, r.bodyMiddle(1).y, r.bodyFrozen(1) ? "frozen" : "awake");
        if (i > 40 && !in && r.ballFrozen()) break;
    }
    std::printf("ticks with the ball inside a body: %d\n", insideTicks);
}
