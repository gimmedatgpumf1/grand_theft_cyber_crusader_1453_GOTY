// Can anything be put through the seesaw?
//
// The plank is a body that turns, and a turning body sweeps an arc; a ball fired at
// it is swept against a path. Anywhere the two answers disagree the ball would end a
// tick on the wrong side of a plank it never touched. This throws the ball at the
// plank from every direction and at every speed, and the cube and the triangle with
// it, and records the deepest anything ever got *inside* the plank's outline -- which
// is the one thing the room says it never does.
#include "common.h"
#include "client/playground_ball_shape.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr double S = PlaygroundRoom::kSeesaw;

// How far the ball is inside the plank, in metres. Negative means clear.
double ballInsidePlank(const PlaygroundRoom& r) {
    const auto& s = r.bodies_[S];
    return r.ball_.radius - ball_shape::outlineGap(r.ball_.position, s.position.data(), s.position.data(), s.count, 1.0).gap;
}

double ballInsideBody(const PlaygroundRoom& r, std::size_t i) {
    const auto& b = r.bodies_[i];
    if (b.count == 0) return 0.0;
    return r.ball_.radius - ball_shape::outlineGap(r.ball_.position, b.position.data(), b.position.data(), b.count, 1.0).gap;
}

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

// Deepest corner of one body inside the plank.
double bodyInsidePlank(const PlaygroundRoom& r, std::size_t i) {
    const auto& s = r.bodies_[S];
    const auto& b = r.bodies_[i];
    double deepest = 0.0, d = 0.0;
    for (std::size_t k = 0; k < b.count; ++k)
        if (pointInside(s.position.data(), s.count, b.position[k], d)) deepest = std::max(deepest, d);
    for (std::size_t k = 0; k < s.count; ++k)
        if (pointInside(b.position.data(), b.count, s.position[k], d)) deepest = std::max(deepest, d);
    return deepest;
}
}  // namespace

int main() {
    double worstBall = 0.0, worstCube = 0.0, worstTri = 0.0;
    double worstInCube = 0.0, worstInTri = 0.0;
    int worstBallCase = -1, worstCubeCase = -1, worstCubeTick = -1;
    for (int trial = 0; trial < 240; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 10; ++k) r.advance(1.0 / 20.0);
        auto& s = r.bodies_[S];
        s.angle = -0.85 + 1.7 * double(trial % 13) / 12.0;
        s.spin = (double((trial * 29) % 40) - 20.0) * 1.2;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.frozen = false;
        // The ball, fired at the middle of the plank from `dist` away at `speed`.
        const double away = 1.2 + 0.4 * double(trial % 5);
        const double height = 0.2 + 0.9 * double((trial / 5) % 5) / 4.0;
        const double side = (trial % 2 == 0) ? 1.0 : -1.0;
        r.ball_.position = {s.centre.x + side * away, height, 0};
        const Vec3 aim = {s.centre.x - r.ball_.position.x, s.centre.y - r.ball_.position.y, 0};
        const double aimLen = length(aim);
        const double speed = 3.0 + 1.5 * double(trial % 12);
        r.ball_.velocity = aim * (speed / aimLen);
        r.ball_.spin = 0.0;
        r.ball_.frozen = false;
        r.ball_.previous = r.ball_.position;
        // A cube and a triangle thrown at it too, in half the trials.
        auto throwBody = [&](std::size_t i, double dx, double dy, double sp) {
            auto& b = r.bodies_[i];
            b.frozen = false;
            b.centre = {s.centre.x + dx, 0.9 + dy, 0};
            b.angle = 0.3 * double(trial % 4);
            b.velocity = Vec3{-sp, 0, 0};
            b.spin = 0.8;
            PlaygroundRoom::placeCorners(b);
            b.previous = b.position;
            b.previousCentre = b.centre;
            b.previousAngle = b.angle;
        };
        if (trial % 2 == 0) throwBody(0, side * 2.2, 0.5, 2.0 + double(trial % 7));
        if (trial % 3 == 0) throwBody(1, -side * 2.4, 0.9, 1.5 + double(trial % 5));
        for (int k = 0; k < 160; ++k) {
            r.advance(1.0 / 20.0);
            const double deep = ballInsidePlank(r);
            const double inCube = ballInsideBody(r, 0), inTri = ballInsideBody(r, 1);
            if (deep > 1e-6 && std::getenv("CLIP")) {
                std::printf("trial%3d tick%3d ball (%+.3f,%+.3f) v(%+.2f,%+.2f) radius-gap %.5f | plank a %+.3f w %+.3f | cube (%+.2f,%+.2f) tri (%+.2f,%+.2f)\n",
                            trial, k, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                            deep, s.angle, s.spin, r.bodies_[0].centre.x, r.bodies_[0].centre.y,
                            r.bodies_[1].centre.x, r.bodies_[1].centre.y);
            }
            if (deep > worstBall) {
                worstBall = deep;
                worstBallCase = trial;
            }
            const double inP = bodyInsidePlank(r, 0);
            if (inP > 1e-6 && std::getenv("CLIPC"))
                std::printf(" cube t%3d k%3d deep %.5f | cube (%+.3f,%+.3f) a%+.3f v(%+.2f,%+.2f) w%+.2f frz%d | plank a%+.3f w%+.2f frz%d\n",
                            trial, k, inP, r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].angle,
                            r.bodies_[0].velocity.x, r.bodies_[0].velocity.y, r.bodies_[0].spin, int(r.bodies_[0].frozen),
                            s.angle, s.spin, int(s.frozen));
            if (inP > worstCube) { worstCube = inP; worstCubeCase = trial; worstCubeTick = k; }
            worstTri = std::max(worstTri, bodyInsidePlank(r, 1));
            if (inCube > 0.05 && std::getenv("CLIPB")) std::printf("  cube t%3d k%3d ball (%+.3f,%+.3f) cube c(%+.3f,%+.3f) a%+.3f deep %.5f\n", trial, k, r.ball_.position.x, r.ball_.position.y, r.bodies_[0].centre.x, r.bodies_[0].centre.y, r.bodies_[0].angle, inCube);
            if (inTri > 1e-6 && std::getenv("CLIPB")) std::printf("  tri  t%3d k%3d ball (%+.3f,%+.3f) tri c(%+.3f,%+.3f) a%+.3f deep %.5f\n", trial, k, r.ball_.position.x, r.ball_.position.y, r.bodies_[1].centre.x, r.bodies_[1].centre.y, r.bodies_[1].angle, inTri);
            worstInCube = std::max(worstInCube, inCube);
            worstInTri = std::max(worstInTri, inTri);
        }
    }
    std::printf("240 throws at the plank:\n");
    std::printf("  deepest the ball got inside the plank/a shape: %.6f m (trial %d)\n", worstBall, worstBallCase);
    std::printf("  cube %.6f m (trial %d tick %d) | triangle %.6f m\n", worstCube, worstCubeCase, worstCubeTick, worstTri);
    std::printf("  ball inside the cube %.6f | ball inside the triangle %.6f\n", worstInCube, worstInTri);
    return 0;
}
