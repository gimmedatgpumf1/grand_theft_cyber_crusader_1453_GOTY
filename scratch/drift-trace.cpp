// Scene 3 of the audit: a ball put down twenty millimetres above the floor, a metre
// clear of everything. It falls, it rolls, and the place keeps moving. What, exactly,
// is still pushing it?
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
using namespace gtc::native;
namespace { constexpr double kDt = 1.0 / 20.0; }
int main(int argc, char** argv) {
    const int from = argc > 1 ? std::atoi(argv[1]) : 1400;
    const int to = argc > 2 ? std::atoi(argv[2]) : 1500;
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 40; ++k) r.advance(kDt);
    const auto putBody = [&](std::size_t i, Vec3 centre, double angle) {
        auto& b = r.bodies_[i];
        b.centre = centre; b.angle = angle; b.velocity = {}; b.spin = 0; b.frozen = false;
        PlaygroundRoom::placeCorners(b);
        b.previous = b.position; b.previousCentre = b.centre; b.previousAngle = b.angle;
    };
    putBody(0, {-0.90, 0.35, 0}, 0.0);
    putBody(1, {1.15, 0.7, 0}, 0.5 * 3.14159265358979323846);
    r.ball_.position = {0.33, 0.14, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    for (int k = 0; k <= to; ++k) {
        if (k >= from) {
            std::printf("t%-4d ball (%.12f,%.12f) v(%+.4e,%+.4e) w%+.4e frz%d touch%d stand%d touches %zu |",
                        k, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                        r.ball_.spin, r.ball_.frozen ? 1 : 0, r.ball_.touching ? 1 : 0,
                        r.ball_.standing ? 1 : 0, r.ballTouchCount_);
            for (std::size_t i = 0; i < r.ballTouchCount_; ++i)
                std::printf(" n(%+.2f,%+.2f) p%.3e", r.ballTouches_[i].normal.x, r.ballTouches_[i].normal.y,
                            r.ballTouches_[i].pressed);
            for (std::size_t i = 0; i < r.forceCount(); ++i) {
                const auto& f = r.forces_[i];
                if (f.owner != PlaygroundRoom::kBodyCount) continue;
                if (f.kind == PlaygroundRoom::ForceKind::Friction)
                    std::printf(" |Fric(%+.3e,%+.3e)", f.force.x, f.force.y);
                if (f.kind == PlaygroundRoom::ForceKind::Rolling)
                    std::printf(" |Roll M%.3e (%+.3e,%+.3e)", f.moment, f.force.x, f.force.y);
                if (f.kind == PlaygroundRoom::ForceKind::Drag)
                    std::printf(" |Drag(%+.3e,%+.3e)", f.force.x, f.force.y);
                if (f.kind == PlaygroundRoom::ForceKind::Contact)
                    std::printf(" |Cont(%+.3e,%+.3e)", f.force.x, f.force.y);
            }
            double lowest = 1e9; for (std::size_t c = 0; c < r.bodies_[1].count; ++c) lowest = std::min(lowest, r.bodies_[1].position[c].y);
            std::printf(" | triLowest %.9f", lowest);
            for (std::size_t i = 0; i < r.forceCount(); ++i) {
                const auto& f = r.forces_[i];
                if (f.owner != 1) continue;
                std::printf(" |T%d(%+.4e,%+.4e)@(%+.3f,%+.3f)", int(f.kind), f.force.x, f.force.y, f.at.x, f.at.y);
            }
            std::printf(" | tri c(%.9f,%.9f) a%.9f v(%+.3e,%+.3e) w%+.3e", r.bodies_[1].centre.x, r.bodies_[1].centre.y, r.bodies_[1].angle, r.bodies_[1].velocity.x, r.bodies_[1].velocity.y, r.bodies_[1].spin);
            std::printf("\n");
        }
        r.advance(kDt);
    }
    return 0;
}
