// The King's picture: a cube standing on the raised end of the seesaw, and the ball
// gone to earth in the V between that cube's side and the triangle's slope.
//
// If it vibrates, it vibrates here, and every tick of it is printed: where the ball
// is, how fast, how fast it is turning, and what the shapes around it are doing.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr double S = PlaygroundRoom::kSeesaw;
constexpr double kDt = 1.0 / 20.0;
constexpr double kG = 9.8;

double energy(const PlaygroundRoom& r) {
    const auto& b = r.ball_;
    double e = 0.5 * b.mass * dot(b.velocity, b.velocity);
    e += 0.5 * (0.4 * b.mass * b.radius * b.radius) * b.spin * b.spin;
    e += b.mass * kG * b.position.y;
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
        const auto& x = r.bodies_[i];
        if (!x.count) continue;
        e += 0.5 * x.mass * dot(x.velocity, x.velocity);
        e += 0.5 * x.moment * x.spin * x.spin;
        e += x.mass * kG * x.centre.y;
    }
    return e;
}
}  // namespace

int main() {
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(kDt);
    auto& s = r.bodies_[S];
    auto& c = r.bodies_[0];
    auto& t = r.bodies_[1];
    // The plank tilted the way the King's picture has it: right end up.
    s.angle = 0.30;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    // The cube standing on that end.
    c.frozen = false;
    const Vec3 end{s.centre.x + 0.78 * std::cos(s.angle), s.centre.y + 0.78 * std::sin(s.angle) + 0.36, 0};
    c.centre = end;
    c.angle = s.angle;
    c.velocity = {};
    c.spin = 0.0;
    PlaygroundRoom::placeCorners(c);
    c.previous = c.position;
    c.previousCentre = c.centre;
    c.previousAngle = c.angle;
    // And the ball let go above the V its side makes with the triangle's slope.
    // In the open: the spot the picture has is inside the triangle's own body, and a
    // ball starting inside a shape is not a case, it is a broken setup -- the room
    // finds it through a face on tick zero and puts it out, and everything measured
    // after that is the putting-out. Raised along the line until it is clear.
    {
        Vec3 at{c.centre.x + 0.55, c.centre.y + 0.55, 0};
        auto clear = [&](Vec3 p) {
            for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
                const auto& b = r.bodies_[i];
                if (!b.count) continue;
                if (ball_shape::outlineGap(p, b.position.data(), b.position.data(), b.count, 1.0).gap <
                    r.ball_.radius + 1e-3)
                    return false;
            }
            return true;
        };
        while (at.y < 6.0 && !clear(at)) at.y += 0.02;
        r.ball_.position = at;
        std::printf("ball let go at (%+.4f,%+.4f)\n", at.x, at.y);
    }
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    const double e0 = energy(r);
    std::printf("start: plank %+.4f cube (%+.4f,%+.4f) a%+.4f ball (%+.4f,%+.4f)\n", s.angle, c.centre.x, c.centre.y,
                c.angle, r.ball_.position.x, r.ball_.position.y);
    for (int k = 0; k < 400; ++k) {
        r.advance(kDt);
        const auto& b = r.ball_;
        std::printf("t%-3d ball (%+.5f,%+.5f) v(%+.4f,%+.4f) w%+.4f frz%d | cube (%+.4f,%+.4f) a%+.4f w%+.4f frz%d"
                    " | plank %+.4f w%+.4f | dE %+7.3f\n",
                    k, b.position.x, b.position.y, b.velocity.x, b.velocity.y, b.spin, int(b.frozen), c.centre.x,
                    c.centre.y, c.angle, c.spin, int(c.frozen), s.angle, s.spin, energy(r) - e0);
    }
    return 0;
}
