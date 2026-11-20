// Every force the room is drawing, per body, with its kind and its size in newtons.
// A body at rest must have its arrows add to nothing: weight down, what holds it up.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

static const char* kindName(PlaygroundRoom::ForceKind k) {
    switch (k) {
        case PlaygroundRoom::ForceKind::Gravity: return "gravity";
        case PlaygroundRoom::ForceKind::Contact: return "CONTACT";
        case PlaygroundRoom::ForceKind::Friction: return "friction";
        case PlaygroundRoom::ForceKind::Pair: return "pair";
        case PlaygroundRoom::ForceKind::Ball: return "ball";
        case PlaygroundRoom::ForceKind::Drag: return "drag";
        case PlaygroundRoom::ForceKind::Rolling: return "rolling";
        default: return "?";
    }
}

static void dump(PlaygroundRoom& r, const char* when) {
    std::printf("\n===== %s =====\n", when);
    for (std::size_t i = 0; i <= PlaygroundRoom::kBodyCount; ++i) {
        const char* name = i == PlaygroundRoom::kBallOwner ? "ball" : i == PlaygroundRoom::kDotOwner ? "dot" : "";
        if (i < PlaygroundRoom::kBodyCount && r.bodies_[i].count == 0) continue;
        Vec3 sum{};
        double down = 0.0;
        std::printf("-- owner %zu %s\n", i, name);
        for (std::size_t f = 0; f < r.forceCount_; ++f) {
            const PlaygroundRoom::ForceSample& s = r.forces_[f];
            if (s.owner != i) continue;
            const double n = length(s.force);
            if (n < 1e-9) continue;
            sum = sum + s.force;
            if (i < PlaygroundRoom::kBodyCount && s.kind == PlaygroundRoom::ForceKind::Contact && s.force.y < 0.0)
                down += n;
            std::printf("   %-8s at (%+7.4f,%+7.4f) F (%+8.3f,%+8.3f) = %7.2f N\n", kindName(s.kind), s.at.x, s.at.y,
                        s.force.x, s.force.y, n);
        }
        if (i < PlaygroundRoom::kBodyCount) {
            const auto& b = r.bodies_[i];
            std::printf("   frozen %d  spin %+7.4f  angle %+7.4f  SUM (%+7.3f,%+7.3f) N   downward contact %.2f N\n",
                        int(b.frozen), b.spin, b.angle, sum.x, sum.y, down);
            std::printf("   applied[]:");
            for (std::size_t a = 0; a < b.appliedCount; ++a)
                std::printf(" [%s (%+7.3f,%+7.3f)]", kindName(b.applied[a].kind),
                            b.applied[a].impulse.x / 0.05, b.applied[a].impulse.y / 0.05);
            std::printf("\n");
        } else {
            std::printf("   SUM (%+7.3f,%+7.3f) N\n", sum.x, sum.y);
        }
    }
}

int main(int argc, char** argv) {
    const double dt = 1.0 / 20.0;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(dt);
    const int mode = argc > 1 ? std::atoi(argv[1]) : 0;
    if (mode == 1) {
        // The cube placed flat on the plank, then left to swing down to its stop --
        // the arrangement in the King's picture.
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        auto& c = r.bodies_[0];
        s.frozen = false;
        s.angle = 0.30;
        s.spin = 0.0;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        c.frozen = false;
        c.angle = s.angle;
        c.velocity = {};
        c.spin = 0.0;
        const double c1 = std::cos(s.angle), s1 = std::sin(s.angle);
        c.centre = {s.centre.x + c1 * 0.60 - s1 * 0.47, s.centre.y + s1 * 0.60 + c1 * 0.47, 0.0};
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
    }
    if (mode == 2) {
        // The plank held by the mouse near the angle in the King's picture, with the
        // cube standing on it.
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        auto& c = r.bodies_[0];
        r.dragTarget_ = PlaygroundRoom::DragTarget::Body;
        r.dragBody_ = PlaygroundRoom::kSeesaw;
        s.frozen = false;
        s.angle = -0.60;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.spin = 0.0;
        c.frozen = false;
        c.angle = s.angle;
        c.velocity = {};
        c.spin = 0.0;
        const double c1 = std::cos(s.angle), s1 = std::sin(s.angle);
        c.centre = {s.centre.x + c1 * 0.55 - s1 * 0.47, s.centre.y + s1 * 0.55 + c1 * 0.47, 0.0};
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
    }
    if (mode == 3 || mode == 4) {
        // The plank's end lying on the triangle -- what the King's picture shows. The
        // triangle is put where the plank's swinging end reaches it, and the cube rides
        // the other side. Nothing is held: the room is left to itself and settles.
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        auto& c = r.bodies_[0];
        auto& t = r.bodies_[1];
        s.frozen = false;
        s.angle = -0.55;
        s.spin = 0.0;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        // The triangle, standing on its own base, moved left until the plank's end is
        // resting on its top corner.
        t.frozen = false;
        t.centre = {mode == 3 ? -3.30 : -3.55, t.centre.y, 0.0};
        t.angle = 0.0;
        t.velocity = {};
        t.spin = 0.0;
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        t.previousAngle = t.angle;
        if (mode == 4) {
            c.frozen = false;
            c.angle = s.angle;
            c.velocity = {};
            c.spin = 0.0;
            const double c1 = std::cos(s.angle), s1 = std::sin(s.angle);
            c.centre = {s.centre.x + c1 * 0.55 - s1 * 0.47, s.centre.y + s1 * 0.55 + c1 * 0.47, 0.0};
            PlaygroundRoom::placeCorners(c);
            c.previous = c.position;
            c.previousCentre = c.centre;
            c.previousAngle = c.angle;
        } else {
            c.frozen = true;
            c.centre = {5.0, 3.0, 0.0};
            PlaygroundRoom::placeCorners(c);
        }
    }
    for (int k = 0; k < 240; ++k) r.advance(dt);
    dump(r, mode == 2 ? "plank held, cube on it" : "after settling");
    return 0;
}
