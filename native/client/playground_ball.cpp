#include "client/playground_ball_shape.h"
#include "client/playground_room_tick.h"
#include <cstdio>
#include <cstdlib>
#include "client/playground_shapes.h"

namespace gtc::native {
using namespace ball_shape;
using namespace playground_detail;
// playground_tick is named where it is used: ball_shape has its own kTick and
// kGravity for the geometry, and the two must not be confused.

// The ball: one point with a radius, and spin. It is not made of corner points
// and there is nothing sampled on its surface -- it is a single body whose only
// shape is how far it reaches.
//
// Its spin is not decoration. Friction acts at the bottom of the ball, one radius
// from its middle, so the same force that slows it down also turns it. A sphere
// that could not turn would be slowed like a brick instead of rolling.
//
// This file is the ball's tick: the forces it is given, the walk that spends the
// tick's travel against everything in the room, and what "at rest" means. The
// geometry it walks against is playground_ball_shape.cpp, the answer it gives a
// contact is playground_ball_contact.cpp, and the grip of all its contacts at once
// is playground_ball_touch.cpp. There is no speed at which one rule turns into
// another.

// A ball that is asleep is not stepped, but only while nothing can reach it. The
// dot and every body count: if any of them is moving, the ball is looked at.
bool PlaygroundRoom::ballNeighboursMoving() const {
    if (!pointFrozen_ && length(velocity_) > 0.0) return true;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        if (!bodies_[index].frozen) return true;
    }
    return false;
}

// The gap between a body's outline and the ball's surface: how far apart they are,
// which way the body has to travel to be further from the ball, and the place the
// two of them would touch. Negative when the ball is already into the outline.
//
// The bodies are walked first and the ball afterwards, so for the length of the
// bodies' walk the ball is a surface that stays where it is; this is what the walk
// measures its step against, and what it stops at.
double PlaygroundRoom::bodyBallGap(std::size_t index, Vec3& way, Vec3& at) const {
    const Body& body = bodies_[index];
    way = Vec3{0, 1, 0};
    at = ball_.position;
    if (body.count == 0) return 1e30;
    const OutlineGap gap =
        outlineGap(ball_.position, body.position.data(), body.position.data(), body.count, 1.0);
    way = gap.normal * -1.0;
    // Where the two meet is on the ball's own surface, in the direction the body
    // lies from its centre. It is not the body's nearest point: a round ball pressed
    // against a flat face touches at the foot of the perpendicular, and the body's
    // nearest point is the same place only while the centre is over the face. Take
    // the body's point and a ball wedged on the end of a face is answered half a
    // metre from where it is really pressed, which is a lever about the ball's
    // centre that nothing in the room gave it.
    at = ball_.position + way * ball_.radius;
    return gap.gap - ball_.radius;
}

void PlaygroundRoom::tickBall() {
    Ball& ball = ball_;
    ballTouchCount_ = 0;
    ball.previous = ball.position;
    ball.previousAngle = ball.angle;
    if (dragTarget_ == DragTarget::Ball) {
        ball.velocity = {};
        ball.spin = 0.0;
        ball.frozen = false;
        // It is being carried, so what it was touching before means nothing.
        ball.touching = false;
        ball.standing = true;
        return;
    }
    if (ball.frozen && !ballNeighboursMoving()) return;
    ball.frozen = false;

    // What the ball arrived with is recorded once for the whole tick, before
    // anything in the room moved -- see advance(). A push the dot gives it later
    // in the same tick is not something the ball arrived with.

    // A ball that ended the last tick at rest on something that can hold it is
    // held: the surfaces answer its weight, so there is no force left to give it
    // any speed, and it stays exactly where it is. This is the same rule the dot
    // follows -- a surface that can carry the weight leaves nothing for gravity to
    // do -- and it is what makes "rest" actually rest instead of a floor and a
    // weight trading the same impulse back and forth every tick.
    const bool held = ball.touching && ball.standing && length(ball.velocity) < kRestSpeed;

    // Forces for this tick: weight, and the air it has to push through. Both are
    // forces in newtons in one accumulator, and the integrator turns the pair of
    // them into speed -- the ball is heavy, so its weight is a large force, and it
    // falls for exactly that reason.
    const Vec3 weight{0.0, -ball.mass * playground_tick::kGravity, 0.0};
    ball.force = {};
    addForce(ball.position, weight, ForceKind::Gravity, &ball.force, kBallOwner);
    if (held) {
        // The surface under it carries it: the same weight back, at the point the
        // ball is standing on -- along the way out of that surface, which is where
        // the ball is actually pressed and the one place the force can act.
        addForce(ball.position + ball.supportNormal * (-ball.radius), weight * -1.0, ForceKind::Contact,
                 &ball.force, kBallOwner);
    } else {
        const double speed = length(ball.velocity);
        if (speed > 0.0) {
            const double area = kPi * ball.radius * ball.radius;
            const double drag = 0.5 * kAirDensity * kBallDragCoefficient * area * speed * speed;  // N
            addForce(ball.position, ball.velocity * (-drag / speed), ForceKind::Drag, &ball.force, kBallOwner);
        }
    }
    ball.velocity = playground_tick::accelerated(ball.velocity, ball.force, ball.mass);

    ball.touching = false;
    ball.standing = true;
    // Which bodies have already had their answer this tick. One landing is one
    // contact: the walk goes on after it with the time that is left, and the same
    // body met again in what is left of the tick would be answered a second time
    // for the same blow. That is what launched a triangle off a ball it had just
    // landed on -- two impulses in one tick, and the body left the contact faster
    // than it arrived.
    std::array<bool, kBodyCount> answered{};
    // The floor and the dot are surfaces too, and they get one answer each in a
    // tick for the same reason. A ball lying on the floor meets it at zero distance
    // on every tick, and answering that over and over used up the walk before it
    // ever got to the thing that was actually arriving.
    bool floorAnswered = false;
    // The bodies the ball is already resting on, one answer each for the same
    // reason: being on a surface is not arriving at it, and a ball that answered
    // that touch every time the walk looked at it would never spend a tick.
    std::array<bool, kBodyCount> restingAnswered{};
    const auto anybodyMoving = [this]() {
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            if (bodies_[index].frozen) continue;
            for (std::size_t i = 0; i < bodies_[index].count; ++i)
                if (length(bodies_[index].position[i] - bodies_[index].previous[i]) > 0.0) return true;
        }
        return length(point_ - Vec3{}) >= 0.0 && !pointFrozen_ && length(velocity_) > 0.0;
    };
    double remaining = 1.0;
    for (int step = 0; step < kMaxContacts && remaining > 1e-12; ++step) {
        const Vec3 next = ball.position + ball.velocity * (kTick * remaining);
        const Vec3 travel = next - ball.position;
        double first = 2.0;
        double depth = 0.0;
        Vec3 normal{0, 1, 0};
        Vec3 contactPoint = ball.position;
        int what = -1;  // -2 the floor, otherwise a body
        // A surface the ball is already on has its answer given below, and that
        // answer changes the ball's speed -- but where the ball would have gone this
        // tick was worked out before the answer was. Spending it anyway sent the
        // ball a whole gravity step into the surface, 24 mm through the triangle's
        // tip every tick. So the walk starts the same instant again, answer in hand.
        bool restart = false;
        // The floor is the one thing nothing gets below, so it is met whenever the
        // ball is at it or on its way through it -- and if the ball is somehow
        // already under it, that is answered at once too.
        if (!floorAnswered && (ball.position.y < ball.radius || (next.y < ball.radius && travel.y < 0.0))) {
            // Not negative zero: a distance of zero along a downward step divides
            // out to -0, and -0 loses every "did I get there first" comparison it
            // takes part in, so an exact hit would be passed over.
            first = travel.y < 0.0
                        ? std::max(0.0, std::clamp((ball.radius - ball.position.y) / travel.y, 0.0, 1.0))
                        : 0.0;
        }
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            // What the body itself did this tick. The bodies are stepped before the
            // ball, so by the time the ball looks, the shape is already at the end of
            // its own travel -- and the distance between the two of them closed by
            // both of them moving. Sweeping the ball's own travel against the finished
            // pose asks only "could the ball have reached where the shape now is", and
            // that is the wrong question whenever the shape is the one arriving: a
            // cube coming down on a ball the floor has already stopped closes eight
            // centimetres on it in a tick while the ball travels none of it, and a
            // sweep that does not subtract the shape's step sees no approach at all.
            // It saw none, and the cube's face went through the ball.
            //
            // So the sweep is done in the shape's own frame: the ball's travel, less
            // the shape's travel. That is exactly the motion of one against the other,
            // and it is what a hitscan between two moving things is. Nothing is
            // sampled and nothing is stepped: the meeting is still solved outright,
            // which is what makes it independent of how fast either of them is going.
            Vec3 bodyStep{};
            for (std::size_t i = 0; i < body.count; ++i) bodyStep = bodyStep + (body.position[i] - body.previous[i]);
            bodyStep = bodyStep * (1.0 / static_cast<double>(body.count));
            // One test per body, not one per edge: the sweep walks the ball's path
            // against the whole outline as the body moves through the tick, and
            // answers with the first piece of it the ball's surface reaches.
            OutlineHit hit;
            if (!circleHitsBody(ball.position, travel - bodyStep, body.position.data(), body.count, ball.radius,
                                hit))
                continue;

            // Already on this surface when the tick began: a contact for the whole
            // tick, answered here and then travelled along, not a meeting to stop
            // at. Stopping there eats every tick's travel and the ball sits
            // gathering speed and spinning instead of rolling.
            // A ball within a skin of the face is against it, not through it. The
            // arithmetic that rests a ball at exactly one radius lands it a
            // nanometre either side, and reading that as inside pinned a ball under
            // a shape where it was, gathering turn on the spot.
            if (hit.depth <= kSkin && hit.resting && !restingAnswered[index]) {
                restingAnswered[index] = true;
                const OutlineGap on = outlineGap(ball.position, body.position.data(), body.position.data(),
                                                 body.count, 1.0);
                if (on.gap < ball.radius) ball.position = ball.position + on.normal * (ball.radius - on.gap);
                ball.touching = true;
                const bool answer = !answered[index];
                if (answer) answered[index] = true;
                ballContact(hit.normal, int(index), held, hit.contact, 0.0, answer);
                // Answered, not skipped. What is left of the tick is still the
                // ball's to spend, and the face is still a face: it is tested again
                // below, so a ball that gravity has driven into it mid-tick is
                // stopped at the surface instead of ending the tick buried in it.
                restart = true;
                continue;
            }
            // Touching and getting no closer is touching, not arriving. A ball
            // already through the outline is never skipped: that is inside, and it
            // has to be answered. What the body did this tick is its actual step,
            // not its stored velocity: a corner's velocity is cut to nothing by the
            // floor while the step still carries the body down into the ball.
            if (hit.depth <= kSkin && hit.along <= 1e-9) {
                const Vec3 surfaceVelocity = bodyStep * (1.0 / kTick);
                if (dot(ball.velocity - surfaceVelocity, hit.normal) >= 0.0) continue;
            }
            if (hit.along < first) {
                first = hit.along;
                normal = hit.normal;
                depth = hit.depth;
                what = int(index);
                contactPoint = hit.contact;
            }
        }
        if (restart) continue;
        if (first > 1.0) {
            ball.position = next;
            break;
        }
        ball.position = ball.position + travel * first;
        remaining *= 1.0 - first;
        // Has this body already had its answer this tick? Asked before the mark is
        // put down, or the first meeting in a tick would be counted as the second.
        const bool exchange = what < 0 || !answered[std::size_t(what)];
        if (what >= 0 && std::size_t(what) < kBodyCount) answered[std::size_t(what)] = true;
        if (what == -2) {
            floorAnswered = true;
            ballContact(normal, -1);
        } else {
            // One blow per body per tick. A body met again in what is left of the
            // tick has already given its answer, so this time it only stops the
            // ball: it is a resting contact for the rest of the tick, which is what
            // stops a ball sliding into a closing wedge and being left inside it.
            ballContact(normal, what, held, contactPoint, depth, exchange);
        }
        // A ball that started the tick already through something has been put back
        // on its surface, and there is no honest travel left to spend. Touching is
        // not that: a ball resting on a surface is at exactly one radius from it,
        // and the arithmetic that got it there is good to about kTouch. Taken for a
        // ball inside, it would stop the walk dead every tick, and a ball that
        // cannot spend a tick's travel never leaves the slope it is on.
        // The floor cannot be given way to. Nothing above it can press the ball
        // through it, so it answers at once, where and when it is pressed: the ball
        // keeps its height and loses the speed that was pointing into the floor.
        if (ball.position.y <= ball.radius + kTouch) {
            ball.position.y = std::max(ball.position.y, ball.radius);
            if (ball.velocity.y < 0.0) ball.velocity.y = 0.0;
        }
        if (depth > kSkin && first <= 1e-9) break;
        // Not "the ball stopped, so stop looking": a ball standing still still has
        // something on its way to it -- a body is stepped before the ball, and its
        // step is felt here even though the ball itself is not going anywhere. The
        // walk ends when the time is spent or when every surface has had its answer.
        if (length(ball.velocity) <= 0.0 && !anybodyMoving()) break;
    }

    // What the ball is touching when the tick ends, and what may therefore not be
    // driven into. This runs whether or not the walk found anything, because the
    // walk answers contacts in the order it meets them and the last one it meets
    // is not necessarily the one that has the final say: a body pressing the ball
    // down onto the floor leaves the press in the ball's velocity, and it is the
    // floor that answers it. Touching when the tick ends counts as touching,
    // exactly as it does for the dot: the walk only reports arrivals, and a ball
    // already lying on something never arrives at it again.
    {
        const auto holds = [](Vec3 normal) {
            return kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));
        };
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            for (std::size_t edge = 0; edge < body.count; ++edge) {
                const std::size_t top = (edge + 1) % body.count;
                const Vec3 a = body.position[edge];
                const Vec3 b = body.position[top];
                const Vec3 side = b - a;
                const double sideLength = side.x * side.x + side.y * side.y;
                const double t = sideLength > 0.0 ? std::clamp(dot(ball.position - a, side) / sideLength, 0.0, 1.0) : 0.0;
                const Vec3 gap = ball.position - (a + side * t);
                const double distance = length(gap);
                if (distance > ball.radius + 1e-9 || distance <= 0.0) continue;
                const Vec3 normal = gap / distance;
                ball.touching = true;
                // A body above the ball is a weight on it, not something it stands
                // on: only a surface underneath can hold it up.
                if (normal.y > 1e-9) ball.standing = ball.standing && holds(normal);
                // The surface is where the ball is, so nothing can be driven into
                // it. Anything left over from an earlier contact in the same tick --
                // a body pressing the ball down onto the floor -- is answered here,
                // which is what lets a ball carry a weight instead of humming.
                const Vec3 closing = ball.velocity - pointSpeed(body, a + side * t);
                const double into = dot(closing, normal);
                if (into < 0.0) ball.velocity = ball.velocity - normal * into;
            }
        }
        {
            // The dot is a surface when the ball is sitting on it, and a load when
            // it is sitting on the ball. Only the first can hold the ball up.
            const Vec3 gap = ball.position - point_;
            const double distance = length(gap);
            if (distance > 0.0 && distance <= ball.radius + kPointRadius + 1e-9) {
                const Vec3 normal = gap / distance;
                ball.touching = true;
                if (normal.y > 1e-9) ball.standing = ball.standing && holds(normal);
                const double into = dot(ball.velocity - velocity_, normal);
                if (into < 0.0) ball.velocity = ball.velocity - normal * into;
            }
        }
        // The floor has the last word: nothing in this room can be below it, so a
        // ball pressed down by something above it -- or pressed into a body it is
        // lying on -- is left standing still rather than holding a speed it has
        // nowhere to spend.
        if (ball.position.y <= ball.radius + 1e-9) {
            ball.touching = true;
            ball.standing = ball.standing && holds(Vec3{0, 1, 0});
            if (ball.velocity.y < 0.0) ball.velocity.y = 0.0;
        }
    }

    // Every face the tick met, solved against the others: what is left of the ball's
    // speed and its turn is what their grips all allow at once.
    settleBallFriction();

    // A whole tick of travel below a picometre is not motion a double can carry:
    // that is the floor of what this simulation can represent, not a speed chosen
    // out of the air. Below it the ball is at rest.
    if (length(ball.velocity) < kRestSpeed) ball.velocity = {};

    // At rest means at rest: no speed left, and every surface it is sitting on
    // able to hold it. A slope cannot hold a ball, so a ball on one is never
    // frozen, however slowly it is going.
    if (ball.touching && length(ball.velocity) < kRestSpeed && ball.standing) {
        ball.frozen = true;
        ball.velocity = {};
        ball.spin = 0.0;
        ball.previous = ball.position;
        ball.shown = ball.position;
    }
    ball.angle += ball.spin * kTick;
}

// The ball against something.// Whether the ball can be put down at this spot. It is the mouse that needs this,
// not the physics: the ball is carried, not simulated, while it is held, and a
// ball put down inside a solid would have to be pushed back out -- which is not
// something this room ever does, because nothing ever gets in. So the put-down
// itself is where the rule belongs: a spot the ball's circle would not fit in is
// simply not a spot it can be put.
bool PlaygroundRoom::ballPlacementClear(Vec3 at) const {
    if (at.y < ball_.radius - 1e-9) return false;
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
            if (length(at - (a + side * t)) < ball_.radius) return false;
        }
    }
    return true;
}

}  // namespace gtc::native}  // namespace gtc::native
