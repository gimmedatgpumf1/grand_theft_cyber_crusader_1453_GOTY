// The rope, held to what a rope is.
//
// A rope is the one constraint in this room that is not a surface. These checks are
// the ones that would catch it ceasing to be one: a bob further from its anchor than
// the rope is long, a rope that pushes as well as pulls, a swing that gains energy
// instead of spending it, a pendulum whose period is not the pendulum's, a ball that
// passes through the bob, and -- the other way round -- a ball that is stopped by the
// rope itself, which has no collision and must not have one.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#define private public
#include "client/playground_pair.h"
#include "client/playground_room_tick.h"
#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#undef private

using gtc::native::PlaygroundRoom;
using gtc::native::Vec3;

namespace {

int localFailures = 0;

void say(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);
}

void check(bool holds, const char* what, const char* detail) {
    if (holds) {
        say("   ok    %s %s\n", what, detail);
    } else {
        say("   FAIL  %s %s\n", what, detail);
        ++localFailures;
    }
}

void settle(PlaygroundRoom& room, int ticks) {
    for (int tick = 0; tick < ticks; ++tick) room.advance(1.0 / 20.0);
}

// Puts the ball where it is aimed at the bob, and lets go of it.
void throwAtBob(PlaygroundRoom& room, Vec3 velocity, double fromTheSide) {
    const Vec3 bob = room.bobPosition();
    room.ball_.position = bob + Vec3{-fromTheSide, 0.0, 0.0};
    room.ball_.previous = room.ball_.shown = room.ball_.position;
    room.ball_.velocity = velocity;
    room.ball_.spin = 0.0;
    room.ball_.frozen = false;
}

double bobEnergy(const PlaygroundRoom& room) {
    return 0.5 * room.bobMass() * gtc::native::dot(room.bobVelocity(), room.bobVelocity()) +
           room.bobMass() * 9.8 * room.bobPosition().y;
}

}  // namespace

int playgroundRopeChecks() {
    char detail[320];
    say("\nthe rope: a constraint that pulls and never pushes\n");

    // 1. At rest it hangs at rest. Not "nearly": a bob that creeps is a bob whose
    //    tension is being worked out from something that is not its own weight.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        const Vec3 before = room.bobPosition();
        settle(room, 400);
        const double moved = gtc::native::length(room.bobPosition() - before);
        std::snprintf(detail, sizeof detail,
                      "400 ticks: the bob moved %.3e m (log2 %.1f), it is %.6f m below its anchor, the rope is "
                      "%.3f m and carrying %.3f N against a weight of %.3f N",
                      moved, moved > 0.0 ? std::log2(moved) : -1000.0, room.ropeAnchor().y - room.bobPosition().y,
                      room.ropeLength(), room.ropeTension(), room.bobMass() * 9.8);
        check(moved == 0.0 && std::abs(room.ropeTension() - room.bobMass() * 9.8) < 1e-9 && room.ropeTaut(),
              "a bob on a rope hangs at rest and stays there", detail);
    }

    // 2. Nothing gets it past its length. Three blows, from a tap to a hard one, with
    //    the whole path of the bob after each one checked -- not the end of the tick,
    //    every tick of four hundred.
    {
        double worst = 0.0, worstBallIn = 0.0;
        double bestSlack = 0.0;
        int blows = 0;
        for (const double speed : {3.0, 6.0, 14.0, 40.0}) {
            PlaygroundRoom room;
            room.reset();
            settle(room, 200);
            throwAtBob(room, Vec3{speed, 0.0, 0.0}, 0.45);
            for (int tick = 0; tick < 400; ++tick) {
                room.advance(1.0 / 20.0);
                worst = std::max(worst, room.bobDistance() - room.ropeLength());
                const double gap = gtc::native::length(room.ballPosition() - room.bobPosition()) -
                                   (room.ballRadius() + room.bobRadius());
                worstBallIn = std::min(worstBallIn, gap);
                bestSlack = std::max(bestSlack, room.ropeLength() - room.bobDistance());
                ++blows;
            }
        }
        std::snprintf(detail, sizeof detail,
                      "%d ticks of four blows (3, 6, 14, 40 m/s): the bob was never further than its rope by more "
                      "than %.3e m (log2 %.1f), the ball never closer to the bob than touching (%.3e m inside at "
                      "worst), and the rope went slack by up to %.3f m",
                      blows, worst, worst > 0.0 ? std::log2(worst) : -1000.0, -worstBallIn, bestSlack);
        check(worst <= gtc::native::playground_tick::kQuantum && worstBallIn >= -gtc::native::playground_tick::kQuantum &&
                  bestSlack > 0.1,
              "the rope holds the bob inside its length, and the bob may come closer", detail);
    }

    // 3. The swing keeps time, against a reference that is not this code: the same
    //    equation integrated four thousand times more finely, in the check.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        // Let it go half a radian out, from rest.
        const double length = room.ropeLength();
        room.pendulum_.position = room.ropeAnchor() + Vec3{std::sin(0.5), -std::cos(0.5), 0.0} * length;
        room.pendulum_.previous = room.pendulum_.shown = room.pendulum_.position;
        room.pendulum_.velocity = {};
        // The reference: theta'' = -(g/L) sin theta, by a fourth-order Runge-Kutta at a
        // five-thousandth of a tick, for the same four seconds.
        double theta = 0.5, omega = 0.0;
        const double step = (1.0 / 20.0) / 5000.0;
        const double pull = 9.8 / length;
        const auto slope = [&](double th, double om, double& dth, double& dom) { dth = om; dom = -pull * std::sin(th); };
        double drawnAngle = 0.0;
        for (int tick = 1; tick <= 80; ++tick) {
            room.advance(1.0 / 20.0);
            for (int sub = 0; sub < 5000; ++sub) {
                double k1t, k1o, k2t, k2o, k3t, k3o, k4t, k4o;
                slope(theta, omega, k1t, k1o);
                slope(theta + 0.5 * step * k1t, omega + 0.5 * step * k1o, k2t, k2o);
                slope(theta + 0.5 * step * k2t, omega + 0.5 * step * k2o, k3t, k3o);
                slope(theta + step * k3t, omega + step * k3o, k4t, k4o);
                theta += step / 6.0 * (k1t + 2.0 * k2t + 2.0 * k3t + k4t);
                omega += step / 6.0 * (k1o + 2.0 * k2o + 2.0 * k3o + k4o);
            }
            const Vec3 arm = room.bobPosition() - room.ropeAnchor();
            drawnAngle = std::atan2(arm.x, -arm.y);
        }
        const double error = std::abs(drawnAngle - theta);
        std::snprintf(detail, sizeof detail,
                      "4 s of a swing let go from 0.5 rad: the room's bob is at %.6f rad, a reference integration of "
                      "the same equation is at %.6f rad -- %.3e rad apart (log2 %.1f)",
                      drawnAngle, theta, error, error > 0.0 ? std::log2(error) : -1000.0);
        check(error < 2e-3, "a swing keeps the pendulum's own time", detail);
    }

    // 4. A pendulum spends its swing and never borrows any. The air in this room is
    //    real, so it decays -- but the total may never go up.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        const double length = room.ropeLength();
        room.pendulum_.position = room.ropeAnchor() + Vec3{std::sin(0.9), -std::cos(0.9), 0.0} * length;
        room.pendulum_.previous = room.pendulum_.shown = room.pendulum_.position;
        room.pendulum_.velocity = {};
        const double start = bobEnergy(room);
        double highest = start;
        for (int tick = 0; tick < 600; ++tick) {
            room.advance(1.0 / 20.0);
            highest = std::max(highest, bobEnergy(room));
        }
        const double rise = highest - start;
        std::snprintf(detail, sizeof detail,
                      "30 s of swinging from 0.9 rad: the bob's energy started at %.6f J and never rose above "
                      "%.6f J (rise %.3e J)", start, highest, rise);
        check(rise <= 1e-12, "a swing never makes energy", detail);
    }

    // 5. The ball takes and gives momentum with it. A head-on blow along x: whatever
    //    the ball loses along x, the bob gains, and the rope's own pull is along the
    //    rope, so it cannot change the pair's total across x.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        throwAtBob(room, Vec3{5.0, 0.0, 0.0}, 0.42);
        double before = 0.0, after = 0.0, worstRope = 0.0;
        double bobSpeed = 0.0, ballSpeed = 0.0;
        for (int tick = 0; tick < 200; ++tick) {
            const double was = room.ball_.mass * room.ballVelocity().x + room.bobMass() * room.bobVelocity().x;
            const double bobWas = gtc::native::length(room.bobVelocity());
            room.advance(1.0 / 20.0);
            const double now = room.ball_.mass * room.ballVelocity().x + room.bobMass() * room.bobVelocity().x;
            if (gtc::native::length(room.bobVelocity()) > bobWas + 0.2) {
                before = was;
                after = now;
            }
            bobSpeed = std::max(bobSpeed, gtc::native::length(room.bobVelocity()));
            ballSpeed = gtc::native::length(room.ballVelocity());
            worstRope = std::max(worstRope, room.bobDistance() - room.ropeLength());
        }
        const double lost = std::abs(after - before);
        std::snprintf(detail, sizeof detail,
                      "a ball at 5 m/s into a hanging bob: the bob came away at %.3f m/s and the ball was left at "
                      "%.3f m/s; the pair's x momentum changed by %.3e kg m/s over the blow (log2 %.1f)",
                      bobSpeed, ballSpeed, lost, lost > 0.0 ? std::log2(lost) : -1000.0);
        check(bobSpeed > 1.0 && ballSpeed > 0.1 && lost < 1e-6, "the ball takes and gives momentum with the bob", detail);
    }

    // 6. The rope has no collision. A ball thrown straight across the line of the rope,
    //    a good distance from the bob, must come out the other side with the speed it
    //    went in with: if the rope were a solid it would be stopped by it.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        const Vec3 anchor = room.ropeAnchor();
        const Vec3 bob = room.bobPosition();
        const double across = anchor.y - 0.45;  // a third of the way down the rope
        room.ball_.position = Vec3{anchor.x - 0.60, across, 0.0};
        room.ball_.previous = room.ball_.shown = room.ball_.position;
        room.ball_.velocity = Vec3{6.0, 0.0, 0.0};
        room.ball_.spin = 0.0;
        room.ball_.frozen = false;
        double slowest = 1e9;
        bool crossed = false;
        for (int tick = 0; tick < 40; ++tick) {
            room.advance(1.0 / 20.0);
            slowest = std::min(slowest, room.ballVelocity().x);
            if (room.ballPosition().x > anchor.x + 0.5) crossed = true;
        }
        std::snprintf(detail, sizeof detail,
                      "a ball thrown at 6 m/s across the rope at %.2f m below the anchor: it came out the other "
                      "side (%s) never slower than %.4f m/s, and the bob is still at (%.3f, %.3f)",
                      across - anchor.y, crossed ? "yes" : "no", slowest, room.bobPosition().x, bob.y);
        check(crossed && slowest > 5.5 && gtc::native::length(room.bobPosition() - bob) < 1e-9,
              "the rope has no collision: a ball thrown through it is not touched", detail);
    }

    return localFailures;
}
