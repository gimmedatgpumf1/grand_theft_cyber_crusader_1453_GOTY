// What the air does to a ball in one tick, across nine decades of speed.
//
// Drag is a force: F = half rho Cd A v^2, against the motion. Stepping that force the
// plain way -- v -= (F/m) dt -- is right only while the tick is short compared with
// the time the air needs to take the ball's speed. Write p = m v for the ball's
// momentum: the step removes F dt / p of it, which is (c/m) v dt. At a hundred
// kilometres a second that is 65: the air is asked for sixty-five times the momentum
// the ball has, so the ball does not slow down, it turns round -- and the next tick
// asks for more, and the one after that more again, and within eight ticks every
// number in the room is a NaN. That is not the air being strong; it is the step
// running out.
//
// The law has an exact answer over a tick:
//
//     dv/dt = -(c/m) v^2   =>   v(t) = v / (1 + (c/m) v t),   x(t) = ln(1 + (c/m) v t) / (c/m)
//
// so the speed falls by 1/(1 + (c/m) v dt) and never crosses nought. The table prints
// both: the share of the ball's momentum the plain step would have taken (which is 1
// or less while the step is honest), and the factor the exact answer gives. The last
// two columns are the displacement the room walked and the one the drag law says.
//
//     g++ -pthread -std=c++20 -O2 -Iscratch -Isrc -Inative -Inative/client \
//         scratch/air-drag-per-tick.cpp build/obj/native-client/playground*.o \
//         -o build/bin/air-drag-per-tick

#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private

#include <cmath>
#include <cstdio>

using namespace gtc::native;

int main() {
    // c/m for the ball, from the room's own numbers: half rho Cd A / mass.
    PlaygroundRoom room;
    room.reset();
    const double radius = room.ball_.radius;
    const double mass = room.ball_.mass;
    const double c = 0.5 * ball_shape::kAirDensity * ball_shape::kBallDragCoefficient *
                     3.141592653589793 * radius * radius;
    const double perMetre = c / mass;
    const double dt = playground_tick::kTick;
    std::printf("radius %.3f m, mass %.3f kg, half rho Cd A %.6f N/(m/s)^2, c/m %.6f 1/m, tick %.3f s\n\n", radius,
                mass, c, perMetre, dt);
    std::printf("%12s %9s %14s %9s %9s %14s %9s %13s %13s\n", "v in m/s", "log2 v", "F(v) N", "F dt/p",
                "log2", "v out m/s", "log2 fac", "walked m", "law says m");
    for (const double v0 : {1.0, 10.0, 100.0, 1000.0, 1e4, 1e5, 1e6, 1e7, 1e8}) {
        PlaygroundRoom runner;
        runner.reset();
        runner.ball_.position = Vec3{0.0, 50.0, 0};  // in the open air, nothing near it
        runner.ball_.velocity = Vec3{-v0, 0, 0};
        runner.ball_.spin = 0.0;
        runner.ball_.frozen = false;
        runner.advance(dt);
        // Along the motion only: the tick's own weight is on the other axis.
        const double out = std::abs(runner.ball_.velocity.x);
        const double walked = std::abs(runner.ball_.position.x);
        const double share = perMetre * v0 * dt;                 // F dt / (m v), the plain step's share
        const double law = std::log(1.0 + share) / perMetre;      // the exact displacement
        std::printf("%12.0f %9.2f %14.6e %9.3f %9.3f %14.6e %9.3f %13.6e %13.6e\n", v0, std::log2(v0),
                    c * v0 * v0, share, std::log2(share), out, std::log2(out / v0), walked, law);
    }
    return 0;
}
