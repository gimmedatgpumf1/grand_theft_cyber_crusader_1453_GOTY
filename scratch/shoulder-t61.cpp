#include "common.h"
int main() {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    r.ball_.position = {1.395, r.ball_.radius, 0};
    r.ball_.velocity = {}; r.ball_.spin = 0.0; r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    PlaygroundRoom::Body& cube = r.bodies_[0];
    cube.centre = {1.395 + 0.16, 0.36 + 0.5, 0};
    cube.angle = 0.0; cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    for (int t = 1; t <= 63; ++t) {
        const Vec3 v0 = r.ball_.velocity;
        r.advance(1.0 / 20.0);
        if (t < 58 || t > 62) continue;
        Vec3 j{};
        for (std::size_t i = 0; i < r.forceCount_; ++i) if (r.forces_[i].owner == 2) j = j + r.forces_[i].force * playground_tick::kTick;
        std::printf("t%-3d ball (%+.4f,%+.4f) v(%+.3f,%+.3f)->(%+.3f,%+.3f) spin %+.2f frz %d touch %d stand %d | account %+.4f vs %+.4f\n",
                    t, r.ball_.position.x, r.ball_.position.y, v0.x, v0.y, r.ball_.velocity.x, r.ball_.velocity.y,
                    r.ball_.spin, (int)r.ball_.frozen, (int)r.ball_.touching, (int)r.ball_.standing,
                    j.x, (r.ball_.velocity.x - v0.x) * r.ball_.mass);
        for (std::size_t i = 0; i < r.forceCount_; ++i) {
            const auto& f = r.forces_[i];
            if (f.owner != 2) continue;
            std::printf("      kind %d at (%+.3f,%+.3f) F(%+7.2f,%+7.2f) = %+.4f N s\n", (int)f.kind, f.at.x, f.at.y,
                        f.force.x, f.force.y, f.force.y * playground_tick::kTick);
        }
    }
    return 0;
}
