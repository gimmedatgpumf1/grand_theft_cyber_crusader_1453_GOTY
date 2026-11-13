// One placement, tick by tick: the room's energy, and every solid's speed, turn and
// place. The shape is the last argument (0 cube, 1 triangle), then the offset from
// the ball and the air under it.
#include "common.h"
#include <cstdlib>
static double energy(const PlaygroundRoom& r) {
    const auto& b = r.ball_;
    double e = 0.5 * b.mass * dot(b.velocity, b.velocity);
    e += 0.5 * (0.4 * b.mass * b.radius * b.radius) * b.spin * b.spin;
    e += b.mass * 9.8 * b.position.y;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& body = r.bodies_[i];
        if (body.count == 0) continue;
        e += 0.5 * body.mass * dot(body.velocity, body.velocity);
        e += 0.5 * body.moment * body.spin * body.spin;
        e += body.mass * 9.8 * body.centre.y;
    }
    return e;
}
int main(int argc, char** argv) {
    const int which = argc > 1 ? std::atoi(argv[1]) : 0;
    const double dx = argc > 2 ? std::atof(argv[2]) : 0.10;
    const double height = argc > 3 ? std::atof(argv[3]) : 0.0;
    const int ticks = argc > 4 ? std::atoi(argv[4]) : 40;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    {
        auto& away = r.bodies_[1 - which];
        away.frozen = true;
        away.centre = {5.0 + 2.0 * which, away.centre.y, 0};
        PlaygroundRoom::placeCorners(away);
        away.previous = away.position;
        away.previousCentre = away.centre;
    }
    auto& shape = r.bodies_[which];
    const double half = which == 0 ? 0.35 : 0.70;
    shape.centre = {0.30 + dx, r.ball_.radius + 0.12 + half + height, 0};
    shape.angle = 0.0;
    shape.velocity = {};
    shape.spin = 0.0;
    shape.frozen = false;
    PlaygroundRoom::placeCorners(shape);
    shape.previous = shape.position;
    shape.previousCentre = shape.centre;
    shape.previousAngle = shape.angle;
    std::printf("E0 %.4f\n", energy(r));
    for (int t = 1; t <= ticks; ++t) {
        r.advance(1.0 / 20.0);
        std::printf("t%2d E %.4f | S v(%+.3f,%+.3f) sp%+.2f pos(%+.4f,%+.4f) frz%d | ball(%+.4f,%+.4f) v(%+.3f,%+.3f) sp%+.2f\n",
                    t, energy(r), shape.velocity.x, shape.velocity.y, shape.spin, shape.centre.x, shape.centre.y,
                    int(shape.frozen), r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                    r.ball_.spin);
    }
}
