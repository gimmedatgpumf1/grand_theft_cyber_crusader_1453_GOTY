#include "common.h"
#include "client/ui_layer.h"
#include <cstdio>
#include <cmath>
using namespace gtc::native;
int main() {
    setGpuRenderer(nullptr);
    PlaygroundRoom room; room.reset(); for (int i = 0; i < 300; ++i) room.advance(1.0/20.0);
    double restDrag = 0.0; int restOwner = -1;
    for (std::size_t i = 0; i < room.forceCount(); ++i)
        if (room.forces_[i].kind == PlaygroundRoom::ForceKind::Drag) {
            const double m = std::sqrt(dot(room.forces_[i].force, room.forces_[i].force));
            if (m > restDrag) { restDrag = m; restOwner = room.forces_[i].owner; }
        }
    std::printf("settled room: largest drag %.12e N by owner %d\n", restDrag, restOwner);
    for (int b = 0; b < (int)PlaygroundRoom::kBodyCount; ++b)
        std::printf("  body %d v(%+.3e,%+.3e) spin %+.3e frozen %d\n", b, room.bodies_[b].velocity.x, room.bodies_[b].velocity.y, room.bodies_[b].spin, room.bodies_[b].frozen?1:0);
    std::printf("  ball v(%+.3e,%+.3e) frozen %d\n", room.ball_.velocity.x, room.ball_.velocity.y, room.ball_.frozen?1:0);
    for (int b = 0; b < (int)PlaygroundRoom::kBodyCount; ++b)
        for (std::size_t i = 0; i < room.bodies_[b].count; ++i) {
            const Vec3 f = room.bodies_[b].position[i], t = room.bodies_[b].position[(i+1)%room.bodies_[b].count];
            std::printf("  body %d edge %zu span %.15f\n", b, i, std::sqrt(dot(t-f,t-f)));
        }
    for (double speed : {8.0}) {
        PlaygroundRoom moving; moving.reset(); for (int i=0;i<300;++i) moving.advance(1.0/20.0);
        moving.bodies_[0].frozen = false; moving.bodies_[0].velocity = {0, speed, 0};
        moving.clearForces();
        std::printf("  body0 before drag v(%+.15f,%+.15f) spin %+.3e\n", moving.bodies_[0].velocity.x, moving.bodies_[0].velocity.y, moving.bodies_[0].spin);
        for (std::size_t i = 0; i < moving.bodies_[0].count; ++i) {
            const Vec3 f = moving.bodies_[0].position[i], t = moving.bodies_[0].position[(i+1)%moving.bodies_[0].count];
            const Vec3 along = t - f; const double span = std::sqrt(dot(along,along));
            const Vec3 out{along.y/span, -along.x/span, 0.0};
            const Vec3 at = f + along*0.5;
            const Vec3 pv = pointSpeed(moving.bodies_[0], at);
            std::printf("   face %zu out(%+.3f,%+.3f) pointv(%+.15f,%+.15f) vn %+.15f\n", i, out.x, out.y, pv.x, pv.y, dot(pv,out));
        }
        moving.applyAirDrag();
        double shape = 0; for (std::size_t i = 0; i < moving.forceCount(); ++i)
            if (moving.forces_[i].kind == PlaygroundRoom::ForceKind::Drag) shape = std::max(shape, std::sqrt(dot(moving.forces_[i].force, moving.forces_[i].force)));
        PlaygroundRoom bally; bally.reset(); for (int i=0;i<300;++i) bally.advance(1.0/20.0);
        bally.ball_.velocity = {speed,0,0}; bally.ball_.frozen = false;
        bally.clearForces(); bally.tickBall();
        double ball = 0; for (std::size_t i = 0; i < bally.forceCount(); ++i)
            if (bally.forces_[i].kind == PlaygroundRoom::ForceKind::Drag) ball = std::max(ball, std::sqrt(dot(bally.forces_[i].force, bally.forces_[i].force)));
        const double e1 = 0.5*1.225*1.28*0.7*1.0*speed*speed;
        const double e2 = 0.5*1.225*0.47*3.14159265358979*0.12*0.12*speed*speed;
        std::printf("%.1f: shape %.12f expect %.12f d %.3e | ball %.12f expect %.12f d %.3e\n", speed, shape, e1, shape-e1, ball, e2, ball-e2);
    }
    return 0;
}
