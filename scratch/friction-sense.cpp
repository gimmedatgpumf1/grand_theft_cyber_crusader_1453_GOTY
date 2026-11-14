// Two questions about the arrows, asked over the scene the King photographed.
//
// 1. Friction. A rigid face pressed on the floor is pressed along the floor as one
//    traction: both ends of it are held the same way. Two friction arrows at the two
//    ends of one contact that point in opposite directions are not a traction, they
//    are a couple that nothing applied -- a shape standing on the floor and being
//    slowly rotated by nothing.
//
// 2. Normals. Every normal force has a face it acts on, and it is square to it. A
//    contact whose force is not along the face normal it is pressed against is not a
//    normal force, whatever it is called.
#include "common.h"
#include "client/playground_pair.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr double kDt = 1.0 / 20.0;
const char* bodyName(std::size_t i) { return i == 0 ? "cube" : i == 1 ? "triangle" : i == 2 ? "plank" : "?"; }

struct Report {
    int oppositePairs = 0;
    double worstOpposite = 0.0;
    int trial = -1, tick = -1;
    std::size_t body = 0;
};

// Friction at the floor, end by end, for one body: are the ends held the same way?
void frictionCheck(PlaygroundRoom& r, Report& rep, int trial, int tick) {
    for (std::size_t index = 0; index < PlaygroundRoom::kBodyCount; ++index) {
        const auto& body = r.bodies_[index];
        if (body.count == 0 || body.pivoted) continue;
        double left = 0.0, right = 0.0;
        int count = 0;
        for (std::size_t f = 0; f < r.forceCount_; ++f) {
            const auto& s = r.forces_[f];
            if (s.owner != index || s.kind != PlaygroundRoom::ForceKind::Friction || s.couple) continue;
            if (s.at.y > playground_tick::kFaceFlat) continue;  // only the floor
            ++count;
            if (s.at.x < body.centre.x) left = s.force.x; else right = s.force.x;
        }
        if (count < 2) continue;
        if (left * right < 0.0 && std::min(std::abs(left), std::abs(right)) > 0.05) {
            ++rep.oppositePairs;
            const double both = std::min(std::abs(left), std::abs(right));
            if (both > rep.worstOpposite) {
                rep.worstOpposite = both;
                rep.trial = trial;
                rep.tick = tick;
                rep.body = index;
            }
        }
    }
}
}  // namespace

int main() {
    Report rep;
    // The cube is let go above the triangle's slope and slides down it; the triangle
    // is shoved; the ball is thrown at the pair. Whatever the room draws in play.
    for (int trial = 0; trial < 60; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 5; ++k) r.advance(kDt);
        auto& c = r.bodies_[0];
        c.centre = {-1.30 + 0.25 * double(trial % 5) - 0.5, 1.30 + 0.22 * double(trial / 5), 0};
        c.angle = 0.7854;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        c.velocity = {};
        c.spin = 0.0;
        c.frozen = false;
        if (trial % 3 == 0) {
            c.velocity = {0.8 - 0.2 * double(trial % 4), -0.2, 0};
            c.spin = 0.4;
        }
        r.ball_.position = {-0.4 - 0.35 * double(trial % 6), 0.12 + 0.35 * double(trial % 4), 0};
        r.ball_.velocity = {-1.2 - 0.2 * double(trial % 7), 0.4, 0};
        r.ball_.previous = r.ball_.position;
        for (int k = 0; k < 120; ++k) {
            r.advance(kDt);
            frictionCheck(r, rep, trial, k);
        }
    }
    std::printf("friction at the two ends of one floor contact, over 60 arrangements:\n");
    if (rep.oppositePairs == 0) {
        std::printf("  never pulled two ways at once.\n");
    } else {
        std::printf("  %d ticks where the two ends were held in opposite directions, worst %.3f N "
                    "[trial %d tick %d %s]\n",
                    rep.oppositePairs, rep.worstOpposite, rep.trial, rep.tick, bodyName(rep.body));
    }
    return 0;
}
