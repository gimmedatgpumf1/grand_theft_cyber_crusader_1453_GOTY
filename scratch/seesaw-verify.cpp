// The seesaw, asked the questions the King asked of it.
//
// 1. Left alone, it does not turn and does not drift.
// 2. A load on the raised end brings it down and the pin does not drift.
// 3. Brought down onto a cube, it stops on the cube and does not cut into it, and
//    the cube does not creep.
// 4. A triangle forced into it after one tick is answered, and the plank does not
//    end up inside it.
// 5. A ball dropped on the raised end brings it down, and the ball does not end up
//    inside the plank and does not gain energy.
// 6. The King's own case: a cube set down on the plank stays where it is put -- no
//    slow drift to the left.
// 7. The ball on the raised end: does the room's total energy ever rise above where
//    it started?
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
        const auto& body = r.bodies_[i];
        if (body.count == 0) continue;
        e += 0.5 * body.mass * dot(body.velocity, body.velocity);
        e += 0.5 * body.moment * body.spin * body.spin;
        e += body.mass * kG * body.centre.y;
    }
    return e;
}

double lowestCorner(const PlaygroundRoom::Body& body) {
    double low = 1e30;
    for (std::size_t i = 0; i < body.count; ++i) low = std::min(low, body.position[i].y);
    return low;
}

// How deep the two outlines are into each other, zero if they are apart.
double into(const PlaygroundRoom& r, std::size_t one, std::size_t two) {
    Vec3 way{0, 1, 0};
    double amount = 0.0;
    const auto& a = r.bodies_[one];
    const auto& b = r.bodies_[two];
    if (!gtc::native::playground_detail::outlineOverlap(a.position.data(), a.count, b.position.data(), b.count, way, amount)) return 0.0;
    return amount;
}

double ballIntoBody(const PlaygroundRoom& r, std::size_t index) {
    const auto& body = r.bodies_[index];
    const double gap = gtc::native::ball_shape::outlineGap(r.ball_.position, body.position.data(), body.position.data(),
                                                           body.count, 1.0).gap;
    return r.ball_.radius - gap;
}

PlaygroundRoom settled() {
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 20; ++i) r.advance(kDt);
    return r;
}

}  // namespace

int main() {
    // 1. Alone: a plank balanced on its own middle stays where it is put.
    {
        PlaygroundRoom r = settled();
        const auto& s = r.bodies_[S];
        const double x0 = s.centre.x, a0 = s.angle;
        for (int i = 0; i < 400; ++i) r.advance(kDt);
        std::printf("1. alone 20 s               centre (%.9f,%.9f) angle %+.9f spin %+.9f\n", s.centre.x - x0,
                    s.centre.y - 0.850, s.angle - a0, s.spin);
    }
    // 2. A cube set down on the raised end brings the plank down, and the pin stays
    //    exactly where the axle is.
    {
        PlaygroundRoom r = settled();
        auto& s = r.bodies_[S];
        auto& c = r.bodies_[0];
        c.frozen = false;
        c.centre = {s.centre.x + 0.85, 1.60, 0};
        c.angle = 0.0;
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        const double e0 = energy(r);
        double rise = 0.0;
        for (int i = 0; i < 600; ++i) {
            r.advance(kDt);
            rise = std::max(rise, energy(r) - e0);
        }
        std::printf("2. loaded on the right end  angle %+.6f spin %+.9f pin drift %.9f rise %+.6f J | cube (%+.3f,%+.3f)\n",
                    s.angle, s.spin, gtc::native::length(Vec3{s.centre.x + 3.850, s.centre.y - 0.850, 0}), rise, c.centre.x,
                    c.centre.y);
    }
    // 3. The plank brought down onto a cube: it rests on the cube, does not cut into
    //    it, and the cube does not creep afterwards.
    {
        PlaygroundRoom r = settled();
        auto& s = r.bodies_[S];
        auto& c = r.bodies_[0];
        c.frozen = false;
        c.centre = {s.centre.x - 0.75, 0.35, 0};
        c.angle = 0.0;
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        s.spin = -1.2;  // tipped down onto the cube's side
        double deepest = 0.0;
        for (int i = 0; i < 100; ++i) {
            r.advance(kDt);
            deepest = std::max(deepest, into(r, S, 0));
        }
        const double x0 = c.centre.x;
        double crept = 0.0;
        for (int i = 0; i < 600; ++i) {
            r.advance(kDt);
            crept = std::max(crept, std::fabs(c.centre.x - x0));
        }
        std::printf("3. plank down onto a cube   deepest %.6f m | 30 s more: drift %+.9f m | plank %+.4f cube (%+.3f,%+.3f)\n",
                    deepest, c.centre.x - x0, s.angle, c.centre.x, c.centre.y);
    }
    // 4. A triangle forced into the plank after one tick.
    {
        PlaygroundRoom r = settled();
        auto& s = r.bodies_[S];
        auto& t = r.bodies_[1];
        t.frozen = false;
        for (int i = 0; i < 1; ++i) r.advance(kDt);
        t.centre = {s.centre.x + 0.30, t.centre.y + 0.05, 0};
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        double deepest = 0.0;
        for (int i = 0; i < 400; ++i) {
            r.advance(kDt);
            deepest = std::max(deepest, into(r, S, 1));
        }
        std::printf("4. triangle forced 0.05 m in -%.6f (worst) | after 20 s -%.6f | tri (%+.3f,%+.3f)\n", deepest,
                    into(r, S, 1), t.centre.x, t.centre.y);
    }
    // 5. A ball dropped on the raised end.
    {
        PlaygroundRoom r = settled();
        auto& s = r.bodies_[S];
        r.ball_.position = {s.centre.x + 0.80, 1.90, 0};
        r.ball_.velocity = {};
        r.ball_.spin = 0.0;
        r.ball_.frozen = false;
        r.ball_.previous = r.ball_.position;
        const double e0 = energy(r);
        double rise = 0.0, deepest = 0.0;
        for (int i = 0; i < 600; ++i) {
            r.advance(kDt);
            rise = std::max(rise, energy(r) - e0);
            deepest = std::max(deepest, ballIntoBody(r, S));
        }
        std::printf("5. ball dropped on raised end plank %+.4f | pin drift %.9f | rise %+.6f J | deepest ball-into-plank %.6f\n",
                    s.angle, gtc::native::length(Vec3{s.centre.x + 3.850, s.centre.y - 0.850, 0}), rise, deepest);
    }
    // 6. The King's drift: a cube set down on the tilted plank must stay where it was
    //    put. The plank is held at the angle it had when the cube was placed, so the
    //    only thing that can move the cube is the room.
    {
        PlaygroundRoom r = settled();
        auto& s = r.bodies_[S];
        auto& c = r.bodies_[0];
        // At the angle it comes to rest at with a load on that end, so the plank is
        // already stopped and everything that moves from here is creep.
        s.angle = -0.885167;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        c.frozen = false;
        c.centre = {-3.85 + 0.55 * std::cos(-0.885167) - 0.24 * std::sin(-0.885167),
                    0.85 + 0.55 * std::sin(-0.885167) + 0.24 * std::cos(-0.885167) + 0.35, 0};
        c.angle = -0.885167;
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        const double x0 = c.centre.x;
        double drift = 0.0;
        for (int i = 0; i < 600; ++i) {
            r.advance(kDt);
            drift = std::max(drift, std::fabs(c.centre.x - x0));
        }
        std::printf("6. cube on the tilted plank drift %.9f m over 30 s | plank %+.4f cube (%+.4f,%+.4f) a%+.4f\n", drift,
                    s.angle, c.centre.x, c.centre.y, c.angle);
    }
    // 7. The ball rolling down the raised end and off: no energy may be made.
    {
        PlaygroundRoom r = settled();
        auto& s = r.bodies_[S];
        s.angle = -0.30;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        r.ball_.position = {s.centre.x + 0.80, 1.40, 0};
        r.ball_.velocity = {};
        r.ball_.spin = 0.0;
        r.ball_.frozen = false;
        r.ball_.previous = r.ball_.position;
        const double e0 = energy(r);
        double worst = 0.0;
        int at = 0;
        for (int i = 0; i < 600; ++i) {
            r.advance(kDt);
            if (energy(r) - e0 > worst) {
                worst = energy(r) - e0;
                at = i;
            }
        }
        std::printf("7. ball at the raised end   worst rise %+.6f J (t%d) | final %.3f of start\n", worst, at,
                    energy(r) / e0);
    }
    return 0;
}
