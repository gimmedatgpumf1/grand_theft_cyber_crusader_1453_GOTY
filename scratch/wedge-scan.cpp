// Hunting for a ball that will not lie still.
//
// Every arrangement of the King's own picture: the plank tilted up, the cube on its
// raised end and jammed between the plank and the triangle's slope, and the ball let
// go into the notch that leaves. Each is run for six hundred ticks and then watched
// for two hundred more. A ball that has come to rest stops; a ball that is vibrating
// keeps a speed and keeps turning round. Both are printed.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
constexpr double S = PlaygroundRoom::kSeesaw;
constexpr double kDt = 1.0 / 20.0;

struct Verdict {
    double speed = 0.0;      // fastest it went over the last stretch
    double excursion = 0.0;  // how far it moved over the last stretch
    double span = 0.0;       // the width of the place it kept to
    int flips = 0;           // how many times it turned round
};
}  // namespace

int main() {
    double worstSpeed = 0.0;
    int worstCase = -1;
    int caseIndex = 0;
    for (double plank = 0.05; plank <= 0.62; plank += 0.075) {
        for (double along = 0.45; along <= 0.95; along += 0.10) {
            for (double drop = 0.0; drop <= 3.0; drop += 0.5) {
                PlaygroundRoom r;
                r.reset();
                for (int i = 0; i < 40; ++i) r.advance(kDt);
                auto& s = r.bodies_[S];
                auto& c = r.bodies_[0];
                s.angle = plank;
                PlaygroundRoom::placeCorners(s);
                s.previous = s.position;
                s.previousCentre = s.centre;
                s.previousAngle = s.angle;
                c.frozen = false;
                c.centre = {s.centre.x + along * std::cos(plank), s.centre.y + along * std::sin(plank) + 0.36, 0};
                c.angle = plank;
                c.velocity = {};
                c.spin = 0.0;
                PlaygroundRoom::placeCorners(c);
                c.previous = c.position;
                c.previousCentre = c.centre;
                c.previousAngle = c.angle;
                // The notch, or the air above it.
                r.ball_.position = {c.centre.x + 0.50, c.centre.y - 0.10 + drop * 0.18, 0};
                r.ball_.velocity = {};
                r.ball_.spin = 0.0;
                r.ball_.frozen = false;
                r.ball_.previous = r.ball_.position;
                for (int k = 0; k < 600; ++k) r.advance(kDt);
                const Vec3 was = r.ball_.position;
                double lo = 1e30, hi = -1e30, fast = 0.0;
                int flips = 0;
                double lastX = was.x, lastVx = r.ball_.velocity.x;
                for (int k = 0; k < 200; ++k) {
                    r.advance(kDt);
                    const Vec3 at = r.ball_.position;
                    lo = std::min(lo, at.y);
                    hi = std::max(hi, at.y);
                    fast = std::max(fast, gtc::native::length(r.ball_.velocity));
                    if ((r.ball_.velocity.x > 1e-4 && lastVx < -1e-4) || (r.ball_.velocity.x < -1e-4 && lastVx > 1e-4))
                        ++flips;
                    lastVx = r.ball_.velocity.x;
                    lastX = at.x;
                }
                (void)lastX;
                if (fast > worstSpeed) {
                    worstSpeed = fast;
                    worstCase = caseIndex;
                }
                if (fast > 0.01 || flips > 3) {
                    std::printf("plank %+.3f along %+.2f drop %+.2f | final speed %.4f m/s, turned round %d times, bob %.4f m\n",
                                plank, along, drop, fast, flips, hi - lo);
                }
                ++caseIndex;
            }
        }
    }
    std::printf("\n%d arrangements; worst final speed %.4f m/s (case %d)\n", caseIndex, worstSpeed, worstCase);
    return 0;
}
