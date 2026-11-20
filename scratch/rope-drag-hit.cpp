// What the King does with the mouse: take the triangle and push it into the ball on
// the rope, then let go. A basketball on a string is shoved and swings away; a
// wrecking ball is shoved and hangs against the thing that shoved it.
//
// The mouse is a ray straight down the z axis onto the room's plane, which is what the
// camera in this room sends. Each tick is one call to advance(), and advance() reads
// the ray -- so moving the ray between ticks is moving the mouse.
#include <array>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#define private public
#include "client/playground_pair.h"
#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#undef private
using namespace gtc::native;

int main() {
    const double dt = 1.0 / 20.0;
    PlaygroundRoom room;
    room.reset();
    for (int i = 0; i < 200; ++i) room.advance(dt);
    auto& other = room.ball(PlaygroundRoom::kThrowBall);
    other.position = Vec3{-3.0, other.radius, 0};
    other.previous = other.shown = other.position;
    other.velocity = {};
    other.spin = 0.0;
    other.frozen = false;

    // The triangle, upright, parked a hand's width clear of the hanging ball.
    auto& triangle = room.bodies_[1];
    triangle.angle = 1.5707963267948966;
    triangle.centre = Vec3{3.6697, 0.7, 0};
    room.placeCorners(triangle);
    double onTheSlope = 3.6697;
    {
        Vec3 way{}, at{};
        const double gap = room.bodyBallGap(room.ropeBall(), 1, way, at);
        if (std::abs(way.x) > 0.0) onTheSlope -= gap / way.x;
    }
    const double clear = 0.400;  // a hand's width to the right of where it just touches
    triangle.centre = Vec3{onTheSlope + clear, 0.7, 0};
    room.placeCorners(triangle);
    triangle.previous = triangle.position;
    triangle.previousCentre = triangle.centre;
    triangle.previousAngle = triangle.angle;
    triangle.velocity = {};
    triangle.spin = 0.0;
    triangle.frozen = false;
    for (int i = 0; i < 20; ++i) room.advance(dt);

    auto& ball = room.ball(PlaygroundRoom::kRopeBall);
    std::printf("the ball hangs at (%.4f,%.4f); the slope is at x %.4f, %.3f m clear\n", ball.position.x,
                ball.position.y, triangle.centre.x, clear);

    // Take the triangle by a point on its slope, and push it left, into the ball.
    const Vec3 grabAt{triangle.centre.x - 0.05, 0.95, 0};
    room.setDragRay(grabAt + Vec3{0, 0, 2.6}, Vec3{0, 0, -1});
    room.beginDrag();
    std::printf("took hold of body %zu\n", room.dragBody_);
    std::printf("%-5s %-9s %-8s %-9s %-9s %s\n", "tick", "ball |v|", "ball x", "ball y", "tri x", "note");
    double worst = 0.0;
    for (int tick = 0; tick < 40; ++tick) {
        // The hand moves left two centimetres a tick -- a metre a second, a firm push.
        const Vec3 hand = grabAt + Vec3{-0.02 * double(tick + 1), 0, 0};
        room.setDragRay(hand + Vec3{0, 0, 2.6}, Vec3{0, 0, -1});
        room.advance(dt);
        worst = std::max(worst, length(ball.velocity));
        std::printf("%-5d %-9.4f %-8.4f %-9.4f %-9.4f %s\n", tick, length(ball.velocity), ball.position.x,
                    ball.position.y, triangle.centre.x, "");
    }
    room.endDrag();
    std::printf("-- let go --\n");
    for (int tick = 0; tick < 60; ++tick) {
        room.advance(dt);
        if (tick < 20)
            std::printf("t%-4d ball |v| %.4f at (%.4f,%.4f) | tri x %.4f |v| %.4f spin %+.3f\n", tick,
                        length(ball.velocity), ball.position.x, ball.position.y, triangle.centre.x,
                        length(triangle.velocity), triangle.spin);
    }
    std::printf("\nthe ball was given up to %.3f m/s while the hand pushed\n", worst);
    return 0;
}
