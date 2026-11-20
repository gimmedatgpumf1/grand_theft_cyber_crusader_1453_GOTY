// The ledger, per tick, added up by which solid feels it and what kind of force it
// is. A force that appears on one solid with no partner on the other is the thing
// this room may never do, and this is where that is visible as two numbers that do
// not match.
#include "common.h"
#include <map>
int main(int argc, char** argv) {
    const int which = argc > 1 ? std::atoi(argv[1]) : 0;
    const double dx = argc > 2 ? std::atof(argv[2]) : 0.10;
    const double height = argc > 3 ? std::atof(argv[3]) : 0.0;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {0.30, r.ball_.radius, 0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    auto& shape = r.bodies_[which];
    const double half = which == 0 ? 0.35 : 0.70;
    shape.centre = {0.30 + dx, r.ball_.radius + 0.12 + half + height, 0};
    shape.angle = 0.0;
    shape.velocity = {};
    shape.spin = 0.0;
    shape.frozen = false;
    PlaygroundRoom::placeCorners(shape);
    shape.previous = shape.position;
    shape.previousCentre = shape.centre;
    shape.previousAngle = shape.angle;
    const char* names[] = {"Gravity", "Contact", "Friction", "Pair", "Ball", "Drag", "Rolling"};
    for (int t = 1; t <= 16; ++t) {
        const Vec3 v0 = shape.velocity;
        const double s0 = shape.spin;
        const Vec3 bv = r.ball_.velocity;
        const double bs = r.ball_.spin;
        r.advance(1.0 / 20.0);
        double kin = 0.0, pot = 0.0;
        r.energyBudget(kin, pot);
        std::printf("t%2d E %.4f | shape dv(%+.4f,%+.4f) dsp %+.4f | ball dv(%+.4f,%+.4f) dsp %+.4f | clamped %zu\n", t,
                    kin + pot, shape.velocity.x - v0.x, shape.velocity.y - v0.y, shape.spin - s0,
                    r.ball_.velocity.x - bv.x, r.ball_.velocity.y - bv.y, r.ball_.spin - bs,
                    r.bodyStepClampedCount());
        std::map<std::pair<std::size_t, int>, Vec3> sums;
        for (std::size_t i = 0; i < r.forceCount(); ++i) {
            const auto& f = r.forces()[i];
            if (f.couple) continue;
            sums[{f.owner, int(f.kind)}] = sums[{f.owner, int(f.kind)}] + f.force * 0.05;
        }
        for (auto& [key, imp] : sums)
            std::printf("     owner %zu %-8s imp(%+.4f,%+.4f)\n", key.first, names[key.second], imp.x, imp.y);
    }
}
