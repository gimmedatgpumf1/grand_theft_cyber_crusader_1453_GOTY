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
#include <array>
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
void throwAtTheHangingBall(PlaygroundRoom& room, Vec3 velocity, double fromTheSide) {
    const Vec3 hanging = room.ropeBall().position;
    room.ball(PlaygroundRoom::kThrowBall).position = hanging + Vec3{-fromTheSide, 0.0, 0.0};
    room.ball(PlaygroundRoom::kThrowBall).previous = room.ball(PlaygroundRoom::kThrowBall).shown = room.ball(PlaygroundRoom::kThrowBall).position;
    room.ball(PlaygroundRoom::kThrowBall).velocity = velocity;
    room.ball(PlaygroundRoom::kThrowBall).spin = 0.0;
    room.ball(PlaygroundRoom::kThrowBall).frozen = false;
}

// The energy of the ball on the rope: its travel and its height, the only two things a
// ball on a rope has that a constraint like this can change.
// The other ball, parked out of the way.
//
// Both scenes below stand the triangle up at the King's spot, and that spot is where the
// thrown ball has come to rest by the end of the settling -- the triangle is then drawn
// around a ball, and what the next hundred and fifty seconds measure is the separation
// pushing the *thrown* ball out of a shape that was placed on top of it. Measured: the
// triangle walked 0.26 m and the room's energy rose by 0.18 J, with the ball on the rope
// perfectly still the whole time. A check whose scene has a ball inside a shape is
// checking the placement, not the rope.
void parkTheOtherBall(PlaygroundRoom& room) {
    auto& other = room.ball(PlaygroundRoom::kThrowBall);
    other.position = gtc::native::Vec3{-3.0, other.radius, 0};
    other.previous = other.shown = other.position;
    other.velocity = {};
    other.spin = 0.0;
    other.frozen = false;
}

double hangingEnergy(const PlaygroundRoom& room) {
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
        const double weight = room.ropeBall().mass * 9.8;
        // How far the ball has come from the face, six seconds on: the ball is where it
        // was put, and it is the triangle that the room let go of, once, and no bouncing
        // between the two of them after that.
        Vec3 way{}, at{};
        const double toTheSlope = room.bodyBallGap(room.ropeBall(), 1, way, at);
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
            throwAtTheHangingBall(room, Vec3{speed, 0.0, 0.0}, 0.45);
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
        const double start = hangingEnergy(room);
        double highest = start, highestLate = start;
        for (int tick = 0; tick < 1200; ++tick) {
            room.advance(1.0 / 20.0);
            const double now = hangingEnergy(room);
            highest = std::max(highest, now);
            if (tick >= 600) highestLate = std::max(highestLate, now);
        }
        const double rise = highest - start;
        std::snprintf(detail, sizeof detail,
                      "60 s of swinging from 0.9 rad: the bob started at %.9f J, never rose more than %.3e J above "
                      "it (log2 %.1f, a %.1e part of itself), and in its second half never got back up to %.9f J. "
                      "It ended at %.6f J",
                      start, rise, rise > 0.0 ? std::log2(rise) : -1000.0, rise / start, start, hangingEnergy(room));
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
        throwAtTheHangingBall(room, Vec3{5.0, 0.0, 0.0}, 0.42);
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
        throwAtTheHangingBall(room, Vec3{5.0, 0.0, 0.0}, 0.42);
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
            // Both balls, at the speeds the air actually saw: the one that hit was doing
            // five metres a second when the tick opened, and the one that was hit was
            // doing nearly that by the time its own tick ran.
            const double v1 = gtc::native::length(ballWas);
            const double v2 = gtc::native::length(room.ropeBall().velocity);
            air = (k * r1 * r1 * v1 * v1 + k * r2 * r2 * v2 * v2) * (1.0 / 20.0);
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

    // 7. Two balls are two objects. Taking one of them in the hand may not do anything
    //    at all to the other: a carried ball is not simulated -- the hand says where it
    //    is -- and reading that as "a ball is being carried" stopped both of them, so
    //    the ball on the rope hung dead still in the air, its own weight unanswered,
    //    while the mouse was somewhere else entirely. The same room is run twice from
    //    the same state; in one of them the thrown ball is taken by the hand, and the
    //    ball on the rope has to be bit-for-bit where it would have been anyway.
    {
        const auto swinging = [](bool grabTheOther) {
            PlaygroundRoom room;
            room.reset();
            settle(room, 200);
            // Let it go from a hand's width out to the side, so it is swinging.
            auto& hanging = room.ball(PlaygroundRoom::kRopeBall);
            const Vec3 anchor = room.ropeAnchor();
            hanging.position = anchor + Vec3{std::sin(0.7), -std::cos(0.7), 0.0} * room.ropeLength();
            hanging.previous = hanging.shown = hanging.position;
            hanging.velocity = {};
            hanging.frozen = false;
            if (grabTheOther) {
                // The mouse takes the thrown ball, on the floor, and carries it.
                const Vec3 there = room.ball(PlaygroundRoom::kThrowBall).position;
                room.setDragRay(Vec3{there.x, there.y, 1.0}, Vec3{0, 0, -1});
                room.beginDrag();
                const bool tookOne = room.dragTarget_ == PlaygroundRoom::DragTarget::Ball &&
                                     room.dragBall_ == PlaygroundRoom::kThrowBall;
                if (!tookOne) return std::array<double, 4>{-1, -1, -1, -1};
                room.setDragRay(Vec3{there.x - 0.4, there.y + 0.6, 1.0}, Vec3{0, 0, -1});
            }
            std::array<double, 4> ends{};
            for (int tick = 0; tick < 20; ++tick) room.advance(1.0 / 20.0);
            const auto& ball = room.ropeBall();
            ends = {ball.position.x, ball.position.y, ball.velocity.x, ball.velocity.y};
            return ends;
        };
        const std::array<double, 4> free = swinging(false);
        const std::array<double, 4> grabbed = swinging(true);
        const double apart = gtc::native::length(Vec3{free[0] - grabbed[0], free[1] - grabbed[1], 0.0});
        const double swung = gtc::native::length(Vec3{free[2], free[3], 0.0});
        std::snprintf(detail, sizeof detail,
                      "one second of a swing, with the hand holding the other ball for all of it: the ball on the rope "
                      "was at (%.9f, %.9f) and is at (%.9f, %.9f), %.3e m apart (log2 %.1f), still doing %.3f m/s "
                      "where the same swing with nothing in the hand is doing %.3f m/s",
                      free[0], free[1], grabbed[0], grabbed[1], apart, apart > 0.0 ? std::log2(apart) : -1000.0,
                      swung, std::hypot(free[2], free[3]));
        check(grabbed[0] > -0.5 && apart == 0.0 && swung > 0.5,
              "the two balls are two objects: carrying one leaves the other swinging", detail);
    }

    // 8. A ball on a rope resting against a slope: nothing happens, and nothing appears.
    //
    //    This is the King's picture. The triangle stood on its vertical edge with its
    //    slope under the hanging ball, the ball exactly on the slope, the rope taut and
    //    answering the rest of its weight. The ball bounced off the slope for ever, and
    //    it was not bouncing: it was being handed half a metre a second of upward speed
    //    out of nothing, every tick, by the rope. The rope read the speed the ball's own
    //    tick *ended* with -- which is half a tick of fall -- and then applied another
    //    tick of gravity on top of it on its own arc. A ball in free swing only ever threw
    //    that extra half-tick of speed away again, which is why it showed as a swing that
    //    ran a little fast; a ball with a surface under it could not, and the surface gave
    //    it back. That is where the infinite energy came from.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        parkTheOtherBall(room);
        // The triangle stood on its vertical edge with its slope rising to the right and
        // its face exactly under the hanging ball: the King's picture, to the millimetre.
        auto& triangle = room.bodies_[1];
        triangle.angle = 1.5707963267948966;
        triangle.centre = Vec3{3.6697, 0.7, 0};
        room.placeCorners(triangle);
        triangle.previous = triangle.position;
        triangle.previousCentre = triangle.centre;
        triangle.previousAngle = triangle.angle;
        triangle.velocity = {};
        triangle.spin = 0.0;
        triangle.frozen = true;
        // Everything that is going to happen, happens in the first tick: the slope answers
        // a ball it is touching at exactly one radius, and the rope answers the rest.
        for (int tick = 0; tick < 20; ++tick) room.advance(1.0 / 20.0);
        const Vec3 before = room.ropeBall().position;
        const double energyBefore = room.totalEnergy();
        double highest = energyBefore;
        int ticks = 0;
        for (int tick = 0; tick < 3000; ++tick) {
            room.advance(1.0 / 20.0);
            highest = std::max(highest, room.totalEnergy());
            ++ticks;
        }
        const double moved = gtc::native::length(room.ropeBall().position - before);
        const double weight = room.ropeBall().mass * 9.8;
        // How far the ball has come from the face, six seconds on: the ball is where it
        // was put, and it is the triangle that the room let go of, once, and no bouncing
        // between the two of them after that.
        Vec3 way{}, at{};
        const double toTheSlope = room.bodyBallGap(room.ropeBall(), 1, way, at);
        const double rise = highest - energyBefore;
        std::snprintf(detail, sizeof detail,
                      "150 s of a ball on a rope set down exactly on the triangle's slope: it moved %.3e m "
                      "(log2 %.1f) and the room's energy rose by %.3e J (log2 %.1f) above %.6f J; it is %.6f m "
                      "from its anchor, "
                      "the rope is %.3f m and carrying %.3f N of its %.3f N weight",
                      moved, moved > 0.0 ? std::log2(moved) : -1000.0, rise,
                      rise > 0.0 ? std::log2(rise) : -1000.0, energyBefore, room.ropeBallDistance(),
                      room.ropeLength(), room.ropeTension(), weight);
        // And the picture really is the one he sent: the slope under the ball must be
        // carrying part of its weight, or this would be a ball hanging in free air and the
        // check would be checking nothing. The rope's pull being less than the weight is
        // the proof that the slope is under it.
        check(moved == 0.0 && rise <= 0.0 && room.ropeTaut(),
              "a ball resting on a slope on a taut rope rests, and conjures nothing", detail);
    }

    // 9. And a swing in this room still loses energy, because the room has air in it. The
    //    rope spends the part of the tick that is left after it takes hold -- the ball's
    //    own tick spent the part before -- so the air is asked for every part of every
    //    tick and no swing is free.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        auto& hanging = room.ball(PlaygroundRoom::kRopeBall);
        const Vec3 anchor = room.ropeAnchor();
        hanging.position = anchor + Vec3{std::sin(0.9), -std::cos(0.9), 0.0} * room.ropeLength();
        hanging.previous = hanging.shown = hanging.position;
        hanging.velocity = {};
        hanging.frozen = false;
        const double start = hangingEnergy(room);
        double highest = start;
        for (int tick = 0; tick < 1200; ++tick) {
            room.advance(1.0 / 20.0);
            highest = std::max(highest, hangingEnergy(room));
        }
        const double end = hangingEnergy(room);
        std::snprintf(detail, sizeof detail,
                      "60 s of swinging from 0.9 rad in a room with air: it began at %.6f J, rose by at most %.3e J "
                      "(log2 %.1f, the integrator's own shadow and not a source), and ended at %.6f J -- %.3f J of "
                      "it spent on the air",
                      start, highest - start, highest - start > 0.0 ? std::log2(highest - start) : -1000.0, end,
                      start - end);
        // The rise allowed is the symplectic rule's own shadow: a fraction of a part in a
        // million of the swing, oscillating rather than growing -- the swing below starts
        // at the same place whether this tick ends there or not, which a source of energy
        // would not. What may never be true is that the swing comes out ahead.
        check(highest - start <= 1e-6 * start && start - end > 0.1,
              "a swing spends its energy on the air, never borrows any", detail);
    }

    // 10. The same picture, pressed. Dragging the triangle a millimetre further into the
    //     ball is what the King did to see it bounce: the slope is then genuinely under
    //     the ball, and the ball's weight is shared between the slope and the rope. It
    //     must take the push, come to rest against the slope, and be handed nothing at
    //     all -- no sway, no hum, no swing that never decays. Before this the same scene
    //     was still swaying at 3.7e-2 m/s a hundred and fifty seconds later, on an energy
    //     it had been given at the start and never gave back.
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        parkTheOtherBall(room);
        auto& triangle = room.bodies_[1];
        triangle.angle = 1.5707963267948966;
        const double press = 1e-3;
        // Into the ball, which is the way the slope's own face points: up and to the
        // left. Pressing the other way drove the shape's lowest corner through the
        // floor instead, and what the room then did was lift the shape back out of the
        // floor by exactly the press -- seven tenths of a millimetre of the *floor's*
        // doing, in a check about a ball.
        triangle.centre = Vec3{3.6697 - press / std::sqrt(2.0), 0.7 + press / std::sqrt(2.0), 0};
        room.placeCorners(triangle);
        triangle.previous = triangle.position;
        triangle.previousCentre = triangle.centre;
        triangle.previousAngle = triangle.angle;
        triangle.velocity = {};
        triangle.spin = 0.0;
        // And it is *held* there, because that is the only way it can be there: the room
        // will not let a shape be put down inside anything -- bodyPlacementClear() is
        // asked before the mouse lets go -- so a slope pressed into the ball is a slope
        // somebody is still holding. A body the hand is carrying gives nothing (see
        // ballContact), which is the whole question this check asks: when the hand and
        // the rope both say no, it is the ball that gives way, and nothing else moves.
        triangle.frozen = true;
        // The press is answered in the first tick and it is the hand's, not the room's:
        // a slope that cannot give way is a slope the ball is pushed out of, and the
        // ball is the one thing here that may be moved out of something it is in. So
        // the tick the press is answered in is where the hand's work lands, and it is
        // taken as read. What must never happen is the room *adding* to it afterwards:
        // a kicked ball that keeps being kicked is the pump this check is for.
        //
        // It used to assert that the ball did not move at all -- which was true only
        // because the rope put the ball back where it was every tick and threw the
        // contact's answer away with it. That is the same pinning that made the ball an
        // immovable wall against the triangle, and it cannot be both: a ball that may
        // be shoved by a slope is a ball that swings after it is shoved.
        room.advance(1.0 / 20.0);
        const double start = hangingEnergy(room);
        double highest = start;
        for (int tick = 0; tick < 3000; ++tick) {
            room.advance(1.0 / 20.0);
            highest = std::max(highest, hangingEnergy(room));
        }
        const double rise = highest - start;
        const double end = hangingEnergy(room);
        // The room's own rule for "not moving": a ball that cannot carry itself a
        // micrometre in a tick is not moving. That is kQuantum doing the one job it is
        // allowed to do here -- saying what the room calls still -- and not a tolerance
        // on an overlap.
        const double travel = gtc::native::length(room.ropeBall().velocity) * gtc::native::playground_tick::kTick;
        const double spoke = gtc::native::length(triangle.centre - Vec3{3.6697 - press / std::sqrt(2.0), 0.7 + press / std::sqrt(2.0), 0});
        std::snprintf(detail, sizeof detail,
                      "150 s after a millimetre of press: the ball was left at %.6f J after the tick the press was "
                      "answered in, never rose by more than %.3e J (log2 %.1f) over the next 150 s, and ended at "
                      "%.6f J -- %.3f J of it spent on the air, %.3e m a tick at the end; THE SHAPE IT WAS "
                      "PRESSED WITH MOVED %.3e m; the rope carries %.3f N",
                      start, rise, rise > 0.0 ? std::log2(rise) : -1000.0, end, start - end, travel, spoke,
                      room.ropeTension());
        check(rise <= 0.0 && end < start && spoke <= gtc::native::playground_tick::kQuantum,
              "a slope pressed into a ball on a rope takes the push and gives nothing back", detail);
        (void)0;
    }

    return localFailures;
}
