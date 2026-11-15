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
                room.ball_.position = starts[shot];
                room.ball_.velocity = aims[shot] * speed;
                room.ball_.spin = 0.0;
                room.ball_.frozen = false;
                for (int tick = 0; tick < 120; ++tick) {
                    room.advance(1.0 / 20.0);
                    for (std::size_t index = 0; index < PlaygroundRoom::kBodyCount; ++index) {
                        Vec3 way{}, at{};
                        fastInside = std::max(fastInside, -room.bodyBallGap(index, way, at));
                        const PlaygroundRoom::Body& body = room.bodies_[index];
                        if (!std::isfinite(body.centre.x) || !std::isfinite(body.velocity.x) ||
                            !std::isfinite(body.spin))
                            everNotANumber = true;
                        for (int corner = 0; corner < body.count; ++corner)
                            if (!std::isfinite(body.position[corner].x) || !std::isfinite(body.position[corner].y))
                                everNotANumber = true;
                    }
                    if (!std::isfinite(room.ball_.position.x) || !std::isfinite(room.ball_.position.y) ||
                        !std::isfinite(room.ball_.velocity.x) || !std::isfinite(room.ball_.velocity.y) ||
                        !std::isfinite(room.ball_.spin))
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
            room.ball_.position = Vec3{0.30, room.ball_.radius, 0};
            room.ball_.velocity = Vec3{};
            room.ball_.spin = 0.0;
            room.ball_.frozen = false;
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
                thrown = std::max(thrown, -room.bodyBallGap(1, way, at));
                if (!std::isfinite(room.ball_.position.x) || !std::isfinite(room.bodies_[1].centre.x) ||
                    !std::isfinite(room.bodies_[1].velocity.x))
                    thrownFinite = false;
            }
        }
    }
    std::snprintf(detail, sizeof detail,
                  "the triangle dropped on the ball at 1, 40, 1 000 and 100 000 m/s, worst %.3e m inside", thrown);
    check(thrown < 1e-6, "nor is one that a shape arrives at at a hundred kilometres a second", detail);
    check(thrownFinite, "and that leaves a number where its position was", detail);

    return localFailures;
}
