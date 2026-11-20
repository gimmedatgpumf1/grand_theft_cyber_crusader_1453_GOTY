// The ball on the rope, swung into the triangle's slope: is it jumpy, or is it a
// wrecking ball?
//
// The King's picture is a basketball on a string. What that means in numbers: it
// comes off the face at most of the speed it arrived with, it swings back and does it
// again, and the shape it hits is shoved and rocks. What a wrecking ball does instead
// is arrive, stop, and hang.
//
// The trace is per tick, from the moment the ball is drawn back along its arc: the
// speed it has, the speed it has along the face's own normal (negative = closing),
// where it is, what the shape is doing, and the room's energy.
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
    // The other ball out of the way: this probe is about one of them.
    auto& other = room.ball(PlaygroundRoom::kThrowBall);
    other.position = Vec3{-3.0, other.radius, 0};
    other.previous = other.shown = other.position;
    other.velocity = {};
    other.spin = 0.0;
    other.frozen = false;

    auto& triangle = room.bodies_[1];
    triangle.angle = 1.5707963267948966;
    triangle.centre = Vec3{3.6697, 0.7, 0};
    room.placeCorners(triangle);
    // The place the slope just touches the hanging ball, found by the room's own gap.
    double onTheSlope = 3.6697;
    Vec3 normal{}, at{};
    {
        const double gap = room.bodyBallGap(room.ropeBall(), 1, normal, at);
        if (std::abs(normal.x) > 0.0) onTheSlope -= gap / normal.x;
    }
    triangle.centre = Vec3{onTheSlope, 0.7, 0};
    room.placeCorners(triangle);
    triangle.previous = triangle.position;
    triangle.previousCentre = triangle.centre;
    triangle.previousAngle = triangle.angle;
    triangle.velocity = {};
    triangle.spin = 0.0;
    triangle.frozen = false;
    // bodyBallGap's way points from the ball at the shape; the face's normal is out of
    // the shape, so it is the other way round.
    const Vec3 face = normal * -1.0;

    auto& ball = room.ball(PlaygroundRoom::kRopeBall);
    const double drawnBack = -0.55;
    ball.position = room.ropeAnchor() + Vec3{std::sin(drawnBack), -std::cos(drawnBack), 0.0} * room.ropeLength();
    ball.previous = ball.shown = ball.position;
    ball.velocity = {};
    ball.spin = 0.0;
    ball.touching = ball.standing = false;

    std::printf("the ball on the rope: m %.3f kg, r %.3f m, rope %.3f m, drawn back %.2f rad\n", ball.mass, ball.radius,
                room.ropeLength(), drawnBack);
    std::printf("the face's normal is (%+.3f,%+.3f); along it, negative is closing\n\n", face.x, face.y);
    std::printf("%-4s %-9s %-9s %-9s %-9s %-8s %-8s %s\n", "tick", "speed", "along-n", "x", "y", "tri|v|", "tri spin", "energy");

    const double start = room.totalEnergy();
    double bestAfter = 0.0, worstRise = 0.0;
    int bounces = 0;
    bool wasClosing = false;
    for (int tick = 0; tick < 200; ++tick) {
        const double along = dot(ball.velocity, face);
        room.advance(dt);
        const double nowAlong = dot(ball.velocity, face);
        worstRise = std::max(worstRise, room.totalEnergy() - start);
        if (wasClosing && nowAlong > 0.0) {
            ++bounces;
            std::printf("      -- bounce %d: came off at %.3f m/s (%+.3f along the face)\n", bounces,
                        length(ball.velocity), nowAlong);
            if (bounces == 1) bestAfter = nowAlong;
        }
        wasClosing = along < -0.2 && nowAlong <= 0.0;
        if (tick < 60 || tick % 20 == 0)
            std::printf("%-4d %-9.4f %-9.4f %-9.5f %-9.5f %-8.4f %-8.4f %.6f\n", tick, length(ball.velocity), nowAlong,
                        ball.position.x, ball.position.y, length(triangle.velocity), triangle.spin,
                        room.totalEnergy());
    }
    std::printf("\nbounces in 10 s: %d; the first came off the face at %.3f m/s; the room's energy rose by at most %.3e J\n",
                bounces, bestAfter, worstRise);
    return 0;
}
