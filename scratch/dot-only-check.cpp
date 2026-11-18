// Dot-only mode: the room emptied down to the point and the floor. Verifies the
// bodies, balls and ropes are parked and frozen, the dot falls and bounces on
// the floor without ever going through it or producing NaN, and debug stepping
// still works in the emptied room.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#define private public
#include "client/playground_pair.h"
#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#undef private

using namespace gtc::native;

namespace {
int failures = 0;
void check(bool holds, const char* what, const char* detail = "") {
    std::printf("%s %s %s\n", holds ? "ok  " : "FAIL", what, detail);
    if (!holds) ++failures;
}
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}  // namespace

int main() {
    PlaygroundRoom room;
    room.setDotOnly(true);
    if (!room.reset()) { std::printf("FAIL reset returned false\n"); return 1; }

    // Everything but the dot is parked below the floor and frozen.
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
        const auto& body = room.bodies_[i];
        check(body.frozen && body.centre.y == -1000.0, "body parked frozen below floor");
        check(finite(body.centre) && finite(body.velocity), "body state finite");
    }
    for (std::size_t i = 0; i < PlaygroundRoom::kBallCount; ++i) {
        const auto& ball = room.balls_[i];
        check(ball.frozen && ball.position.y == -1000.0, "ball parked frozen below floor");
        check(finite(ball.position) && finite(ball.velocity), "ball state finite");
    }
    for (std::size_t i = 0; i < PlaygroundRoom::kRopeCount; ++i) {
        const auto& rope = room.ropeForBall(i + 1);
        check(!rope.attached && rope.anchor.y == -1000.0, "rope detached, anchor parked");
    }

    // The dot starts at the top and falls to the floor.
    const Vec3 start = room.point();
    check(start.y > 1.5, "dot starts at the top", "");
    double minY = 1e30, maxY = -1e30;
    bool bounced = false;
    double prevVy = 0.0;
    for (int tick = 0; tick < 400; ++tick) {
        room.advance(0.05);  // one 20 Hz tick
        const Vec3 p = room.point();
        const Vec3 v = room.velocity_;
        if (!finite(p) || !finite(v)) { check(false, "dot state finite every tick"); break; }
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
        if (prevVy < 0.0 && v.y > 0.0) bounced = true;
        prevVy = v.y;
    }
    check(minY >= 0.07 - 1e-9, "dot never goes through the floor", "");
    check(bounced, "dot bounces on the floor", "");
    check(room.tickCount() == 400, "400 ticks ran", "");

    // Debug stepping still works in the emptied room.
    const std::uint64_t before = room.tickCount();
    room.stepOnce();
    check(room.tickCount() == before + 1, "stepOnce advances one tick", "");
    check(room.stepBack(), "stepBack succeeds", "");
    check(room.tickCount() == before, "stepBack returns to the tick", "");
    check(!room.tickLog().empty(), "tick log is built in dot-only mode", "");

    // Turning the mode off brings the full room back.
    room.setDotOnly(false);
    room.reset();
    bool anyRemoved = false;
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i)
        if (room.bodies_[i].count == 0) anyRemoved = true;
    for (std::size_t i = 0; i < PlaygroundRoom::kBallCount; ++i)
        if (!room.balls_[i].active) anyRemoved = true;
    check(!anyRemoved, "full room returns when dot-only is off", "");

    std::printf(failures ? "dot-only: FAIL (%d)\n" : "dot-only: PASS\n", failures);
    return failures ? 1 : 0;
}
