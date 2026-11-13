// Does the solved sweep agree with the truth?
//
// The turn's meeting with the floor, with another shape and with the ball is solved
// outright in pivotTurnAllowed(). Here the same question is answered the slow, dumb
// way -- by stepping along the turn in slices far finer than anything in the room and
// closing in on the first slice that is inside something -- and the two answers are
// compared. A sweep that is right agrees with the dumb answer everywhere. A sweep
// that is wrong either lets the body through something (it says yes where the truth
// says a fraction) or stops it for nothing (it says a fraction where the truth says
// the whole turn is clear).
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr double S = PlaygroundRoom::kSeesaw;

// The same two questions the sweep asks: is it under the floor at all, and is it
// deeper into another shape than it already is when the turn starts.
double depthOf(const PlaygroundRoom& r, double angle) {
    const auto& body = r.bodies_[S];
    return r.pivotFloorDepthAt(body, angle);
}

double objectDepthOf(const PlaygroundRoom& r, double angle) {
    const auto& body = r.bodies_[S];
    return r.pivotObjectDepthAt(body, angle);
}

// The truth: the first fraction of `turn` at which the plank would be in anything,
// found by slicing.
double truth(const PlaygroundRoom& r, double turn) {
    const int slices = 20000;
    const double allowance = std::max(0.0, objectDepthOf(r, r.bodies_[S].angle) - 1e-6);
    const auto free = [&](double angle) {
        return depthOf(r, angle) <= 1e-6 && objectDepthOf(r, angle) <= allowance + 1e-6;
    };
    double clear = 0.0;
    for (int i = 1; i <= slices; ++i) {
        const double fraction = double(i) / double(slices);
        if (free(r.bodies_[S].angle + turn * fraction)) {
            clear = fraction;
            continue;
        }
        double low = clear, high = fraction;
        for (int b = 0; b < 60; ++b) {
            const double mid = 0.5 * (low + high);
            if (free(r.bodies_[S].angle + turn * mid))
                low = mid;
            else
                high = mid;
        }
        return low;
    }
    return 1.0;
}
}  // namespace

int main() {
    int bad = 0, total = 0;
    double worstMiss = 0.0;
    for (int trial = 0; trial < 120; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 20; ++i) r.advance(1.0 / 20.0);
        auto& s = r.bodies_[S];
        auto& c = r.bodies_[0];
        auto& t = r.bodies_[1];
        // Something in the way, in a legal place.
        s.angle = -0.8 + 1.6 * double(trial % 11) / 10.0;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        c.frozen = false;
        c.centre = {s.centre.x - 0.9 + 1.8 * double((trial * 7) % 11) / 10.0, 1.0 + 0.2 * double(trial % 4), 0};
        c.angle = 0.3 * double(trial % 5);
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        t.frozen = false;
        t.centre = {-2.0 - 0.3 * double(trial % 3), 0.7, 0};
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        for (int k = 0; k < 60; ++k) r.advance(1.0 / 20.0);
        // Now ask, from wherever it has got to, about a handful of turns.
        for (int which = 0; which < 6; ++which) {
            const double turn = (which % 2 == 0 ? 1.0 : -1.0) * (0.02 + 0.12 * double(which));
            const double solved = r.pivotTurnAllowed(s, turn);
            const double slow = truth(r, turn);
            ++total;
            const double miss = std::abs(solved - slow) * std::abs(turn);
            // In radians of plank: the sweep may not be out by more than the room's
            // resolution at the tip.
            if (miss > 1e-6) {
                ++bad;
                if (miss > worstMiss) worstMiss = miss;
                if (bad <= 12)
                    std::printf("MISS trial%d turn %+.3f: solved %.9f truth %.9f (out by %.3g rad)\n", trial, turn,
                                solved, slow, miss);
            }
        }
    }
    std::printf("%d of %d turns disagreed, worst by %.3g rad\n", bad, total, worstMiss);
    return 0;
}
