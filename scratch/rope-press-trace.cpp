// A slope pressed into the ball on the rope: who gives way, and does the shape move?
//
// The press is a millimetre of the triangle driven into the ball and held there. The
// ball is the one thing here that may be moved out of something it is in, so the ball
// gives way and the shape -- held by the hand -- must not. What this prints per tick is
// the ball's distance off its rope's arc, the triangle's centre against where it was
// put, and what either of them is doing.
#include <algorithm>
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

    auto& triangle = room.bodies_[1];
    triangle.angle = 1.5707963267948966;
    triangle.centre = Vec3{3.6697, 0.7, 0};
    room.placeCorners(triangle);
    double onTheSlope = 3.6697;
    Vec3 way{}, at{};
    {
        const double gap = room.bodyBallGap(room.ropeBall(), 1, way, at);
        if (std::abs(way.x) > 0.0) onTheSlope -= gap / way.x;
    }
    const double press = 1e-3;
    triangle.centre = Vec3{onTheSlope - press, 0.7, 0};
    room.placeCorners(triangle);
    triangle.previous = triangle.position;
    triangle.previousCentre = triangle.centre;
    triangle.previousAngle = triangle.angle;
    triangle.velocity = {};
    triangle.spin = 0.0;
    triangle.frozen = true;
    const Vec3 was = triangle.centre;

    auto& ball = room.ball(PlaygroundRoom::kRopeBall);
    std::printf("pressed by %.4f m; the ball is at (%.6f,%.6f), the shape at (%.6f,%.6f)\n", press, ball.position.x,
                ball.position.y, triangle.centre.x, triangle.centre.y);
    std::printf("%-5s %-10s %-10s %-11s %-11s %-9s %s\n", "tick", "ball v", "ball spin", "shape off by", "shape |v|",
                "gap", "energy");
    double worst = 0.0, rise = 0.0;
    const double start = room.totalEnergy();
    for (int tick = 0; tick < 3000; ++tick) {
        room.advance(dt);
        worst = std::max(worst, length(triangle.centre - was));
        rise = std::max(rise, room.totalEnergy() - start);
        if (tick < 12 || tick % 500 == 0)
            std::printf("%-5d %-10.6f %-10.6f %-11.4e %-11.4e %-9.3e %.6f\n", tick, length(ball.velocity), ball.spin,
                        length(triangle.centre - was), length(triangle.velocity),
                        room.bodyBallGap(room.ropeBall(), 1, way, at), room.totalEnergy());
    }
    std::printf("\nthe shape moved %.4e m at worst (the press was %.4e); the room's energy rose by at most %.4e J\n", worst,
                press, rise);
    return 0;
}
