// What the ledger actually holds, arrow by arrow, once the room is at rest.
//
// The King's picture is the triangle lying on the floor with its hypotenuse up and
// the seesaw's plank lying across it, and the room is drawing a 19.7 N arrow out of
// the middle of nothing and two 9.4 N arrows at one corner. This prints every force
// sample a settled room holds: what kind it is, who feels it, where it acts and how
// hard it pushes, and then the sum per solid -- which has to be the weight of what
// it is carrying and nothing else.
#include "common.h"
#include <cstdio>
#include <cstdlib>

namespace {
const char* kindName(PlaygroundRoom::ForceKind kind) {
    switch (kind) {
        case PlaygroundRoom::ForceKind::Gravity: return "gravity ";
        case PlaygroundRoom::ForceKind::Contact: return "contact ";
        case PlaygroundRoom::ForceKind::Friction: return "friction";
        case PlaygroundRoom::ForceKind::Pair: return "pair    ";
        case PlaygroundRoom::ForceKind::Ball: return "ball    ";
        case PlaygroundRoom::ForceKind::Drag: return "drag    ";
        case PlaygroundRoom::ForceKind::Rolling: return "rolling ";
        case PlaygroundRoom::ForceKind::KindCount: break;
    }
    return "??????  ";
}
const char* ownerName(std::size_t owner) {
    switch (owner) {
        case PlaygroundRoom::kCube: return "cube";
        case PlaygroundRoom::kTriangle: return "tri ";
        case PlaygroundRoom::kSeesaw: return "plank";
        case PlaygroundRoom::kBallOwner: return "ball";
        case PlaygroundRoom::kDotOwner: return "dot";
    }
    return "?   ";
}
}  // namespace

int main(int argc, char** argv) {
    PlaygroundRoom r;
    r.reset();
    r.toggleForceArrows();
    r.toggleForceNewtons();
    const int ticks = argc > 1 ? std::atoi(argv[1]) : 200;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    {
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        auto& c = r.bodies_[PlaygroundRoom::kCube];
        c.frozen = false;
        c.centre = {s.centre.x + 0.82, 1.62, 0};
        c.angle = 0.0;
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
    }
    for (int i = 0; i < ticks - 40; ++i) r.advance(1.0 / 20.0);
    std::printf("after %d ticks. ball (%+.4f,%+.4f) frz%d | dot (%+.3f,%+.3f) frz%d\n", ticks,
                r.ball_.position.x, r.ball_.position.y, int(r.ball_.frozen), r.point_.x, r.point_.y,
                int(r.pointFrozen_));
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
        const auto& b = r.bodies_[i];
        std::printf("  body %zu (%s) centre (%+.4f,%+.4f) a%+.4f frz%d corners:", i, ownerName(i), b.centre.x,
                    b.centre.y, b.angle, int(b.frozen));
        for (std::size_t k = 0; k < b.count; ++k) std::printf(" (%+.3f,%+.3f)", b.position[k].x, b.position[k].y);
        std::printf("\n");
    }
    std::array<Vec3, 5> sum{};
    double coupleSum[5]{};
    for (std::size_t i = 0; i < r.forceCount_; ++i) {
        const auto& f = r.forces_[i];
        std::printf("  %-9s %-5s at (%+.4f,%+.4f) f (%+.4f,%+.4f) |f| %7.3f N%s\n", kindName(f.kind),
                    ownerName(f.owner), f.at.x, f.at.y, f.force.x, f.force.y, length(f.force),
                    f.couple ? "  (couple)" : "");
        if (f.owner < 5) {
            sum[f.owner] = sum[f.owner] + f.force;
            coupleSum[f.owner] += f.moment;
        }
    }
    std::printf("  --- totals (should be zero for anything at rest) ---\n");
    for (std::size_t i = 0; i < 5; ++i)
        std::printf("  %-5s net (%+.4f,%+.4f) N | moment %+.5f N m\n", ownerName(i), sum[i].x, sum[i].y, coupleSum[i]);
    return 0;
}
