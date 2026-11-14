// Is the plank slow because it is not allowed to turn, or because it is heavy?
//
// A pinned body's turn is asked of pivotTurnAllowed() every tick. If that answers a
// fraction and the spin is left as it was, the plank turns at a fraction of the rate
// it says it is turning at -- which looks slow, sluggish, and nothing like a plank on
// an axle. This prints, every tick, what the plank's spin says it should have turned
// through, what it actually turned through, and how much of its turn was refused.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>
int main() {
    const double dt = 1.0 / 20.0;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(dt);
    auto& s = r.bodies_[2];
    s.angle = 0.45;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    s.frozen = false;
    s.spin = 0.0;
    // The ball on the raised end.
    r.ball_.position = {s.centre.x + 0.75, s.centre.y + 0.95, 0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    int refused = 0, ticking = 0;
    double worstLoss = 0.0;
    for (int k = 0; k < 240; ++k) {
        const double wasAngle = s.angle;
        const double wasSpin = s.spin;
        r.advance(dt);
        const double wanted = wasSpin * dt;
        const double did = s.angle - wasAngle;
        const double share = std::abs(wanted) > 1e-9 ? did / wanted : 1.0;
        if (std::abs(wasSpin) > 0.05) {
            ++ticking;
            if (share < 0.99) ++refused;
            worstLoss = std::max(worstLoss, 1.0 - share);
        }
        if (k % 4 == 0)
            std::printf("t%3d plank a%+.4f spin%+.4f | wanted %+.5f did %+.5f (%.0f%%) | kept spin %+.4f\n", k,
                        s.angle, wasSpin, wanted, did, 100.0 * share, s.spin);
    }
    std::printf("ticks where the plank was turning: %d | of those, refused part of the turn: %d (%.0f%%)\n", ticking,
                refused, ticking > 0 ? 100.0 * refused / ticking : 0.0);
    std::printf("worst refusal: %.0f%% of a tick's turn thrown away while the spin was kept\n", 100.0 * worstLoss);
    return 0;
}
