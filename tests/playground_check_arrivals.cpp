// The room's answer to the fastest arrival it can be asked about.
//
// A hundred kilometres a second is not a special case in the geometry: every meeting
// in the room is solved for, not sampled, so there is no step that can be too coarse
// to catch it. What a speed that high can still break is the arithmetic. Drag is a
// force, and a force bigger than the momentum it acts on, applied for a whole tick,
// turns the ball round and drives it further back than it came -- which is how a ball
// used to end up inside a shape at that speed. It was thrown there, not let through.
// So this asks the two things together: the ball is never inside a shape, and no
// number in the room ever stops being a number.
//
// The rest of the physics checks are in playground_check.cpp, which calls this and
// adds what it finds to the same count.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#define private public
#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#undef private

namespace {

using namespace gtc::native;

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

void settle(gtc::native::PlaygroundRoom& room, int ticks) {
    for (int tick = 0; tick < ticks; ++tick) room.advance(1.0 / 20.0);
}

// Recreate the user's picture: the rope is almost horizontal to the left, and
// the triangle stands on its long bottom edge, with its vertical side just left
// of the anchor. The ball is placed by the same mouse ray used by the client.
void horizontalRopeImpact(int fps, bool awake, double sideOffset) {
    using gtc::native::PlaygroundRoom;
    using gtc::native::Vec3;
    using gtc::native::dot;
    using gtc::native::length;

    PlaygroundRoom room;
    room.reset();
    settle(room, 200);
    auto& other = room.ball(PlaygroundRoom::kThrowBall);
    other.position = other.previous = other.shown = Vec3{-3.0, other.radius, 0.0};
    other.velocity = {};
    other.spin = 0.0;
    other.frozen = false;

    const Vec3 anchor = room.ropeAnchor();
    auto& triangle = room.bodies_[PlaygroundRoom::kTriangle];
    triangle.centre = triangle.previousCentre = Vec3{anchor.x - sideOffset + 0.7, 0.7, 0.0};
    triangle.angle = triangle.previousAngle = 0.0;
    triangle.velocity = {};
    triangle.spin = 0.0;
    triangle.frozen = !awake;
    room.placeCorners(triangle);
    triangle.previous = triangle.shown = triangle.position;
    const auto startingCorners = triangle.position;
    const Vec3 startingCentre = triangle.centre;

    auto& ball = room.ball(PlaygroundRoom::kRopeBall);
    const double rope = room.ropeLength();
    const auto placeAt = [&](double angle) {
        return anchor + Vec3{std::sin(angle), -std::cos(angle), 0.0} * rope;
    };
    const Vec3 first = placeAt(-1.44);
    const Vec3 release = placeAt(-1.54);
    ball.position = ball.previous = ball.shown = first;
    ball.velocity = {};
    ball.spin = 0.0;
    ball.frozen = false;
    ball.touching = false;
    room.rope_.taut = true;
    room.setDragRay(first + Vec3{0, 0, 5}, Vec3{0, 0, -1});
    room.beginDrag();
    const bool grabbed = room.dragTarget_ == PlaygroundRoom::DragTarget::Ball &&
                         room.dragBall_ == PlaygroundRoom::kRopeBall;
    room.setDragRay(release + Vec3{0, 0, 5}, Vec3{0, 0, -1});
    room.advance(1.0 / fps);
    room.endDrag();
    const double releaseError = length(ball.position - release);
    const double releaseSpeed = length(ball.velocity);
    const double releaseSpin = std::abs(ball.spin);

    bool hit = false;
    int tick = 0, impactTick = -1;
    double incoming = 0.0, outgoing = 0.0, impactBallX = 0.0, lowestX = 0.0, separation = 0.0;
    double triangleMove = 0.0, maxGain = 0.0, overlap = 0.0, stretch = 0.0;
    int gainTick = 0;
    const bool trace = std::getenv("GTC_TRACE_HORIZONTAL") != nullptr && fps == 60 && !awake;
    for (int frame = 0; frame < fps * 4; ++frame) {
        Vec3 way{}, at{};
        const double beforeGap = room.bodyBallGap(ball, PlaygroundRoom::kTriangle, way, at);
        const Vec3 normal = way * -1.0;
        const double beforeRelative = dot(ball.velocity - room.pointSpeed(triangle, at), normal);
        const double beforeEnergy = room.totalEnergy();
        const bool stepped = std::min(room.accumulator_ + 1.0 / fps, 0.25) >= 1.0 / 20.0;
        room.advance(1.0 / fps);
        if (!stepped) continue;
        ++tick;
        const double gap = room.bodyBallGap(ball, PlaygroundRoom::kTriangle, way, at);
        const double relative = dot(ball.velocity - room.pointSpeed(triangle, at), way * -1.0);
        const double gain = room.totalEnergy() - beforeEnergy;
        if (gain > maxGain) { maxGain = gain; gainTick = tick; }
        overlap = std::max(overlap, -gap);
        stretch = std::max(stretch, room.ropeBallDistance() - rope);
        bool impulse = ball.meetingArrival[PlaygroundRoom::kTriangle] > 1e-7;
        for (std::size_t i = 0; i < room.forceCount(); ++i) {
            const auto& force = room.forces()[i];
            if (force.owner == PlaygroundRoom::kTriangle && force.kind == PlaygroundRoom::ForceKind::Ball &&
                length(force.force) > 1e-4)
                impulse = true;
        }
        if (!hit && impulse && beforeRelative < -0.1 && beforeGap < 0.3) {
            hit = true;
            impactTick = tick;
            incoming = beforeRelative;
            outgoing = dot(ball.velocity - room.pointSpeed(triangle, at), normal);
            impactBallX = ball.velocity.x;
            if (trace) {
                std::printf("   impact: began (%+.4f,%+.4f), alone (%+.4f,%+.4f), "
                            "blows (%+.4f,%+.4f) N s, bend %.4f at (%+.4f,%+.4f), "
                            "meeting %.4f / %.4f N s, spin %+.4f rad/s, impacted %d, "
                            "answered %d, bent %d, before gap %.7f, offset %.2f\n",
                            ball.began.x, ball.began.y, ball.alone.x, ball.alone.y,
                            ball.blows.x, ball.blows.y, ball.bendAt, ball.bendPoint.x, ball.bendPoint.y,
                            ball.meetingGiven[PlaygroundRoom::kTriangle],
                            ball.meetingArrival[PlaygroundRoom::kTriangle], ball.spin,
                            int(ball.impacted), int(ball.answered), int(ball.bent), beforeGap, sideOffset);
            }
        }
        if (hit) {
            lowestX = std::min(lowestX, ball.velocity.x);
            separation = std::max(separation, gap);
            triangleMove = std::max(triangleMove, length(triangle.centre - startingCentre));
            for (std::size_t i = 0; i < triangle.count; ++i)
                triangleMove = std::max(triangleMove, length(triangle.position[i] - startingCorners[i]));
        }
        if (trace && tick <= 50)
            std::printf("   tick %02d gap %+.6f rel %+.4f -> %+.4f ball (%+.3f,%+.3f) "
                        "triangle (%+.3f,%+.3f) spin %+.3f dE %+.7f impulse %d\n",
                        tick, gap, beforeRelative, relative, ball.velocity.x, ball.velocity.y,
                        triangle.velocity.x, triangle.velocity.y, triangle.spin, gain, int(impulse));
    }
    char detail[400];
    std::snprintf(detail, sizeof detail,
                  "%d Hz %s offset %.2f: hit %d at tick %d, normal %.3f -> %.3f m/s, "
                  "impact vx %.3f m/s, min vx %.3f, separation %.3f m, triangle moved %.4f m, "
                  "gain %.6f J at tick %d, overlap %.3g m, stretch %.3g m",
                  fps, awake ? "awake" : "asleep", sideOffset, int(hit), impactTick,
                  incoming, outgoing, impactBallX, lowestX, separation, triangleMove, maxGain, gainTick,
                  overlap, stretch);
    check(grabbed && releaseError < 1e-6 && releaseSpeed < 1e-9 && releaseSpin < 1e-9,
          "a mouse release places the rope ball without throwing it", detail);
    check(hit && incoming < -0.5 && outgoing > 0.2 && impactBallX < -0.2 && separation > 0.1,
          "the horizontal rope swing rebounds visibly from the triangle", detail);
    check(hit && triangleMove > 0.001,
          "the triangle reacts at its centre or corners", detail);
    check(maxGain < 1e-4 && overlap < 1.1e-6 && stretch < 1.1e-6,
          "the swing does not gain energy, penetrate the triangle or stretch the rope", detail);
}

// A rolling ball reaches the cube's side while both are supported by the floor.
// Measure motion, not just the restitution bookkeeping: a recorded bounce is not
// useful if the subsequent solve leaves the ball moving into the cube.
void floorBallHitsCube(double speed, bool awake, double initialGap) {
    using gtc::native::PlaygroundRoom;
    using gtc::native::Vec3;
    using gtc::native::dot;
    using gtc::native::length;

    PlaygroundRoom room;
    room.reset();
    settle(room, 200);
    auto& cube = room.bodies_[PlaygroundRoom::kCube];
    cube.centre = cube.previousCentre = Vec3{1.2, 0.35, 0};
    cube.angle = cube.previousAngle = 0.0;
    cube.velocity = {};
    cube.spin = 0.0;
    cube.frozen = !awake;
    room.placeCorners(cube);
    cube.previous = cube.shown = cube.position;
    const Vec3 start = cube.centre;
    const auto corners = cube.position;
    auto& ball = room.ball(PlaygroundRoom::kThrowBall);
    ball.position = ball.previous = ball.shown = Vec3{1.2 - 0.35 - ball.radius - initialGap, ball.radius, 0};
    ball.velocity = Vec3{speed, 0, 0};
    ball.spin = -speed / ball.radius;
    ball.frozen = false;
    ball.touching = ball.standing = true;
    ball.supportNormal = Vec3{0, 1, 0};

    bool hit = false;
    double incoming = 0.0, outgoing = 0.0, impactX = 0.0, maxGap = 0.0;
    double movement = 0.0, penetration = 0.0, maxGain = 0.0;
    int impactTick = -1;
    for (int tick = 1; tick <= 60; ++tick) {
        Vec3 way{}, at{};
        const double beforeGap = room.bodyBallGap(ball, PlaygroundRoom::kCube, way, at);
        const Vec3 normal = way * -1.0;
        const double beforeRelative = dot(ball.velocity - room.pointSpeed(cube, at), normal);
        const double energy = room.totalEnergy();
        room.advance(1.0 / 20.0);
        const double gap = room.bodyBallGap(ball, PlaygroundRoom::kCube, way, at);
        maxGain = std::max(maxGain, room.totalEnergy() - energy);
        penetration = std::max(penetration, -gap);
        bool impulse = ball.meetingArrival[PlaygroundRoom::kCube] > 1e-7;
        for (std::size_t i = 0; i < room.forceCount(); ++i) {
            const auto& force = room.forces()[i];
            if (force.owner == PlaygroundRoom::kCube && force.kind == PlaygroundRoom::ForceKind::Ball &&
                length(force.force) > 1e-4) impulse = true;
        }
        if (!hit && impulse && beforeRelative < -0.1 && beforeGap < 0.3) {
            hit = true;
            impactTick = tick;
            incoming = beforeRelative;
            outgoing = dot(ball.velocity - room.pointSpeed(cube, at), normal);
            impactX = ball.velocity.x;
        }
        if (hit) {
            maxGap = std::max(maxGap, gap);
            movement = std::max(movement, length(cube.centre - start));
            for (std::size_t i = 0; i < cube.count; ++i)
                movement = std::max(movement, length(cube.position[i] - corners[i]));
        }
    }
    char detail[320];
    std::snprintf(detail, sizeof detail,
                  "%.1f m/s %s gap %.2f m: hit %d at tick %d, normal %.3f -> %.3f m/s, "
                  "ball vx %.3f m/s, separation %.4f m, cube moved %.4f m, overlap %.3g m, gain %.6f J",
                  speed, awake ? "awake" : "asleep", initialGap, int(hit), impactTick,
                  incoming, outgoing, impactX, maxGap, movement, penetration, maxGain);
    check(hit && outgoing > 0.1 && impactX < -0.1 && maxGap > 0.03,
          "a rolling ball rebounds and separates from the cube", detail);
    // A light hit may be absorbed by static friction at the cube's feet. A hard
    // hit should move the cube; neither case may put the ball through it.
    check(hit && (speed < 3.0 || movement > 0.001) && penetration < 1.1e-6,
          "the floor-supported cube does not let the ball through", detail);
}

void ordinaryReleaseDoesNotThrow() {
    using gtc::native::PlaygroundRoom;
    using gtc::native::Vec3;
    using gtc::native::length;
    PlaygroundRoom room;
    room.reset();
    settle(room, 200);
    auto& ball = room.ball(PlaygroundRoom::kThrowBall);
    const Vec3 from{2.0, 2.6, 0}, to{2.3, 2.8, 0};
    ball.position = ball.previous = ball.shown = from;
    ball.velocity = {};
    ball.frozen = false;
    room.setDragRay(from + Vec3{0, 0, 5}, Vec3{0, 0, -1});
    room.beginDrag();
    const bool grabbed = room.dragTarget_ == PlaygroundRoom::DragTarget::Ball &&
                         room.dragBall_ == PlaygroundRoom::kThrowBall;
    room.setDragRay(to + Vec3{0, 0, 5}, Vec3{0, 0, -1});
    room.endDrag();  // release before another rendered frame
    const double error = length(ball.position - to), speed = length(ball.velocity);
    room.advance(1.0 / 20.0);
    char detail[180];
    std::snprintf(detail, sizeof detail, "release error %.3g m, speed %.3g m/s, next velocity (%+.3f,%+.3f)",
                  error, speed, ball.velocity.x, ball.velocity.y);
    check(grabbed && error < 1e-6 && speed < 1e-9 && std::abs(ball.velocity.x) < 1e-6 && ball.velocity.y < 0.0,
          "releasing the ordinary ball places it without throwing it", detail);
}

// A bounce is one number, and it must be the same number whichever way the surface
// happens to point. It is a ratio of forces and it is measured as one: the impulse the
// meeting dealt the ball, over the impulse that would just have stopped it, both in
// newton seconds and both read at the instant of the meeting. A cube the ball can move
// takes part of the blow with it, and that is conservation of momentum, not a softer
// bounce.
//
// Read across a tick instead -- the ball's speed after it over its speed before it --
// the same room reads 0.81 off the floor and 0.75 off a wall, and neither of those is
// the coefficient: a tick of gravity is half a metre a second of momentum that never
// touched either surface, and where in the tick the meeting fell decides how much of
// it lands in the ratio. That is not a difference in the room, it is a difference in
// the arithmetic.
//
// And a blow below the middle does not make the bounce smaller. The coefficient is a
// property of the pair of materials; where the blow lands decides how much of the
// impulse goes into turning the cube, not how much of the approach comes back. So the
// off-centre hit is checked for what it should do -- turn the cube -- and for still
// coming back at the same coefficient, which is what a room with one law in it does.
void bounceIsTheSameEveryWay() {
    say("\nbounciness is not only for vertical hits\n");
    PlaygroundRoom room;
    room.reset();
    settle(room, 300);
    const Vec3 cube = room.bodyMiddle(0);
    // What a surface gave back, as a force: the impulse the meeting dealt the ball
    // over the impulse that would just have stopped it, less one. Both numbers are
    // newton seconds read at the instant of the meeting -- not a speed divided by a
    // speed across a tick, in which gravity alone is half a metre a second of
    // momentum that never touched the surface. A meeting that only stops the ball
    // gives back nothing and reads nought; a meeting that gives back e of the arrival
    // reads e, from any direction and at any point in the tick.
    const auto bounceOf = [](const PlaygroundRoom& room, std::size_t surface) {
        if (!(room.ball(PlaygroundRoom::kThrowBall).meetingArrival[surface] > 0.0)) return 0.0;
        return room.ball(PlaygroundRoom::kThrowBall).meetingGiven[surface] / room.ball(PlaygroundRoom::kThrowBall).meetingArrival[surface] - 1.0;
    };
    // Fire the ball at the flat middle of a face: a hand's width away, so that what
    // gravity takes out of its line on the way is nearly nothing.
    const auto hitTheSide = [&](double away, double& bounce) {
        bounce = 0.0;
        PlaygroundRoom into;
        into.reset();
        settle(into, 300);
        into.ball(PlaygroundRoom::kThrowBall).position = Vec3{cube.x - 0.35 - into.ball(PlaygroundRoom::kThrowBall).radius - away, cube.y, 0};
        into.ball(PlaygroundRoom::kThrowBall).velocity = Vec3{6.0, 0, 0};
        into.ball(PlaygroundRoom::kThrowBall).frozen = false;
        for (int tick = 0; tick < 40; ++tick) {
            into.advance(1.0 / 20.0);
            const double met = bounceOf(into, 0);
            if (met > 0.0) {
                bounce = met;
                return into.bodies_[0].spin;
            }
        }
        return 0.0;
    };
    double side = 0.0, floor = 0.0;
    hitTheSide(0.02, side);
    {
        PlaygroundRoom down;
        down.reset();
        settle(down, 300);
        down.ball(PlaygroundRoom::kThrowBall).position = Vec3{3.1, 2.0, 0};
        down.ball(PlaygroundRoom::kThrowBall).velocity = {};
        down.ball(PlaygroundRoom::kThrowBall).frozen = false;
        for (int tick = 0; tick < 40; ++tick) {
            down.advance(1.0 / 20.0);
            floor = bounceOf(down, PlaygroundRoom::kBodyCount);
            if (floor > 0.0) break;
        }
    }
    char detail[220];
    std::snprintf(detail, sizeof detail, "side restitution %.4f, floor %.4f", side, floor);
    check(side > 0.5 && floor > 0.5, "the ball leaves both a side and the floor", detail);
    check(std::abs(side - floor) < 0.01, "by exactly the same amount", detail);

    double lowSide = 0.0;
    const double spin = hitTheSide(1.2, lowSide);
    std::snprintf(detail, sizeof detail,
                  "hit a hand's width low: restitution %.4f (against %.4f on the middle), the cube turned at %.3f rad/s",
                  lowSide, side, spin);
    check(lowSide > 0.6 && std::abs(lowSide - side) < 0.01 && std::abs(spin) > 0.5,
          "a hit below the middle turns the cube, at the same bounciness", detail);
}

// A ball is not a point with a radius. It rolls, and rolling is a different set of
// laws from sliding: the place a rolling ball touches the ground is *not moving*, so
// the grip along the ground does no work on it and cannot slow it down at all. What
// slows a roll is the ground's own squash -- the pressure under the ball is heavier
// in front of it than behind -- which is a couple of size Crr * N * r and nothing
// else. It is a couple, so it takes the same rate out of a fast roll as a slow one:
// distance goes as the square of the speed, a metre a second dies in about two
// metres, and three metres a second covers over five in the first two seconds.
//
// That is what a basketball does, and it is measured in metres here because a
// coefficient is not something a player can see. It is checked because it was wrong:
// the couple was charged afresh on every round of the grip solve, so a tick spent
// eight ticks' worth of rolling resistance and a ball at three metres a second died
// in a metre and a half, at nearly two metres a second squared -- eight times
// Crr * g / 1.4. It read on screen as a ball rolling on sand.
void aRollIsARoll() {
    say("\na roll is a roll\n");
    // The ball rolled along the floor with its bottom standing still against it: the
    // speed and the turn agree, which is what a roll *is*. What is measured is how far
    // it gets, and whether it stays a roll all the way there.
    const auto rolled = [](double speed, int ticks, double& distance, double& slip, bool& asleep) {
        PlaygroundRoom room;
        room.reset();
        settle(room, 400);
        room.ball(PlaygroundRoom::kThrowBall).frozen = false;
        // Rolled away from everything else in the room, along the open floor: the
        // ball starts beside the cube, and a roll into the cube is a collision, not a
        // roll.
        room.ball(PlaygroundRoom::kThrowBall).velocity = Vec3{speed, 0, 0};
        room.ball(PlaygroundRoom::kThrowBall).spin = -speed / room.ball(PlaygroundRoom::kThrowBall).radius;
        const double from = room.ball(PlaygroundRoom::kThrowBall).position.x;
        slip = 0.0;
        for (int tick = 0; tick < ticks; ++tick) {
            room.advance(1.0 / 20.0);
            slip = std::max(slip, std::abs(room.ball(PlaygroundRoom::kThrowBall).velocity.x + room.ball(PlaygroundRoom::kThrowBall).spin * room.ball(PlaygroundRoom::kThrowBall).radius));
        }
        distance = std::abs(room.ball(PlaygroundRoom::kThrowBall).position.x - from);
        asleep = room.ball(PlaygroundRoom::kThrowBall).frozen;
    };
    double slowDistance = 0.0, slowSlip = 0.0, fastDistance = 0.0, fastSlip = 0.0;
    bool slowAsleep = false, fastAsleep = false;
    rolled(1.0, 600, slowDistance, slowSlip, slowAsleep);
    rolled(3.0, 40, fastDistance, fastSlip, fastAsleep);
    char detail[220];
    std::snprintf(detail, sizeof detail, "a roll at 1.0 m/s died after %.3f m%s, and at 3.0 m/s covered %.3f m in two seconds",
                  slowDistance, slowAsleep ? "" : " (never stopped)", fastDistance);
    check(slowDistance > 1.6 && slowDistance < 2.4 && slowAsleep && fastDistance > 4.5,
          "a roll dies at the rate rolling resistance says, not eight times it", detail);
    std::snprintf(detail, sizeof detail, "the bottom of the ball never moved against the floor, worst slip %.2e m/s, %.2e m/s",
                  slowSlip, fastSlip);
    check(slowSlip < 1e-3 && fastSlip < 1e-3, "and it rolled all the way, it never skidded", detail);
}

// What a surface under the ball decides: whether the ball stays where it was put.
//
// A surface holds a ball when the couple the ground can supply is enough to stop it
// rolling, which is the same Crr as above: a ball on a slope of angle a needs
// N * r * tan(a) of couple, so it stays when tan(a) <= Crr and rolls away when it
// does not. That is a little over two degrees, and it is the ball's own coefficient
// deciding it -- friction along the surface is a far weaker condition (mu_s 0.70,
// thirty-five degrees) and never the one that answers. Asked with the cone alone, a
// ball put down on a ten degree ramp sat on it for ever, which no basketball does.
//
// The first case is also the wriggle. A sleeping plate tilted by a twentieth of a
// degree had no contact written for it at all -- the solver only writes contacts for
// bodies it is stepping -- so the ball lying on it counted as held up by nothing,
// took the whole of a tick's weight twenty times a second, and spent the along-the-
// face part of that fall as travel: a micrometre and a half a tick, for ever, on a
// plate that was not moving. It is measured here over ten seconds, by position.
void whatHoldsABall() {
    say("\nwhat a surface can hold\n");
    // A plate where the triangle was, at a chosen angle, with the ball set down on it.
    const auto onARamp = [](double angle, int ticks, double& moved, bool& asleep) {
        PlaygroundRoom room;
        room.reset();
        settle(room, 300);
        auto& plate = room.bodies_[1];
        plate.local[0] = Vec3{-1.5, -0.05, 0};
        plate.local[1] = Vec3{1.5, -0.05, 0};
        plate.local[2] = Vec3{1.5, 0.05, 0};
        plate.local[3] = Vec3{-1.5, 0.05, 0};
        plate.count = 4;
        plate.centre = Vec3{-2.5, 1.0, 0};
        plate.angle = angle;
        plate.velocity = Vec3{};
        plate.spin = 0.0;
        plate.frozen = true;
        room.placeCorners(plate);
        plate.previous = plate.position;
        const Vec3 normal{-std::sin(angle), std::cos(angle), 0};
        room.ball(PlaygroundRoom::kThrowBall).position = Vec3{plate.centre.x, plate.centre.y, 0} + normal * (0.05 + room.ball(PlaygroundRoom::kThrowBall).radius);
        room.ball(PlaygroundRoom::kThrowBall).velocity = Vec3{};
        room.ball(PlaygroundRoom::kThrowBall).spin = 0.0;
        room.ball(PlaygroundRoom::kThrowBall).frozen = false;
        const Vec3 start = room.ball(PlaygroundRoom::kThrowBall).position;
        double furthest = 0.0;
        for (int tick = 0; tick < ticks; ++tick) {
            room.advance(1.0 / 20.0);
            furthest = std::max(furthest, std::sqrt(dot(room.ball(PlaygroundRoom::kThrowBall).position - start, room.ball(PlaygroundRoom::kThrowBall).position - start)));
        }
        moved = furthest;
        asleep = room.ball(PlaygroundRoom::kThrowBall).frozen;
    };
    const double critical = std::atan(ball_shape::kBallRollingResistance);  // radians
    double stayed = 0.0, rolled = 0.0;
    bool stayedAsleep = false, rolledAsleep = false;
    onARamp(critical * 0.025, 400, stayed, stayedAsleep);   // a twentieth of the limit
    onARamp(critical * 2.0, 400, rolled, rolledAsleep);     // twice it
    char detail[240];
    std::snprintf(detail, sizeof detail,
                  "on a plate tilted %.3f degrees the ball moved %.3e m in ten seconds (%.3f degrees it went %.3f m)",
                  critical * 0.025 * 180.0 / 3.141592653589793, stayed, critical * 2.0 * 180.0 / 3.141592653589793,
                  rolled);
    check(stayed == 0.0 && stayedAsleep && rolled > 0.1,
          "the ball stays where it is put below its own rolling limit, and rolls away above it", detail);
    (void)rolledAsleep;
}

}  // namespace

// The ball is fired across the room at every speed up to a hundred kilometres a
// second, and the triangle is dropped on the ball that fast as well. Returns how many
// of the checks failed.
int playgroundArrivalChecks() {
    using gtc::native::PlaygroundRoom;
    using gtc::native::Vec3;
    char detail[240];

    double fastInside = 0.0;
    bool everNotANumber = false;
    {
        const double fast[] = {1.0, 6.0, 40.0, 1000.0, 100000.0};
        // Free floor to the right of the triangle's corner; the air above its slope;
        // the slope low; the slope high.
        const Vec3 starts[] = {{0.6, 0.12, 0}, {-1.55, 3.4, 0}, {0.6, 0.35, 0}, {0.6, 1.2, 0}};
        const Vec3 aims[] = {{-1, 0, 0}, {0, -1, 0}, {-1, 0, 0}, {-1, 0, 0}};
        for (const double speed : fast) {
            for (int shot = 0; shot < 4; ++shot) {
                PlaygroundRoom room;
                room.reset();
                settle(room, 400);
                room.ball(PlaygroundRoom::kThrowBall).position = starts[shot];
                room.ball(PlaygroundRoom::kThrowBall).velocity = aims[shot] * speed;
                room.ball(PlaygroundRoom::kThrowBall).spin = 0.0;
                room.ball(PlaygroundRoom::kThrowBall).frozen = false;
                for (int tick = 0; tick < 120; ++tick) {
                    room.advance(1.0 / 20.0);
                    for (std::size_t index = 0; index < PlaygroundRoom::kBodyCount; ++index) {
                        Vec3 way{}, at{};
                        fastInside = std::max(fastInside, -room.bodyBallGap(room.ball(PlaygroundRoom::kThrowBall), index, way, at));
                        const PlaygroundRoom::Body& body = room.bodies_[index];
                        if (!std::isfinite(body.centre.x) || !std::isfinite(body.velocity.x) ||
                            !std::isfinite(body.spin))
                            everNotANumber = true;
                        for (int corner = 0; corner < body.count; ++corner)
                            if (!std::isfinite(body.position[corner].x) || !std::isfinite(body.position[corner].y))
                                everNotANumber = true;
                    }
                    if (!std::isfinite(room.ball(PlaygroundRoom::kThrowBall).position.x) || !std::isfinite(room.ball(PlaygroundRoom::kThrowBall).position.y) ||
                        !std::isfinite(room.ball(PlaygroundRoom::kThrowBall).velocity.x) || !std::isfinite(room.ball(PlaygroundRoom::kThrowBall).velocity.y) ||
                        !std::isfinite(room.ball(PlaygroundRoom::kThrowBall).spin))
                        everNotANumber = true;
                }
            }
        }
    }
    std::snprintf(detail, sizeof detail,
                  "20 shots at 1, 6, 40, 1 000 and 100 000 m/s into the corner and both ways at the slope, "
                  "worst %.3e m inside",
                  fastInside);
    check(fastInside < 1e-6, "a shot at a hundred kilometres a second is stopped by the same geometry", detail);
    check(!everNotANumber, "and nothing in the room ever stops being a number",
          "every centre, corner, speed and spin of the ball and of all three shapes stayed finite");

    // The same speed the other way round: the shape arrives at the ball. Nothing gets a
    // way through a ball just because it is the one moving quickly.
    double thrown = 0.0;
    bool thrownFinite = true;
    {
        for (const double speed : {1.0, 40.0, 1000.0, 100000.0}) {
            PlaygroundRoom room;
            room.reset();
            settle(room, 400);
            room.ball(PlaygroundRoom::kThrowBall).position = Vec3{0.30, room.ball(PlaygroundRoom::kThrowBall).radius, 0};
            room.ball(PlaygroundRoom::kThrowBall).velocity = Vec3{};
            room.ball(PlaygroundRoom::kThrowBall).spin = 0.0;
            room.ball(PlaygroundRoom::kThrowBall).frozen = false;
            // The triangle's bottom face, level, just above the ball, coming down.
            room.bodies_[1].centre = Vec3{0.30, 0.99, 0};
            room.bodies_[1].angle = 0.0;
            room.bodies_[1].velocity = Vec3{0, -speed, 0};
            room.bodies_[1].spin = 0.0;
            room.bodies_[1].frozen = false;
            room.placeCorners(room.bodies_[1]);
            room.bodies_[1].previous = room.bodies_[1].position;
            for (int tick = 0; tick < 120; ++tick) {
                room.advance(1.0 / 20.0);
                Vec3 way{}, at{};
                thrown = std::max(thrown, -room.bodyBallGap(room.ball(PlaygroundRoom::kThrowBall), 1, way, at));
                if (!std::isfinite(room.ball(PlaygroundRoom::kThrowBall).position.x) || !std::isfinite(room.bodies_[1].centre.x) ||
                    !std::isfinite(room.bodies_[1].velocity.x))
                    thrownFinite = false;
            }
        }
    }
    std::snprintf(detail, sizeof detail,
                  "the triangle dropped on the ball at 1, 40, 1 000 and 100 000 m/s, worst %.3e m inside", thrown);
    check(thrown < 1e-6, "nor is one that a shape arrives at at a hundred kilometres a second", detail);
    check(thrownFinite, "and that leaves a number where its position was", detail);

    for (int fps : {30, 60, 144})
        for (bool awake : {false, true})
            for (double offset : {0.12, 0.16, 0.20}) horizontalRopeImpact(fps, awake, offset);
    for (double speed : {1.0, 2.0, 4.0})
        for (bool awake : {false, true})
            for (double gap : {0.03, 0.15}) floorBallHitsCube(speed, awake, gap);
    ordinaryReleaseDoesNotThrow();
    bounceIsTheSameEveryWay();
    aRollIsARoll();
    whatHoldsABall();
    return localFailures;
}
