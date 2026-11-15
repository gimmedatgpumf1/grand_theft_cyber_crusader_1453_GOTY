// The rope, held to what a rope is.
//
// A rope is the one constraint in this room that is not a surface. These checks are
// the ones that would catch it ceasing to be one: a ball further from its anchor than
// the rope is long, a rope that pushes as well as pulls, a swing that gains energy
// instead of spending it, a swing whose period is not the pendulum's, a ball that
// passes through the ball on the rope, and -- the other way round -- a ball that is
// stopped by the rope itself, which has no collision and must not have one.
//
// The thing on the end of the rope is a ball, so everywhere below it is read and set
// as a ball: there is no second kind of object here to have its own name.

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

// Puts the ball where it is aimed at the ball on the rope, and lets go of it.
void throwAtBob(PlaygroundRoom& room, Vec3 velocity, double fromTheSide) {
    const Vec3 bob = room.ropeBall().position;
    room.ball(PlaygroundRoom::kThrowBall).position = bob + Vec3{-fromTheSide, 0.0, 0.0};
    room.ball(PlaygroundRoom::kThrowBall).previous = room.ball(PlaygroundRoom::kThrowBall).shown = room.ball(PlaygroundRoom::kThrowBall).position;
    room.ball(PlaygroundRoom::kThrowBall).velocity = velocity;
    room.ball(PlaygroundRoom::kThrowBall).spin = 0.0;
    room.ball(PlaygroundRoom::kThrowBall).frozen = false;
}

// The energy of the ball on the rope: its travel and its height, the only two things a
// ball on a rope has that a constraint like this can change.
double bobEnergy(const PlaygroundRoom& room) {
    const auto& ball = room.ropeBall();
    return 0.5 * ball.mass * gtc::native::dot(ball.velocity, ball.velocity) + ball.mass * 9.8 * ball.position.y;
}

}  // namespace

int playgroundRopeChecks() {
    char detail[340];
    say("\nthe rope: a constraint that pulls and never pushes\n");

    // 1. At rest it hangs at rest. Not "nearly": a bob that creeps is a bob whose
    //    tension is being worked out from something that is not its own weight.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        const Vec3 before = room.ropeBall().position;
        settle(room, 400);
        const double moved = gtc::native::length(room.ropeBall().position - before);
        std::snprintf(detail, sizeof detail,
                      "400 ticks: the bob moved %.3e m (log2 %.1f), it is %.6f m below its anchor, the rope is "
                      "%.3f m and carrying %.3f N against a weight of %.3f N",
                      moved, moved > 0.0 ? std::log2(moved) : -1000.0, room.ropeAnchor().y - room.ropeBall().position.y,
                      room.ropeLength(), room.ropeTension(), room.ropeBall().mass * 9.8);
        check(moved == 0.0 && std::abs(room.ropeTension() - room.ropeBall().mass * 9.8) < 1e-9 && room.ropeTaut(),
              "a ball on a rope hangs at rest and stays there", detail);
    }

    // 2. Nothing gets it past its length. Four blows, from a tap to a hard one, with
    //    every tick of the four hundred after each one checked -- not the end of the
    //    tick, every tick -- and the ball checked against the bob at the same time.
    {
        double worst = 0.0, worstBallIn = 0.0, bestSlack = 0.0;
        int ticks = 0;
        for (const double speed : {3.0, 6.0, 14.0, 40.0}) {
            PlaygroundRoom room;
            room.reset();
            settle(room, 200);
            throwAtBob(room, Vec3{speed, 0.0, 0.0}, 0.45);
            for (int tick = 0; tick < 400; ++tick) {
                room.advance(1.0 / 20.0);
                worst = std::max(worst, room.ropeBallDistance() - room.ropeLength());
                worstBallIn = std::min(worstBallIn,
                                       gtc::native::length(room.ballPosition() - room.ropeBall().position) -
                                           (room.ballRadius() + room.ropeBall().radius));
                bestSlack = std::max(bestSlack, room.ropeLength() - room.ropeBallDistance());
                ++ticks;
            }
        }
        std::snprintf(detail, sizeof detail,
                      "%d ticks of four blows (3, 6, 14, 40 m/s): the bob was never further than its rope by more "
                      "than %.3e m (log2 %.1f), the ball never got closer to the bob's centre than the two radii "
                      "added up (worst gap %.3e m, log2 %.1f, which is the rounding of the contact solve), and the "
                      "rope went slack by up to %.3f m",
                      ticks, worst, worst > 0.0 ? std::log2(worst) : -1000.0, worstBallIn,
                      worstBallIn > 0.0 ? std::log2(worstBallIn) : -1000.0, bestSlack);
        check(worst <= gtc::native::playground_tick::kQuantum &&
                  worstBallIn >= -gtc::native::playground_tick::kQuantum && bestSlack > 0.1,
              "the rope holds the ball inside its length, and the ball may come closer", detail);
    }

    // 3. The swing keeps time. Mass does not appear in theta'' = -(g/L) sin theta, so a
    //    bob heavy enough for the air not to be able to touch it must trace the
    //    textbook swing exactly: the same equation integrated a five-thousandth of a
    //    tick at a time, by fourth-order Runge-Kutta, for eight seconds.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        room.ball(PlaygroundRoom::kRopeBall).mass = 1.0e6;  // the air's share of a swing: (c/m) is a millionth
        const double length = room.ropeLength();
        room.ball(PlaygroundRoom::kRopeBall).position = room.ropeAnchor() + Vec3{std::sin(0.5), -std::cos(0.5), 0.0} * length;
        room.ball(PlaygroundRoom::kRopeBall).previous = room.ball(PlaygroundRoom::kRopeBall).shown =
            room.ball(PlaygroundRoom::kRopeBall).position;
        room.ball(PlaygroundRoom::kRopeBall).velocity = {};
        double theta = 0.5, omega = 0.0;
        const double step = (1.0 / 20.0) / 5000.0;
        const double pull = 9.8 / length;
        const auto slope = [&](double th, double om, double& dth, double& dom) { dth = om; dom = -pull * std::sin(th); };
        double worst = 0.0;
        for (int tick = 1; tick <= 160; ++tick) {
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
            const Vec3 arm = room.ropeBall().position - room.ropeAnchor();
            worst = std::max(worst, std::abs(std::atan2(arm.x, -arm.y) - theta));
        }
        std::snprintf(detail, sizeof detail,
                      "8 s of a swing let go from 0.5 rad: the room's bob is at %.9f rad, the same equation "
                      "integrated finely is at %.9f rad -- they never differ by more than %.3e rad (log2 %.1f)",
                      std::atan2((room.ropeBall().position - room.ropeAnchor()).x, -(room.ropeBall().position - room.ropeAnchor()).y),
                      theta, worst, worst > 0.0 ? std::log2(worst) : -1000.0);
        check(worst < 5e-4, "a swing keeps the pendulum's own time", detail);
    }

    // 4. A swing spends and never borrows. The room's air is real, so the swing decays;
    //    what it may never do is come back higher than it started, or creep up over
    //    time while it is decaying.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        const double length = room.ropeLength();
        room.ball(PlaygroundRoom::kRopeBall).position = room.ropeAnchor() + Vec3{std::sin(0.9), -std::cos(0.9), 0.0} * length;
        room.ball(PlaygroundRoom::kRopeBall).previous = room.ball(PlaygroundRoom::kRopeBall).shown =
            room.ball(PlaygroundRoom::kRopeBall).position;
        room.ball(PlaygroundRoom::kRopeBall).velocity = {};
        const double start = bobEnergy(room);
        double highest = start, highestLate = start;
        for (int tick = 0; tick < 1200; ++tick) {
            room.advance(1.0 / 20.0);
            const double now = bobEnergy(room);
            highest = std::max(highest, now);
            if (tick >= 600) highestLate = std::max(highestLate, now);
        }
        const double rise = highest - start;
        std::snprintf(detail, sizeof detail,
                      "60 s of swinging from 0.9 rad: the bob started at %.9f J, never rose more than %.3e J above "
                      "it (log2 %.1f, a %.1e part of itself), and in its second half never got back up to %.9f J. "
                      "It ended at %.6f J",
                      start, rise, rise > 0.0 ? std::log2(rise) : -1000.0, rise / start, start, bobEnergy(room));
        check(rise <= 1e-6 * start && highestLate <= start, "a swing never makes energy", detail);
    }

    // 5. The ball takes and gives momentum with it, and nothing else takes any.
    //
    //    Two things are being asked here and they are not the same question. The first
    //    is that the hanging ball is a solid the thrown ball collides with -- not a
    //    ghost, not a thing the rope drags out of the way: a blow of five metres a
    //    second has to move it most of the way to five metres a second, and the ball
    //    that dealt it has to be the one that is left slow, because the two of them are
    //    the same mass and the same bounciness.
    //
    //    The second is the books: on a tick where the rope is not on that ball at all,
    //    the pair is a closed system along the blow, so the pair's own momentum may
    //    change by the air's share and no more. With the rope still on it the pair is
    //    not closed -- the rope is pulling sideways the moment the ball leaves the
    //    bottom of its arc, and that pull is a force of its own, in the ledger, paired
    //    with the anchor. So the books are checked where the books are closed, and the
    //    collision is checked where the rope is.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        throwAtBob(room, Vec3{5.0, 0.0, 0.0}, 0.42);
        double after = 0.0, bounceBack = 0.0, exchanged = 0.0;
        for (int tick = 0; tick < 200; ++tick) {
            const Vec3 ballWas = room.ballVelocity();
            room.advance(1.0 / 20.0);
            if (std::abs(room.ballVelocity().x - ballWas.x) < 0.5) continue;
            after = room.ropeBall().velocity.x;
            bounceBack = room.ballVelocity().x;
            exchanged = std::abs(after * room.ropeBall().mass);
        }
        std::snprintf(detail, sizeof detail,
                      "a ball at 5.0 m/s into the ball hanging on the rope: the hanging ball came away at %.3f m/s "
                      "and the ball that hit it was left at %.3f m/s, so %.3f kg m/s moved across the pair",
                      after, bounceBack, exchanged);
        check(exchanged > 2.5 && std::abs(after) > 2.5 && std::abs(bounceBack) < 0.5,
              "the ball takes and gives momentum with the ball on the rope", detail);
    }
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        room.rope_.attached = false;  // the same two balls, with nothing else on them
        throwAtBob(room, Vec3{5.0, 0.0, 0.0}, 0.42);
        double residual = 0.0, air = 0.0, exchanged = 0.0;
        for (int tick = 0; tick < 200; ++tick) {
            const Vec3 ballWas = room.ballVelocity();
            const Vec3 bobWas = room.ropeBall().velocity;
            const Vec3 pairWas =
                ballWas * room.ball(PlaygroundRoom::kThrowBall).mass + bobWas * room.ropeBall().mass;
            room.advance(1.0 / 20.0);
            if (std::abs(room.ballVelocity().x - ballWas.x) < 0.5) continue;
            const Vec3 pairNow = room.ballVelocity() * room.ball(PlaygroundRoom::kThrowBall).mass +
                                 room.ropeBall().velocity * room.ropeBall().mass;
            residual = std::abs((pairNow - pairWas).x);
            exchanged = std::abs((room.ropeBall().velocity - bobWas).x * room.ropeBall().mass);
            // The air's own pull over that one tick, from the drag law the room uses, on
            // both balls: neither of them can carry its momentum through still air.
            const double k = 0.5 * gtc::native::ball_shape::kAirDensity *
                             gtc::native::ball_shape::kBallDragCoefficient * gtc::native::ball_shape::kPi;
            const double r1 = room.ball(PlaygroundRoom::kThrowBall).radius;
            const double r2 = room.ropeBall().radius;
            air = std::abs((k * r1 * r1 * ballWas.x * ballWas.x + k * r2 * r2 * bobWas.x * bobWas.x) *
                           (1.0 / 20.0));
        }
        std::snprintf(detail, sizeof detail,
                      "and with the rope off it, nothing else takes any: the blow moved %.3f kg m/s across the "
                      "pair, and the pair's own total moved by %.3e kg m/s -- no more than the %.3e kg m/s the "
                      "air takes in that tick",
                      exchanged, residual, air);
        check(exchanged > 2.5 && residual <= 1.05 * air, "the pair's books balance: only the air takes any",
              detail);
    }

    // 6. The rope has no collision. A ball thrown through the line of the rope must be
    //    exactly where a ball thrown alongside it is, to the last bit: if the rope were
    //    a solid it would deflect one of them, and no two flight paths agree after that.
    {
        const auto flight = [](bool throughTheRope, Vec3& endsAt, double& slowest, bool& crossed) {
            PlaygroundRoom room;
            room.reset();
            settle(room, 200);
            const Vec3 anchor = room.ropeAnchor();
            // The rope hangs from the anchor down to the bob at 1.10 m: this is a third
            // of the way down it, well clear of the bob itself.
            room.ball(PlaygroundRoom::kThrowBall).position = Vec3{anchor.x + (throughTheRope ? -0.30 : 0.30), anchor.y - 0.45, 0.0};
            room.ball(PlaygroundRoom::kThrowBall).previous = room.ball(PlaygroundRoom::kThrowBall).shown = room.ball(PlaygroundRoom::kThrowBall).position;
            room.ball(PlaygroundRoom::kThrowBall).velocity = Vec3{6.0, 0.0, 0.0};
            room.ball(PlaygroundRoom::kThrowBall).spin = 0.0;
            room.ball(PlaygroundRoom::kThrowBall).frozen = false;
            slowest = 1e9;
            crossed = false;
            for (int tick = 0; tick < 3; ++tick) {
                room.advance(1.0 / 20.0);
                slowest = std::min(slowest, room.ballVelocity().x);
                if (throughTheRope && room.ballPosition().x > anchor.x) crossed = true;
            }
            endsAt = room.ballVelocity();
        };
        Vec3 through{}, beside{};
        double throughSlowest = 0.0, besideSlowest = 0.0;
        bool crossed = false, besideCrossed = false;
        flight(true, through, throughSlowest, crossed);
        flight(false, beside, besideSlowest, besideCrossed);
        const double difference = gtc::native::length(through - beside);
        std::snprintf(detail, sizeof detail,
                      "6 m/s across the rope, a third of the way down it: it came out the other side (%s) and is at "
                      "(%.9f, %.9f), where the same ball flying 0.6 m alongside the rope is at (%.9f, %.9f) -- %.1e m/s "
                      "apart (log2 %.1f)",
                      crossed ? "yes" : "no", through.x, through.y, beside.x, beside.y, difference,
                      difference > 0.0 ? std::log2(difference) : -1000.0);
        check(crossed && difference == 0.0, "the rope has no collision: a ball thrown through it is not touched", detail);
    }

    return localFailures;
}
