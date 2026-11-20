// The air, on the ball, and the one force in the room whose answer over a tick is
// solved rather than stepped into.
//
// It is the drag equation and nothing else: F = half rho Cd A v^2, against the motion,
// applied at the ball's middle. Everything that moves is pressed by it -- a ball the
// ground is carrying, a ball bouncing, a ball thrown -- because being held up is a fact
// about the vertical and about nothing else; a ball rolling across the room is still
// pushing air out of its way.
//
// The *impulse* it delivers over a tick is not the force times the tick, and the
// difference is not a detail. While the tick is short next to the time the air needs to
// take the ball's speed the two agree to a part in two hundred, which is why they are
// the same number at walking pace. At a hundred kilometres a second they are not the
// same number at all: the force times a tick there is a hundred times the momentum the
// ball is carrying, and adding it does not slow the ball down, it turns it round and
// throws it back the way it came -- and then further, every tick, for ever. That is
// not the air being strong, it is the arithmetic running out; the ball leaves the room
// at a hundred and forty degrees and comes back the following tick faster still, and
// within ten ticks every number in the room is a NaN.
//
// The law it is meant to be is an ordinary differential equation, and it has an exact
// answer. Along the motion,
//
//     dv/dt = -(c/m) v^2      =>      v(t) = v / (1 + (c/m) v t),     c = half rho Cd A
//
// so over one tick the speed falls by exactly the factor 1 / (1 + (c/m) v dt). That is
// monotone: it never reaches nought and never crosses it, so the air can take the whole
// of a ball's momentum and not one bit more, at any speed. There is nothing clamped
// here and nothing switched off -- it is the same force at every speed, solved.
//
// The arrow is the force at the speed the ball arrived with, which is where a tick
// begins; the impulse is that same law integrated over the tick.

#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <cmath>

namespace gtc::native {
using namespace ball_shape;

Vec3 PlaygroundRoom::ballAirDrag(Ball& ball, Vec3 velocity, double over) {
    const double speed = length(velocity);
    if (!(speed > 0.0)) return velocity;
    const double area = kPi * ball.radius * ball.radius;
    const double c = 0.5 * kAirDensity * kBallDragCoefficient * area;  // N per (m/s)^2
    // The force, drawn where it acts: at the middle, against the way the ball is going.
    addForce(ball.position, velocity * (-(c * speed * speed) / speed), ForceKind::Drag, nullptr,
             ownerOf(ball));
    if (!(over > 0.0)) return velocity;
    const double after = speed / (1.0 + (c / ball.mass) * speed * over);
    return velocity * (after / speed);
}

}  // namespace gtc::native
