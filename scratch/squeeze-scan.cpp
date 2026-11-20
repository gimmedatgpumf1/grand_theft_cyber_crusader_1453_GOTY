// Every placement of a shape on the ball, and whether the room's energy ever rises
// above where it started.
//
// A shape set down on the ball and left to squeeze or slide off may lose energy and
// may never gain any. This walks a grid of offsets and heights, for the cube and for
// the triangle, drops the shape from rest, and reports the worst rise in the total
// over four hundred ticks -- with the tick it happened on, so a rise that only
// appears once the shape has slid halfway off can be told from one at the landing.
#include "common.h"
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
static void run(double dx, double height, int which) {
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    // The other shape is parked out of the way: the room's triangle is two metres
    // across and its resting place overlaps where the cube stands.
    {
        auto& away = r.bodies_[1 - which];
        away.frozen = true;
        away.centre = {5.0 + 2.0 * which, away.centre.y, 0};
        PlaygroundRoom::placeCorners(away);
        away.previous = away.position;
        away.previousCentre = away.centre;
    }
    auto& shape = r.bodies_[which];
    // Half the height of the shape's own footprint, so `height` is the air under it:
    // the cube stands on a face, the triangle on its base.
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
    const double e0 = energy(r);
    double worst = 0.0;
    int at = 0;
    for (int t = 1; t <= 400; ++t) {
        r.advance(1.0 / 20.0);
        const double rise = energy(r) - e0;
        if (rise > worst) {
            worst = rise;
            at = t;
        }
    }
    std::printf("body %d dx %+.2f h %.2f | rise %+.4f J (t%-3d) | E0 %.3f final %.3f\n", which, dx, height, worst, at, e0,
                energy(r));
}
int main() {
    for (int which = 0; which < 2; ++which)
        for (double dx : {0.0, 0.10, 0.16, 0.22, 0.30, 0.34})
            for (double h : {0.0, 0.30, 0.80, 1.50}) run(dx, h, which);
}
