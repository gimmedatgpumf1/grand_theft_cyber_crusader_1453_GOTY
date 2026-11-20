// Does debug stepping go back and forth exactly?
//
// Advance 100 ticks, record the state. Step back 40: the state must be the
// state at tick 60, bit for bit. Step forward 40: the state must be the state
// at tick 100 again, bit for bit -- the tick is deterministic, so re-running
// it from a restored state must land in the same place. Also: the log of a
// tick names its energy, speeds, patches, forces and impulses; stepping back
// on an empty history fails; reset clears the history.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#define private public
#include "client/playground_room.h"
#undef private
using namespace gtc::native;

namespace {

struct State {
    double centre[3][2];
    double angle[3];
    double velocity[3][2];
    double spin[3];
    double ballPos[7][2];
    double ballVel[7][2];
    double ballSpin[7];
    double point[2];
    double pointVel[2];
    double energy;
    std::uint64_t tick;
};

State capture(PlaygroundRoom& r) {
    State s{};
    for (std::size_t i = 0; i < 3; ++i) {
        s.centre[i][0] = r.bodies_[i].centre.x;
        s.centre[i][1] = r.bodies_[i].centre.y;
        s.angle[i] = r.bodies_[i].angle;
        s.velocity[i][0] = r.bodies_[i].velocity.x;
        s.velocity[i][1] = r.bodies_[i].velocity.y;
        s.spin[i] = r.bodies_[i].spin;
    }
    for (std::size_t i = 0; i < 7; ++i) {
        s.ballPos[i][0] = r.balls_[i].position.x;
        s.ballPos[i][1] = r.balls_[i].position.y;
        s.ballVel[i][0] = r.balls_[i].velocity.x;
        s.ballVel[i][1] = r.balls_[i].velocity.y;
        s.ballSpin[i] = r.balls_[i].spin;
    }
    s.point[0] = r.point_.x;
    s.point[1] = r.point_.y;
    s.pointVel[0] = r.velocity_.x;
    s.pointVel[1] = r.velocity_.y;
    s.energy = r.totalEnergy();
    s.tick = r.tickCount();
    return s;
}

bool same(const State& a, const State& b) {
    return std::memcmp(&a, &b, sizeof(State)) == 0;
}

}  // namespace

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    PlaygroundRoom room;
    room.reset();
    check(room.tickCount() == 0, "tick count starts at 0");
    check(room.debugHistoryDepth() == 0, "history starts empty");
    check(!room.stepBack(), "step back on empty history fails");
    check(room.tickLog().empty(), "no log before the first tick");

    // Run 100 ticks, remembering tick 60 and tick 100.
    State at60{}, at100{};
    for (int tick = 1; tick <= 100; ++tick) {
        room.advance(1.0 / 20.0);
        if (tick == 60) at60 = capture(room);
        if (tick == 100) at100 = capture(room);
    }
    check(room.tickCount() == 100, "100 ticks ran");
    check(room.debugHistoryDepth() == 100, "history holds 100 entries");
    check(!room.tickLog().empty(), "tick log is filled");

    // The log names the things the / key promises.
    bool sawEnergy = false, sawBody = false, sawBall = false, sawPatch = false, sawForce = false,
         sawImpulse = false, sawCounters = false;
    for (const std::string& line : room.tickLog()) {
        if (line.rfind("ENERGY", 0) == 0) sawEnergy = true;
        if (line.rfind("BODY", 0) == 0) sawBody = true;
        if (line.rfind("BALL", 0) == 0) sawBall = true;
        if (line.rfind("PATCHES", 0) == 0) sawPatch = true;
        if (line.rfind("FORCES", 0) == 0) sawForce = true;
        if (line.rfind("IMPULSES", 0) == 0) sawImpulse = true;
        if (line.rfind("COUNTERS", 0) == 0) sawCounters = true;
    }
    check(sawEnergy && sawBody && sawBall && sawPatch && sawForce && sawImpulse && sawCounters,
          "log has energy, bodies, balls, patches, forces, impulses, counters");

    // Step back 40: exactly tick 60.
    for (int i = 0; i < 40; ++i) room.stepBack();
    check(room.tickCount() == 60, "stepped back to tick 60");
    check(same(capture(room), at60), "state at tick 60 is bit-identical after stepping back");

    // Step forward 40: exactly tick 100 again.
    for (int i = 0; i < 40; ++i) room.stepOnce();
    check(room.tickCount() == 100, "stepped forward to tick 100");
    check(same(capture(room), at100), "state at tick 100 is bit-identical after re-stepping");

    // Step back past the start: 100 more steps, the last ones fail.
    int backs = 0;
    while (room.stepBack()) ++backs;
    check(backs == 100, "stepped back to tick 0 and no further");
    check(room.tickCount() == 0, "back at tick 0");

    // Reset clears the debug state.
    for (int i = 0; i < 10; ++i) room.stepOnce();
    room.reset();
    check(room.tickCount() == 0 && room.debugHistoryDepth() == 0 && room.tickLog().empty(),
          "reset clears tick count, history and log");

    // History is capped: 700 ticks leave 600 entries.
    for (int i = 0; i < 700; ++i) room.advance(1.0 / 20.0);
    check(room.debugHistoryDepth() == 600, "history is capped at 600");
    check(room.tickCount() == 700, "tick count is not capped");

    std::printf(failures == 0 ? "debug-step: PASS\n" : "debug-step: FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
