// Every scene the King has ever pointed at, left alone, measured in METRES of place.
#include "common.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace gtc::native;
namespace { constexpr double kDt = 1.0 / 20.0; }

struct Range {
    double lo = 1e9, hi = -1e9;
    void take(double v) { lo = std::min(lo, v); hi = std::max(hi, v); }
    double span() const { return hi < lo ? 0.0 : hi - lo; }
};

static void run(const char* name, int settleTicks, int measureTicks, const std::function<void(PlaygroundRoom&)>& place) {
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 40; ++k) r.advance(kDt);
    place(r);
    for (int k = 0; k < settleTicks; ++k) r.advance(kDt);
    std::vector<Range> bx(3), by(3), ba(3);
    Range ballx, bally, dotx, doty;
    std::vector<Range> cx(12), cy(12);
    double worstStep = 0.0;
    Vec3 wasBall = r.ball_.position;
    for (int k = 0; k < measureTicks; ++k) {
        r.advance(kDt);
        ballx.take(r.ball_.position.x); bally.take(r.ball_.position.y);
        dotx.take(r.point().x); doty.take(r.point().y);
        for (std::size_t i = 0; i < 3; ++i)
            for (std::size_t c = 0; c < r.bodies_[i].count; ++c) {
                cx[i * 4 + c].take(r.bodies_[i].position[c].x);
                cy[i * 4 + c].take(r.bodies_[i].position[c].y);
            }
        worstStep = std::max(worstStep, length(r.ball_.position - wasBall));
        wasBall = r.ball_.position;
        for (std::size_t i = 0; i < 3; ++i) {
            if (r.bodies_[i].count == 0) continue;
            bx[i].take(r.bodies_[i].centre.x); by[i].take(r.bodies_[i].centre.y); ba[i].take(r.bodies_[i].angle);
        }
    }
    double worst = std::max({ballx.span(), bally.span(), dotx.span(), doty.span()});
    for (std::size_t i = 0; i < 12; ++i) worst = std::max({worst, cx[i].span(), cy[i].span()});
    for (std::size_t i = 0; i < 3; ++i) worst = std::max({worst, bx[i].span(), by[i].span(), ba[i].span() * 0.5});
    std::printf("%-34s ball %.3e/%.3e | dot %.3e/%.3e | cube %.3e/%.3e | tri %.3e/%.3e | plank %.3e/r%.3e | worst %.3e m\n", name,
                ballx.span(), bally.span(), dotx.span(), doty.span(), bx[0].span(), by[0].span(), bx[1].span(), by[1].span(),
                by[2].span(), ba[2].span(), worst);
}

static void putBody(PlaygroundRoom& r, std::size_t i, Vec3 centre, double angle) {
    auto& b = r.bodies_[i];
    b.centre = centre; b.angle = angle; b.velocity = {}; b.spin = 0; b.frozen = false;
    PlaygroundRoom::placeCorners(b);
    b.previous = b.position; b.previousCentre = b.centre; b.previousAngle = b.angle;
}
static void putBall(PlaygroundRoom& r, Vec3 at, Vec3 v = {}) {
    r.ball_.position = at; r.ball_.previous = at;
    r.ball_.velocity = v; r.ball_.spin = 0; r.ball_.frozen = false;
}

int main() {
    run("0 default room", 400, 400, [](PlaygroundRoom&) {});
    run("1 notch: cube -0.90 ball(-0.15,0.40)", 1400, 400, [](PlaygroundRoom& r) {
        putBody(r, 0, {-0.90, 0.35, 0}, 0.0); putBody(r, 1, {1.15, 0.7, 0}, 0.5 * 3.14159265358979323846);
        putBall(r, {-0.15, 0.40, 0});
    });
    run("2 cube -0.45 ball(+0.12,+0.80)", 1400, 400, [](PlaygroundRoom& r) {
        putBody(r, 0, {-0.45, 0.35, 0}, 0.0); putBody(r, 1, {1.15, 0.7, 0}, 0.5 * 3.14159265358979323846);
        putBall(r, {0.12, 0.80, 0});
    });
    run("3 cube -0.90 ball(+0.33,+0.14)", 1400, 400, [](PlaygroundRoom& r) {
        putBody(r, 0, {-0.90, 0.35, 0}, 0.0); putBody(r, 1, {1.15, 0.7, 0}, 0.5 * 3.14159265358979323846);
        putBall(r, {0.33, 0.14, 0});
    });
    run("4 cube leaning on triangle", 1200, 400, [](PlaygroundRoom& r) {
        putBody(r, 1, {-1.90, 0.7, 0}, 0.0); putBody(r, 0, {-0.60, 1.10, 0}, 0.60);
    });
    run("5 triangle on cube", 1200, 400, [](PlaygroundRoom& r) { putBody(r, 1, {1.333, 1.40, 0}, 0.0); });
    run("6 cube on ball", 1200, 400, [](PlaygroundRoom& r) {
        putBall(r, {3.10, 0.12, 0}); putBody(r, 0, {3.10, 0.82, 0}, 0.0);
    });
    run("7 ball dropped on triangle apex", 1200, 400, [](PlaygroundRoom& r) { putBall(r, {-1.90, 2.20, 0}); });
    run("8 ball dropped on slope", 1200, 400, [](PlaygroundRoom& r) { putBall(r, {-2.60, 2.20, 0}); });
    run("9 ball on the seesaw", 1200, 400, [](PlaygroundRoom& r) { putBall(r, {-3.10, 1.40, 0}); });
    run("10 cube on the seesaw end", 1200, 400, [](PlaygroundRoom& r) { putBody(r, 0, {-3.10, 1.50, 0}, 0.0); });
    run("11 ball between cube and slope", 1400, 400, [](PlaygroundRoom& r) {
        putBody(r, 0, {-0.30, 0.35, 0}, 0.0); putBody(r, 1, {0.90, 0.70, 0}, 0.0);
        putBall(r, {0.05, 0.25, 0});
    });
    run("12 ball at the foot of the slope", 1400, 400, [](PlaygroundRoom& r) { putBall(r, {-0.70, 0.30, 0}); });
    run("13 dot dropped on the ball", 1200, 400, [](PlaygroundRoom& r) {
        putBall(r, {3.10, 0.12, 0}); r.resetPoint({3.10, 1.40, 0});
    });
    run("14 ball dropped on the dot", 1200, 400, [](PlaygroundRoom& r) {
        r.resetPoint({3.10, 0.0, 0}); putBall(r, {3.10, 1.20, 0});
    });
    run("15 cube dropped on the ball", 1200, 400, [](PlaygroundRoom& r) {
        putBall(r, {3.10, 0.12, 0}); putBody(r, 0, {3.10, 1.40, 0}, 0.0);
    });
    return 0;
}
