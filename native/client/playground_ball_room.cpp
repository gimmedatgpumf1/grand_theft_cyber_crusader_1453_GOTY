// The room around the ball: what can reach it, where it may be put down, and the
// order in which the room's balls are ticked.
//
// This file holds the three questions that are about a ball *in this room* rather
// than about a ball in itself -- whether it has to be looked at, whether a place it
// is being carried to will hold it, and which one goes first. The tick itself is
// playground_ball.cpp, and there is nothing here that a second ball needs to know
// about: both of them are balls and both are asked the same three questions.

#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <cmath>

namespace gtc::native {
using namespace ball_shape;

// A ball that is asleep is not stepped, but only while nothing can reach it. The
// dot and every body count: if any of them is moving, the ball is looked at.
bool PlaygroundRoom::ballNeighboursMoving(const Ball& ball) const {
    if (!pointFrozen_ && length(velocity_) > 0.0) return true;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        if (!bodies_[index].frozen) return true;
    }
    // The other ball is a solid too, and a ball moving at a sleeping one is something
    // arriving at it. There is nothing about the one on the rope that makes it less of
    // a neighbour: it is a ball, and this is the test for a ball.
    for (std::size_t other = 0; other < kBallCount; ++other) {
        if (&balls_[other] == &ball) continue;
        if (length(balls_[other].position - balls_[other].previous) > 0.0) return true;
    }
    // And the rope, on the ball it is tied to. A rope pulls, and a ball with a rope on
    // it is not lying on anything -- it is hanging, and its weight is answered by the
    // rope every tick. A ball that slept through that would be a ball with its weight
    // unanswered, which is not what a hanging ball is doing.
    if (rope_.attached && &balls_[rope_.ball < kBallCount ? rope_.ball : kThrowBall] == &ball) return true;
    return false;
}

// Whether the ball can be put down at this spot. It is the mouse that needs this,
// not the physics: the ball is carried, not simulated, while it is held, and a
// ball put down inside a solid would have to be pushed back out -- which is not
// something this room ever does, because nothing ever gets in. So the put-down
// itself is where the rule belongs: a spot the ball's circle would not fit in is
// simply not a spot it can be put.
bool PlaygroundRoom::ballPlacementClear(const Ball& ball, Vec3 at) const {
    if (at.y < ball.radius - 1e-9) return false;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (insideOutline(at, body.position.data(), body.count)) return false;
        for (std::size_t edge = 0; edge < body.count; ++edge) {
            const Vec3 a = body.position[edge];
            const Vec3 b = body.position[(edge + 1) % body.count];
            const Vec3 side = b - a;
            const double sideLength = std::sqrt(dot(side, side));
            if (!(sideLength > 1e-12)) continue;
            // How far the ball's middle is from this edge: closer than its own
            // radius and its surface would be through the outline.
            const double t = std::clamp(dot(at - a, side) / dot(side, side), 0.0, 1.0);
            if (length(at - (a + side * t)) < ball.radius) return false;
        }
    }
    return true;
}

// Every ball, one after another. Each is the same tick against the whole room; the one
// on the rope is held on it as soon as its own tick is over, so that the next ball --
// and the drawing -- meet it where the rope really left it rather than where its own
// gravity took it.
void PlaygroundRoom::tickBalls() {
    for (std::size_t index = 0; index < kBallCount; ++index) {
        Ball& ball = balls_[index];
        if (!ball.frozen || ballNeighboursMoving(ball)) tickBall(ball);
        else {
            // Asleep: its tick is not run, so what it would have begun with is the
            // speed it has, and nothing in the room is going to move it this tick --
            // which is an answer, and the rope must not invent a different one.
            ball.previous = ball.position;
            ball.previousAngle = ball.angle;
            ball.began = ball.velocity;
            ball.alone = ball.velocity;
            ball.answered = true;
        }
        if (rope_.attached && rope_.ball == index) {
            if (ballHeld(ball)) {
                // The hand, not gravity, owns the held position. Do not advance a
                // pendulum step or invent tension until the ball is released.
                rope_.taut = length(ball.position - rope_.anchor) >= rope_.length - ball_shape::kTouch;
                rope_.tension = 0.0;
            } else {
                holdOnRope(ball);
            }
        }
    }
}

}  // namespace gtc::native
