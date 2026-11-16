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
    const bool trace = std::getenv("GTC_TRACE_HORIZONTAL") != nullptr && fps == 60 && !awake && sideOffset == 0.16;
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
    return localFailures;
}
