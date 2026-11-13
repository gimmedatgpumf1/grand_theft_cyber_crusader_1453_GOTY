// The hand, and whether it can pass anything through the seesaw.
//
// Dragging is not simulated: the mouse puts a thing where it asks, and the room
// answers afterwards. So the hand is the one thing in this room that can be *asked*
// to put something inside something else, and the question here is whether it refuses
// -- for the ball, for a shape, and for the seesaw itself.
#include "common.h"
#include "client/playground_ball_shape.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace {
constexpr double S = PlaygroundRoom::kSeesaw;

bool pointInside(const Vec3* outline, std::size_t count, Vec3 point, double& howDeep) {
    Vec3 middle{};
    for (std::size_t i = 0; i < count; ++i) middle = middle + outline[i];
    middle = middle * (1.0 / double(count));
    double deepest = 1e30;
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 a = outline[i];
        const Vec3 side = outline[(i + 1) % count] - a;
        const double span = length(side);
        if (!(span > 1e-12)) continue;
        Vec3 outward{side.y / span, -side.x / span, 0.0};
        if (dot(middle - a, outward) > 0.0) outward = outward * -1.0;
        deepest = std::min(deepest, -dot(point - a, outward));
    }
    howDeep = deepest;
    return deepest > 0.0;
}

double overlap(const PlaygroundRoom& r, std::size_t a, std::size_t b) {
    const auto& A = r.bodies_[a];
    const auto& B = r.bodies_[b];
    double d = 0.0, deepest = 0.0;
    for (std::size_t k = 0; k < B.count; ++k)
        if (pointInside(A.position.data(), A.count, B.position[k], d)) deepest = std::max(deepest, d);
    for (std::size_t k = 0; k < A.count; ++k)
        if (pointInside(B.position.data(), B.count, A.position[k], d)) deepest = std::max(deepest, d);
    return deepest;
}

double ballInto(const PlaygroundRoom& r, std::size_t i) {
    const auto& b = r.bodies_[i];
    if (b.count == 0) return 0.0;
    return r.ball_.radius - ball_shape::outlineGap(r.ball_.position, b.position.data(), b.position.data(), b.count, 1.0).gap;
}

// A ray that hits `at` from straight in front of the plane, the way a click does.
void click(PlaygroundRoom& r, Vec3 at) {
    r.setDragRay(Vec3{at.x, at.y, 4.0}, Vec3{0, 0, -1});
    r.beginDrag();
}

void moveTo(PlaygroundRoom& r, Vec3 at) {
    r.setDragRay(Vec3{at.x, at.y, 4.0}, Vec3{0, 0, -1});
    r.updateDrag();
}

void report(const char* what, const PlaygroundRoom& r, int extraBody) {
    double worst = 0.0;
    worst = std::max(worst, overlap(r, 0, S));
    worst = std::max(worst, overlap(r, 1, S));
    worst = std::max(worst, overlap(r, 0, 1));
    worst = std::max(worst, ballInto(r, S));
    worst = std::max(worst, ballInto(r, 0));
    worst = std::max(worst, ballInto(r, 1));
    double under = 0.0;
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i)
        for (std::size_t k = 0; k < r.bodies_[i].count; ++k) under = std::max(under, -r.bodies_[i].position[k].y);
    std::printf("%-44s | deepest overlap %.6f m | deepest under the floor %.6f m | plank a %+.4f at (%+.3f,%+.3f)%s\n",
                what, worst, under, r.bodies_[S].angle, r.bodies_[S].centre.x, r.bodies_[S].centre.y,
                extraBody == 1 ? " | cube moved" : "");
}
}  // namespace

int main() {
    // 1. Take hold of the seesaw itself and pull it. The pin is the room's, not the
    // hand's: a plank carried off its axle is a plank held inside whatever it was
    // passing through.
    {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 10; ++k) r.advance(1.0 / 20.0);
        click(r, {-4.0, 0.85, 0});
        std::printf("picked while holding: plank centre (%+.3f,%+.3f), pin (%+.3f,%+.3f)\n", r.bodies_[S].centre.x,
                    r.bodies_[S].centre.y, r.bodies_[S].pivotPoint.x, r.bodies_[S].pivotPoint.y);
        for (const Vec3 to : {Vec3{-4.9, 2.2, 0}, Vec3{-3.0, 1.9, 0}, Vec3{-1.4, 0.5, 0}}) {
            r.setDragRay(Vec3{to.x, to.y, 4.0}, Vec3{0, 0, -1});
            r.updateDrag();
            for (int k = 0; k < 6; ++k) r.advance(1.0 / 20.0);
        }
        report("seesaw dragged by its middle", r, 1);
    }
    // 2. Take a cube resting on the floor and put it down on the far side of the
    // plank in one movement of the hand -- across the plank, not around it.
    {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 10; ++k) r.advance(1.0 / 20.0);
        auto& s = r.bodies_[S];
        s.frozen = false;
        s.angle = -0.60;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        auto& c = r.bodies_[0];
        c.frozen = true;  // asleep, so it stays exactly where it is put
        c.centre = {s.centre.x + 1.9, 0.35, 0};
        c.angle = 0.0;
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        std::printf("[cube] click at (%+.3f,%+.3f), overlap before %.6f\n", c.centre.x, c.centre.y, overlap(r, 0, S));
        click(r, {c.centre.x, c.centre.y, 0});
        moveTo(r, {s.centre.x - 1.9, 0.75, 0});
        std::printf("[cube] after the move: (%+.3f,%+.3f) overlap %.6f\n", r.bodies_[0].centre.x,
                    r.bodies_[0].centre.y, overlap(r, 0, S));
        for (int k = 0; k < 4; ++k) r.advance(1.0 / 20.0);
        report("cube dragged straight through the plank", r, 1);
    }
    // 3. The same with the ball.
    {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 10; ++k) r.advance(1.0 / 20.0);
        auto& s = r.bodies_[S];
        s.frozen = false;
        s.angle = -0.60;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        r.ball_.position = {s.centre.x + 1.9, 0.12, 0};
        r.ball_.velocity = {};
        r.ball_.previous = r.ball_.position;
        r.ball_.frozen = false;
        for (int k = 0; k < 4; ++k) r.advance(1.0 / 20.0);
        click(r, r.ball_.position);
        moveTo(r, {s.centre.x - 1.9, 0.55, 0});
        std::printf("[ball] after the move: (%+.3f,%+.3f)\n", r.ball_.position.x, r.ball_.position.y);
        for (int k = 0; k < 4; ++k) r.advance(1.0 / 20.0);
        report("ball dragged straight through the plank", r, 1);
    }
    return 0;
}
