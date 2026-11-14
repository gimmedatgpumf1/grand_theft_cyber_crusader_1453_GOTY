// A contact can only push. This hunts every contact force the room writes and asks
// which way it points: out of the surface it acts against, or into it. A contact that
// pushes a body into the thing it is touching is not a force, it is a bug with a sign.
//
// The surfaces are the floor and the other shapes' outlines. A contact written for a
// body is looked up at the place it acts: whichever surface is within a millimetre of
// that place is the one it is against, and the force has to lie on the outward side of
// it.
#include "common.h"
#include "client/playground_shapes.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

struct Flag {
    double newtons = 0.0;
    std::size_t owner = 0;
    Vec3 at{};
    Vec3 force{};
    const char* against = "";
};

static bool hunt(PlaygroundRoom& r, std::vector<Flag>& flags) {
    bool any = false;
    for (std::size_t i = 0; i < r.forceCount_; ++i) {
        const auto& s = r.forces_[i];
        if (s.kind != PlaygroundRoom::ForceKind::Contact) continue;
        if (s.owner >= PlaygroundRoom::kBodyCount) continue;
        if (!(length(s.force) > 1e-6)) continue;
        // The floor, if the place is on it.
        if (s.at.y <= r.ball_.radius + 1e-3 || s.at.y <= 1e-3) {
            if (s.force.y < -1e-9) {
                flags.push_back(Flag{length(s.force), s.owner, s.at, s.force, "the floor"});
                any = true;
            }
            continue;
        }
        // Another shape, if the place is on one.
        for (std::size_t other = 0; other < PlaygroundRoom::kBodyCount; ++other) {
            if (other == s.owner || r.bodies_[other].count == 0) continue;
            const auto gap = ball_shape::outlineGap(s.at, r.bodies_[other].position.data(),
                                                    r.bodies_[other].position.data(), r.bodies_[other].count, 1.0);
            if (std::abs(gap.gap) > 2e-3) continue;
            if (dot(s.force, gap.normal) < -1e-9) {
                flags.push_back(Flag{length(s.force), s.owner, s.at, s.force, "another shape"});
                any = true;
            }
            break;
        }
    }
    return any;
}

static const char* bodyName(std::size_t i) { return i == 0 ? "cube" : i == 1 ? "triangle" : i == 2 ? "plank" : "?"; }

int main() {
    const double dt = 1.0 / 20.0;
    int trials = 0, flagged = 0;
    double worst = 0.0;
    Flag worstFlag;
    for (int trial = 0; trial < 900; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 20; ++i) r.advance(dt);
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        auto& c = r.bodies_[0];
        auto& t = r.bodies_[1];
        // A plank at some angle, a cube somewhere on or near it, the triangle where it
        // already is, and the ball parked out of the way on most trials.
        s.frozen = false;
        s.angle = -0.85 + 1.7 * double(trial % 18) / 17.0;
        s.spin = 0.0;
        s.velocity = {};
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        c.frozen = false;
        c.angle = s.angle + 0.2 * double((trial / 18) % 5);
        c.velocity = {};
        c.spin = 0.0;
        const double along = -0.7 + 1.4 * double((trial / 90) % 10) / 9.0;
        const double up = 0.47 + 0.1 * double((trial / 900) % 2);
        const double c1 = std::cos(s.angle), s1 = std::sin(s.angle);
        c.centre = {s.centre.x + c1 * along - s1 * up, s.centre.y + s1 * along + c1 * up, 0.0};
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        t.frozen = true;  // the triangle stays where it is: the room's own starting pose
        if ((trial / 180) % 5 != 0) {
            r.ball_.frozen = true;
            r.ball_.position = {0.0, -5.0, 0.0};
        }
        ++trials;
        for (int k = 0; k < 120; ++k) {
            r.advance(dt);
            std::vector<Flag> flags;
            if (!hunt(r, flags)) continue;
            ++flagged;
            for (const Flag& f : flags) {
                if (f.newtons <= worst) continue;
                worst = f.newtons;
                worstFlag = f;
            }
            break;
        }
    }
    std::printf("%d of %d arrangements wrote a contact pushing into the surface it acts against\n", flagged, trials);
    if (flagged > 0)
        std::printf("worst %.3f N on the %s at (%+.3f,%+.3f) F (%+.3f,%+.3f), against %s\n", worst,
                    bodyName(worstFlag.owner), worstFlag.at.x, worstFlag.at.y, worstFlag.force.x,
                    worstFlag.force.y, worstFlag.against);
    return 0;
}
