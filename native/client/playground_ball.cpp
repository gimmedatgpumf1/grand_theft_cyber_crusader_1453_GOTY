#include "client/playground_room.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {
using namespace playground_detail;

// The ball: one point with a radius, and spin. It is not made of corner points
// and there is nothing on its surface held at a fixed distance -- it is a single
// body whose only shape is how far it reaches.
//
// The spin is not decoration. Friction acts at the bottom of the ball, which is
// a lever arm from its middle, so the same force that slows it down also turns
// it. A sphere that could not turn would be slowed like a brick instead of
// rolling, which is not what a ball does.

namespace {

constexpr double kTick = 1.0 / 20.0;
constexpr double kGravity = 9.8;
constexpr double kMass = 1.0;        // the point's mass, for the exchange with it
constexpr double kSurfaceY = 0.0;
constexpr int kMaxContacts = 4;
// How much a rolling ball gives up to the floor as it goes. A basketball on a
// wooden floor rolls a long way, so this is small.
constexpr double kRollingResistance = 0.015;
// How close to rolling without slipping counts as rolling.
constexpr double kRollingSlip = 0.02;  // m/s
constexpr double kBallKineticFriction = 0.30;
// Below this it is not moving in any way that matters.
constexpr double kBallSleepSpeed = 0.05;  // m/s
constexpr int kBallSleepTicks = 2;

// Where the ball's own path first reaches a piece of a body's outline. A convex
// body's surface can only be reached by the ball in two ways: the ball's centre
// crosses the outline, or the centre comes within a radius of one of its
// corners. Both are exact, so nothing is sampled or guessed.
bool ballHitsCapsule(Vec3 from, Vec3 to, Vec3 a, Vec3 b, double radius, double& along, Vec3& normal) {
    const Vec3 travel = to - from;
    double best = 2.0;
    Vec3 bestNormal{0, 1, 0};
    // Corner circles.
    for (const Vec3& corner : {a, b}) {
        double hit = 2.0;
        Vec3 facing{0, 1, 0};
        if (segmentHitsCircle(from, to, corner, radius, hit, facing) && hit < best) {
            best = hit;
            bestNormal = facing;
        }
    }
    // Reaching the side. A ball does not have to get its middle to the surface:
    // it touches when its middle is a radius away, so the side is looked at where
    // the ball's own surface would meet it, one radius further out.
    const Vec3 side = b - a;
    const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
    if (sideLength > 1e-12) {
        const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        const double start = dot(from - a, outward) - radius;
        const double end = dot(to - a, outward) - radius;
        // Only a crossing from outside to inside counts, and only through the
        // part of the line the side actually covers.
        if (start > 0.0 && end <= 0.0) {
            const double crossing = start / (start - end);
            const Vec3 at = from + travel * crossing;
            const double position = dot(at - a, side) / (sideLength * sideLength);
            if (position >= 0.0 && position <= 1.0 && crossing < best) {
                best = crossing;
                bestNormal = outward;
            }
        }
    }
    if (best > 1.0) return false;
    along = best;
    normal = bestNormal;
    return true;
}

} // namespace

void PlaygroundRoom::tickBall() {
    Ball& ball = ball_;
    ball.previous = ball.position;
    ball.previousAngle = ball.angle;
    if (dragTarget_ == DragTarget::Ball) {
        ball.velocity = {};
        ball.spin = 0.0;
        ball.frozen = false;
        return;
    }
    // In the air the ball is a projectile: gravity, and nothing else. Its spin is
    // untouched, because nothing is touching it to change it.
    ball.velocity.y -= kGravity * kTick;
    ball.touching = false;
    ball.rolling = false;
    ball.level = true;
    double remaining = 1.0;
    for (int step = 0; step < kMaxContacts && remaining > 1e-9; ++step) {
        const Vec3 next = ball.position + ball.velocity * (kTick * remaining);
        double first = 2.0;
        Vec3 normal{0, 1, 0};
        int what = -1;  // -2 the plane, -3 the point, otherwise a body
        if (next.y < ball.radius && ball.velocity.y < 0.0) {
            const double fall = next.y - ball.position.y;
            if (std::abs(fall) > 1e-12) {
                first = std::clamp((ball.radius - ball.position.y) / fall, 0.0, 1.0);
                normal = {0, 1, 0};
                what = -2;
            }
        }
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            for (std::size_t edge = 0; edge < body.count; ++edge) {
                double along = 2.0;
                Vec3 facing{0, 1, 0};
                if (!ballHitsCapsule(ball.position, next, body.position[edge],
                                     body.position[(edge + 1) % body.count], ball.radius, along, facing))
                    continue;
                if (along < first) {
                    first = along;
                    normal = facing;
                    what = int(index);
                }
            }
        }
        {
            double along = 2.0;
            Vec3 facing{0, 1, 0};
            if (segmentHitsCircle(ball.position, next, point_, kPointRadius + ball.radius, along, facing) &&
                along < first) {
                first = along;
                normal = facing;
                what = -3;
            }
        }
        if (first > 1.0) {
            ball.position = next;
            break;
        }
        ball.position = ball.position + (next - ball.position) * first;
        remaining *= 1.0 - first;
        ball.touching = true;
        if (what == -2) {
            ballContact(normal, -1);
        } else if (what == -3) {
            hitBallWithPoint(normal);
        } else {
            ballContact(normal, what);
        }
        if (!(length(ball.velocity) > 1e-12)) break;
    }
    // Once the tick's travel is done. A ball that is rolling gives up a little
    // speed to whatever it is rolling on, and its turn slows by exactly as much,
    // so it stays rolling instead of turning into a skid.
    if (ball.touching) {
        const double speed = length(ball.velocity);
        if (ball.rolling && speed > 1e-9) {
            const double loss = kRollingResistance * kGravity * kTick;
            const double keep = speed <= loss ? 0.0 : (speed - loss) / speed;
            ball.velocity = ball.velocity * keep;
            ball.spin *= keep;
        }
        // It only stops for good on something level. On a slope a ball always has
        // somewhere to go, so it is never still.
        const bool quiet = speed < kBallSleepSpeed && ball.level;
        ball.quietTicks = quiet ? ball.quietTicks + 1 : 0;
        if (ball.quietTicks >= kBallSleepTicks) {
            ball.frozen = true;
            ball.velocity = {};
            ball.spin = 0.0;
            ball.previous = ball.position;
            ball.shown = ball.position;
        }
    } else {
        ball.quietTicks = 0;
    }
    settleBallIntoRoom();
    ball.angle += ball.spin * kTick;
}

// The ball against something. The normal part is answered by both sides: if the
// other side can move, the ball keeps whatever it did not hand over. The rest of
// the contact is friction, and because it acts at the bottom of the ball rather
// than at its middle it turns the ball as well as slowing it.
void PlaygroundRoom::ballContact(Vec3 normal, int bodyIndex) {
    Ball& ball = ball_;
    Body* body = bodyIndex >= 0 ? &bodies_[std::size_t(bodyIndex)] : nullptr;
    std::size_t struck = 0;
    Vec3 answer = normal;
    bool canMove = false;
    if (body != nullptr) {
        double nearest = 1e30;
        for (std::size_t i = 0; i < body->count; ++i) {
            const double distance = length(ball.position - body->position[i]);
            if (distance < nearest) {
                nearest = distance;
                struck = i;
            }
        }
        bool supported = false;
        for (std::size_t i = 0; i < body->count; ++i) supported = supported || body->position[i].y <= kSurfaceY + 1e-9;
        // The floor answers only what drives into it; the rest is the body's to
        // take. A body held by the mouse has nowhere to go at all.
        answer = dragTarget_ == DragTarget::Body ? Vec3{} : (supported ? Vec3{normal.x, 0, 0} : normal);
        canMove = length(answer) > 1e-9;
    }
    const Vec3 surfaceVelocity = canMove ? body->velocity[struck] : Vec3{};
    double pressed = ball.mass * kGravity * kTick;
    const double closing = dot(ball.velocity - surfaceVelocity, normal);
    if (closing < 0.0) {
        constexpr double kStruckShare = 0.5;
        if (canMove) {
            const double impulse = -closing / (1.0 / ball.mass + kStruckShare / kMass);
            ball.velocity = ball.velocity + normal * (impulse / ball.mass);
            const double rest = (1.0 - kStruckShare) / static_cast<double>(body->count - 1);
            for (std::size_t i = 0; i < body->count; ++i) {
                const double share = i == struck ? kStruckShare : rest;
                body->velocity[i] = body->velocity[i] - answer * (impulse * share / kMass);
            }
            body->frozen = false;
            pressed = std::max(pressed, impulse);
        } else {
            // Nowhere to go: the ball takes the whole answer itself.
            const double impulse = -closing * ball.mass;
            ball.velocity = ball.velocity + normal * (impulse / ball.mass);
            pressed = std::max(pressed, impulse);
        }
    }
    ball.level = ball.level && normal.y > 0.99;
    const Vec3 tangent{normal.y, -normal.x, 0};
    const Vec3 arm = normal * -ball.radius;
    const double inertia = 0.4 * ball.mass * ball.radius * ball.radius;  // a solid sphere
    const Vec3 spinSpeed{-ball.spin * arm.y, ball.spin * arm.x, 0};
    const double slip = dot(ball.velocity + spinSpeed - surfaceVelocity, tangent);
    if (std::abs(slip) < kRollingSlip) {
        ball.rolling = true;  // the surface holds the bottom of the ball
        return;
    }
    // Friction is limited by mu * N and acts on the speed and the spin alike.
    const double lever = arm.x * tangent.y - arm.y * tangent.x;
    const double effective = 1.0 / ball.mass + lever * lever / inertia;
    double impulse = -slip / effective;
    const double limit = kBallKineticFriction * pressed;
    const bool enoughGrip = std::abs(impulse) <= limit;
    if (!enoughGrip) impulse = std::clamp(impulse, -limit, limit);
    ball.velocity = ball.velocity + tangent * (impulse / ball.mass);
    ball.spin += lever * impulse / inertia;
    if (enoughGrip) ball.rolling = true;
}

// The ball runs into the point. Both have mass, so they exchange momentum along
// the line between them and nothing bounces.
void PlaygroundRoom::hitBallWithPoint(Vec3 normal) {
    Ball& ball = ball_;
    const Vec3 closing = ball.velocity - velocity_;
    const double into = dot(closing, normal);
    if (into < 0.0) {
        const double impulse = -into / (1.0 / ball.mass + 1.0 / kMass);
        ball.velocity = ball.velocity + normal * (impulse / ball.mass);
        velocity_ = velocity_ - normal * (impulse / kMass);
        ball.frozen = false;
        pointFrozen_ = false;
        resting_ = false;
        restingBody_ = -1;
    }
    // Separate them, so they are not still overlapping next tick.
    const Vec3 gap = ball.position - point_;
    const double distance = length(gap);
    const double overlap = kPointRadius + ball.radius - distance;
    if (overlap > 0.0 && distance > 1e-12) {
        ball.position = ball.position + gap * (overlap * 0.5 / distance);
        point_ = point_ - gap * (overlap * 0.5 / distance);
    }
}

// The point runs into the ball. The normal points from the ball to the point, so
// the point is the one that gets pushed back, and whatever it hands over is what
// the ball takes.
void PlaygroundRoom::pointAgainstBall(Vec3 normal) {
    Ball& ball = ball_;
    const double closing = dot(velocity_ - ball.velocity, normal);
    if (closing < 0.0) {
        const double impulse = -closing / (1.0 / kMass + 1.0 / ball.mass);
        velocity_ = velocity_ + normal * (impulse / kMass);
        ball.velocity = ball.velocity - normal * (impulse / ball.mass);
        ball.frozen = false;
        ball.quietTicks = 0;
        pointFrozen_ = false;
        resting_ = false;
        restingBody_ = -1;
    }
    // Separate them, so they are not still overlapping next tick.
    const Vec3 gap = point_ - ball.position;
    const double distance = length(gap);
    const double overlap = kPointRadius + ball.radius - distance;
    if (overlap > 0.0 && distance > 1e-12) {
        point_ = point_ + gap * (overlap * 0.5 / distance);
        ball.position = ball.position - gap * (overlap * 0.5 / distance);
    }
}

// A ball that has ended up inside something -- dragged into a body, or shoved
// through by a hit from the far side -- is put back on the surface it left. This
// is geometry, not a physics answer: nothing is being stopped that way, it is
// being undone.
void PlaygroundRoom::settleBallIntoRoom() {
    Ball& ball = ball_;
    if (ball.position.y < ball.radius) {
        ball.position.y = ball.radius;
        if (ball.velocity.y < 0.0) ball.velocity.y = 0.0;
    }
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        // Inside a body, the way out is the shortest one that does not go
        // through the floor. Being wholly inside is not a contact: there is no
        // surface between the two, so nothing in the contact code can answer it.
        bool inside = true;
        double bestMove = 1e30;
        Vec3 bestPlace = ball.position;
        Vec3 exitNormal{0, 1, 0};
        for (std::size_t edge = 0; edge < body.count; ++edge) {
            const Vec3 a = body.position[edge];
            const Vec3 b = body.position[(edge + 1) % body.count];
            const Vec3 side = b - a;
            const double sideLength = length(side);
            if (!(sideLength > 1e-12)) continue;
            const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
            const double depth = dot(ball.position - a, outward);
            if (depth > 0.0) inside = false;
            const double move = ball.radius - depth;
            if (move < 1e-9) continue;
            const Vec3 place = ball.position + outward * move;
            if (place.y < ball.radius - 1e-9) continue;  // not through the floor
            if (move < bestMove) {
                bestMove = move;
                bestPlace = place;
                exitNormal = outward;
            }
        }
        if (inside && bestMove < 1e30) {
            ball.position = bestPlace;
            const double into = dot(ball.velocity, exitNormal);
            if (into < 0.0) ball.velocity = ball.velocity - exitNormal * into;
            ball.frozen = false;
            continue;
        }
        double nearest = ball.radius;
        Vec3 way{0, 1, 0};
        for (std::size_t edge = 0; edge < body.count; ++edge) {
            const Vec3 a = body.position[edge];
            const Vec3 b = body.position[(edge + 1) % body.count];
            const Vec3 side = b - a;
            const double sideLength = side.x * side.x + side.y * side.y;
            const double t = sideLength > 1e-18
                                 ? std::clamp(dot(ball.position - a, side) / sideLength, 0.0, 1.0)
                                 : 0.0;
            const Vec3 gap = ball.position - (a + side * t);
            const double distance = length(gap);
            if (distance < nearest) {
                nearest = distance;
                if (distance > 1e-12) way = gap / distance;
            }
        }
        if (nearest >= ball.radius - 1e-9) continue;
        ball.position = ball.position + way * (ball.radius - nearest);
        const double into = dot(ball.velocity, way);
        if (into < 0.0) ball.velocity = ball.velocity - way * into;
    }
}

} // namespace gtc::native
