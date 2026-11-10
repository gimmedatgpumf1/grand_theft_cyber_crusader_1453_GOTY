#include "client/playground_room.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {
using namespace playground_detail;

// The ball: one point with a radius, and spin. It is not made of corner points
// and there is nothing sampled on its surface -- it is a single body whose only
// shape is how far it reaches.
//
// Its spin is not decoration. Friction acts at the bottom of the ball, one radius
// from its middle, so the same force that slows it down also turns it. A sphere
// that could not turn would be slowed like a brick instead of rolling.
//
// Everything below is either geometry -- where a circle and an outline first
// touch, worked out before anything moves -- or one of the physical quantities a
// ball actually has: its mass, how hard it is to spin up, how much of an impact
// it gives back, its friction with the floor, and the air it pushes through.
// There is no speed at which one rule turns into another.

namespace {

constexpr double kTick = 1.0 / 20.0;
constexpr double kGravity = 9.8;
constexpr double kMass = 1.0;  // the dot's mass, and a body corner's
constexpr double kSurfaceY = 0.0;
// The same contact guard the dot uses: a walk that keeps finding contacts stops
// here rather than looping.
constexpr int kMaxContacts = 4;

// The ball's material. Rubber on a wooden floor grips, which is why a ball rolls
// instead of skidding for ever.
constexpr double kBallStaticFriction = 0.70;
constexpr double kBallKineticFriction = 0.55;
// A basketball is a gas-filled skin: it gives back three quarters of the speed it
// lands with, which is a little over half the height.
constexpr double kBallRestitution = 0.75;
// Air, from the drag equation: half the density of air, the drag coefficient of a
// sphere, and the area the ball presents. F = 0.5 * rho * Cd * A * v^2.
constexpr double kAirDensity = 1.225;  // kg/m^3
constexpr double kBallDragCoefficient = 0.47;
constexpr double kPi = 3.14159265358979323846;

// Where a circle's path first reaches a moving convex outline. Both sides travel
// in straight lines for the length of one tick, so taking the outline's travel
// off the circle's leaves the same question -- do these two meet -- and the
// answer is exact. This is the whole of the ball's collision detection: a circle
// against a polygon has exactly two kinds of contact, a corner and a side.
bool circleHitsOutline(Vec3 from, Vec3 travel, Vec3 a, Vec3 aTravel, Vec3 b, Vec3 bTravel, double radius,
                       double& along, Vec3& normal) {
    double best = 2.0;
    Vec3 bestNormal{0, 1, 0};
    // Corner circles.
    const Vec3 corners[2] = {a, b};
    const Vec3 cornerTravels[2] = {aTravel, bTravel};
    for (int i = 0; i < 2; ++i) {
        double hit = 2.0;
        Vec3 facing{0, 1, 0};
        const Vec3 relative = travel - cornerTravels[i];
        if (segmentHitsCircle(from, from + relative, corners[i], radius, hit, facing) && hit < best) {
            best = hit;
            bestNormal = facing;
        }
    }
    // Reaching a side. A ball does not have to get its middle to the surface: it
    // touches when its middle is a radius away, so the side is looked at where the
    // ball's own surface would meet it, one radius further out.
    const Vec3 side = b - a;
    const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
    if (sideLength > 1e-12) {
        const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        const Vec3 relative = travel - (aTravel + bTravel) * 0.5;
        const double start = dot(from - a, outward) - radius;
        const double end = dot(from + relative - a, outward) - radius;
        // A ball lying exactly on a side is at the surface, not outside it: it is
        // touching whatever the thing is doing, so a press counts as a contact.
        if (start >= -1e-9 && end <= 0.0 && start - end > 1e-12) {
            const double crossing = std::clamp(start / (start - end), 0.0, 1.0);
            const Vec3 at = from + relative * crossing;
            const double position = dot(at - a, side) / (sideLength * sideLength);
            if (position >= 0.0 && position <= 1.0 && crossing < best) {
                best = crossing;
                bestNormal = outward;
            }
        }
    }
    if (best > 1.0) return false;
    along = std::max(0.0, best);
    normal = bestNormal;
    return true;
}

}  // namespace

// A ball that is asleep is not stepped, but only while nothing can reach it. The
// dot and every body count: if any of them is moving, the ball is looked at.
bool PlaygroundRoom::ballNeighboursMoving() const {
    if (!pointFrozen_ && length(velocity_) > 0.0) return true;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        if (!bodies_[index].frozen) return true;
    }
    return false;
}

void PlaygroundRoom::tickBall() {
    Ball& ball = ball_;
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

    // Forces for this tick: weight, and the air it has to push through.
    if (!held) ball.velocity.y -= kGravity * kTick;
    if (!held) {
        const double speed = length(ball.velocity);
        if (speed > 0.0) {
            const double area = kPi * ball.radius * ball.radius;
            const double force = 0.5 * kAirDensity * kBallDragCoefficient * area * speed * speed;
            const double loss = force / ball.mass * kTick;
            ball.velocity = ball.velocity * (speed <= loss ? 0.0 : (speed - loss) / speed);
        }
    }

    ball.touching = false;
    ball.standing = true;
    // Which surfaces have had their answer this tick. Two surfaces can be met at
    // the same instant over the same distance -- the floor and a face coming down
    // onto a ball lying on it are met at exactly zero -- and without this the one
    // the search happens to look at first would be found again and again while the
    // other was never answered at all.
    std::array<int, kMaxContacts> answered{};
    int answeredCount = 0;
    const auto alreadyAnswered = [&answered, &answeredCount](int what) {
        for (int i = 0; i < answeredCount; ++i) {
            if (answered[std::size_t(i)] == what) return true;
        }
        return false;
    };
    double remaining = 1.0;
    for (int step = 0; step < kMaxContacts && remaining > 1e-12; ++step) {
        const Vec3 next = ball.position + ball.velocity * (kTick * remaining);
        const Vec3 travel = next - ball.position;
        double first = 2.0;
        Vec3 normal{0, 1, 0};
        int what = -1;  // -2 the floor, -3 the dot, otherwise a body
        if (next.y < ball.radius && travel.y < 0.0) {
            // Not negative zero: a distance of zero along a downward step divides
            // out to -0, and -0 loses every "did I get there first" comparison it
            // takes part in, so an exact hit would be passed over.
            first = std::max(0.0, std::clamp((ball.radius - ball.position.y) / travel.y, 0.0, 1.0));
            normal = {0, 1, 0};
            what = -2;
        }
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            for (std::size_t edge = 0; edge < body.count; ++edge) {
                const std::size_t top = (edge + 1) % body.count;
                double along = 2.0;
                Vec3 facing{0, 1, 0};
                // How far the body's own corners travelled this tick. The bodies
                // are stepped first, so this is not a prediction: it is exactly
                // what they did, and the ball's own travel is tested against it.
                if (!circleHitsOutline(ball.position, travel, body.previous[edge],
                                       body.position[edge] - body.previous[edge], body.previous[top],
                                       body.position[top] - body.previous[top], ball.radius, along, facing))
                    continue;
                // Touching and getting no closer is touching, not arriving -- the
                // same rule the dot follows. What matters is the closing speed of
                // the two of them, not of the ball alone: a ball lying still under a
                // face coming down on it is being arrived at, and the face is doing
                // the arriving.
                const Vec3 relativeVelocity =
                    ball.velocity - (body.velocity[edge] + body.velocity[top]) * 0.5;
                if (along <= 1e-9 && dot(relativeVelocity, facing) >= 0.0) continue;
                if (alreadyAnswered(int(index))) continue;
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
                !(along <= 1e-9 && dot(ball.velocity - velocity_, facing) >= 0.0) && along < first &&
                !alreadyAnswered(-3)) {
                first = along;
                normal = facing;
                what = -3;
            }
        }
        if (first > 1.0) {
            ball.position = next;
            break;
        }
        ball.position = ball.position + travel * first;
        remaining *= 1.0 - first;
        ball.touching = true;
        if (answeredCount < kMaxContacts) answered[std::size_t(answeredCount++)] = what;
        if (what == -2) {
            ballContact(normal, -1);
        } else if (what == -3) {
            hitBallWithPoint(normal);
        } else {
            ballContact(normal, what, first, held);
        }
        if (length(ball.velocity) <= 0.0) break;
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
                if (normal.y > 0.0) ball.standing = ball.standing && holds(normal);
                // The surface is where the ball is, so nothing can be driven into
                // it. Anything left over from an earlier contact in the same tick --
                // a body pressing the ball down onto the floor -- is answered here,
                // which is what lets a ball carry a weight instead of humming.
                const Vec3 closing = ball.velocity - (body.velocity[edge] + body.velocity[top]) * 0.5;
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
                if (normal.y > 0.0) ball.standing = ball.standing && holds(normal);
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

// The ball against something. The normal part is answered by both sides: if the
// other side can move, the ball keeps whatever it did not hand over. The rest of
// the contact is friction, and because it acts at the bottom of the ball rather
// than at its middle it turns the ball as well as slowing it.
void PlaygroundRoom::ballContact(Vec3 normal, int bodyIndex, double along, bool ballHeld) {
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
        // The floor carries a body standing on it, so the body cannot be driven
        // down into the floor -- a ball resting on top of the cube presses it down
        // and the floor answers that, not the cube. Anything else the body is free
        // to do, and being lifted is one of those things: a body coming down on a
        // ball has to be able to stop on it. A body held by the mouse has nowhere
        // to go at all.
        const bool drivenDown = normal.y > 0.0;
        answer = dragTarget_ == DragTarget::Body ? Vec3{} : (supported && drivenDown ? Vec3{normal.x, 0, 0} : normal);
        // What the body can do about this contact is only the part of its freedom
        // that lies along the normal. A face is never exactly level in floating
        // point, so its normal carries a horizontal part of 1e-16; taking that for
        // a body that can move made a cube standing on the floor share a blow with
        // a ball resting on top of it, over and over, and never settle. The same
        // dot is what the impulse below is solved against, so the two of them are
        // always talking about the same thing.
        canMove = dot(answer, normal) > 1e-9;
    }
    // Both sides stop where they met. The bodies are stepped before the ball, so
    // the body's corners have already been committed to where they ended up; the
    // crossing says how far along its own step that meeting was. The body is put
    // back there, and its corners carry what that shortened step says they are
    // doing plus the blow they took -- the step is travel, the blow is momentum,
    // and a body whose travel was cut short by a ball has to keep both.
    std::array<Vec3, kMaxCorners> handed{};
    const Vec3 surfaceVelocity = canMove ? body->velocity[struck] : Vec3{};
    const double arrivedSpeed = dot(ball.arrival - surfaceVelocity, normal);
    const double closing = dot(ball.velocity - surfaceVelocity, normal);
    // A bounce is an answer to arriving at something that is not going to give
    // way: the floor, or a solid that cannot move. Against something that is
    // itself moving -- a body falling on the ball, the dot landing on it -- the
    // two exchange momentum and nothing springs back, which is also the only way
    // two things stepped one after the other can stay consistent. A ball that
    // arrived at nothing is pressed into the surface rather than hitting it, so it
    // does not hop; no speed has to be invented to tell those apart either.
    const double rebound = (!canMove && arrivedSpeed < 0.0) ? -kBallRestitution * arrivedSpeed : 0.0;
    double pressed = 0.0;
    if (closing < 0.0) {
        constexpr double kStruckShare = 0.5;
        if (canMove) {
            // Solved so that the two of them are left with no closing speed at all
            // along the normal. The body answers only part of the normal -- a body
            // on the floor gives nothing downwards -- and the share it can answer
            // is exactly what the impulse has to be divided by. Getting this wrong
            // leaves the ball still driving into the face after the contact has
            // been answered, so the same contact is found again and answered again
            // without the ball ever going anywhere.
            //
            // The ball has the same question to answer about itself. A ball lying
            // on the floor with a body coming down on it cannot give way: the floor
            // answers its weight, so there is nowhere for the momentum to go and it
            // is the body that has to stop. Letting the ball take a share it cannot
            // keep leaves the body still falling with a speed nothing took off it.
            const double answered = dot(answer, normal);
            const double ballShare = ballHeld ? 0.0 : 1.0;
            const double impulse =
                (rebound - closing) / (ballShare / ball.mass + kStruckShare * answered / kMass);
            ball.velocity = ball.velocity + normal * (impulse * ballShare / ball.mass);
            const double rest = (1.0 - kStruckShare) / static_cast<double>(body->count - 1);
            for (std::size_t i = 0; i < body->count; ++i) {
                const double share = i == struck ? kStruckShare : rest;
                handed[i] = answer * -(impulse * share / kMass);
            }
            body->frozen = false;
            pressed = impulse;
        } else {
            // Nowhere to go: the ball takes the whole answer itself.
            const double impulse = (rebound - closing) * ball.mass;
            ball.velocity = ball.velocity + normal * (impulse / ball.mass);
            pressed = impulse;
        }
    }
    // The body is only put back when the meeting was inside its step, or when it
    // is the one travelling into the ball -- a face already lying against the ball
    // that is being pushed away from it must be allowed to go, or the contact keeps
    // undoing the very motion it just handed over and the body can never be moved
    // by the ball at all.
    const bool surfaceMovingIn = dot(surfaceVelocity, normal) > 0.0;
    if (canMove && along < 1.0 && (along > 1e-9 || surfaceMovingIn)) {
        for (std::size_t i = 0; i < body->count; ++i) {
            body->position[i] = body->previous[i] + (body->position[i] - body->previous[i]) * along;
        }
    }
    if (canMove) {
        // What the body is left doing is what it was doing plus the blow it took.
        // Its speed must not be read back off the shortened step: the step was cut
        // short by the ball, and reading it back would hand the body the whole fall
        // again on the next tick and let it through.
        for (std::size_t i = 0; i < body->count; ++i) body->velocity[i] = body->velocity[i] + handed[i];
    }

    // A contact the ball is already at when the tick starts -- no travel to reach
    // it -- is a surface it is lying against, and that surface is where the ball
    // is. Whatever is left of the ball's own speed pointing into it is therefore
    // speed it cannot spend, and leaving it there would have the sweep find the
    // same contact again and again at zero distance while the ball, touching, is
    // never allowed to travel along the face it is touching. A bounce has already
    // turned the ball around by the time this runs, so nothing that was going to
    // come off the surface is touched. The motion it keeps is the surface's.
    if (along <= 1e-9) {
        const double into = dot(ball.velocity - surfaceVelocity, normal);
        if (into < 0.0) ball.velocity = ball.velocity - normal * into;
    }

    // Can this surface hold the ball still? That is the whole of static friction:
    // mu_s * N against the pull along the surface. The floor can, a slope cannot.
    ball.standing = ball.standing &&
                    kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));

    // Kinetic friction, limited by mu * N, with N the impulse this contact carried.
    // It acts at the bottom of the ball, so it also turns it: a skid spins the ball
    // up until the bottom stops slipping, and from then on the turn is the travel.
    const Vec3 tangent{normal.y, -normal.x, 0};
    const Vec3 arm = normal * -ball.radius;
    const double inertia = 0.4 * ball.mass * ball.radius * ball.radius;  // a solid sphere
    const Vec3 spinSpeed{-ball.spin * arm.y, ball.spin * arm.x, 0};
    const double slip = dot(ball.velocity + spinSpeed - surfaceVelocity, tangent);
    const double lever = arm.x * tangent.y - arm.y * tangent.x;
    if (pressed > 0.0 && slip != 0.0) {
        const double effective = 1.0 / ball.mass + lever * lever / inertia;
        const double wanted = -slip / effective;
        // Two coefficients, as everywhere else in this room. If the grip can take
        // the slip away outright the ball rolls from here -- the contact holds, and
        // nothing is left slipping. If it cannot, the ball slides and mu_k * N acts
        // against the slide, over time, never in one instant.
        const bool holds = std::abs(wanted) <= kBallStaticFriction * pressed;
        const double limit = (holds ? kBallStaticFriction : kBallKineticFriction) * pressed;
        const double impulse = holds ? wanted : (wanted > 0.0 ? limit : -limit);
        ball.velocity = ball.velocity + tangent * (impulse / ball.mass);
        ball.spin += lever * impulse / inertia;
    }
}

// The ball runs into the dot. Both have mass, so they exchange momentum along the
// line between them.
void PlaygroundRoom::hitBallWithPoint(Vec3 normal) {
    Ball& ball = ball_;
    const double closing = dot(ball.velocity - velocity_, normal);
    // Nothing springs back between two things that are both free to move.
    const double rebound = 0.0;
    if (closing < 0.0) {
        // A dot that is not free to move -- frozen, or held by the mouse -- cannot
        // take a share of the blow, and giving it one only leaves the ball with a
        // speed the dot can never have given it.
        const double pointShare = pointFrozen_ || dragTarget_ == DragTarget::Point ? 0.0 : 1.0;
        const double impulse = (rebound - closing) / (1.0 / ball.mass + pointShare / kMass);
        ball.velocity = ball.velocity + normal * (impulse / ball.mass);
        velocity_ = velocity_ - normal * (impulse * pointShare / kMass);
        ball.frozen = false;
        pointFrozen_ = false;
        resting_ = false;
        restingBody_ = -1;
    }
    ball.standing = ball.standing &&
                    kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));
    // Separate them, so they are not still overlapping next tick.
    const Vec3 gap = ball.position - point_;
    const double distance = length(gap);
    const double overlap = kPointRadius + ball.radius - distance;
    if (overlap > 0.0 && distance > 1e-12) {
        ball.position = ball.position + gap * (overlap * 0.5 / distance);
        point_ = point_ - gap * (overlap * 0.5 / distance);
    }
}

// The dot runs into the ball. The normal points from the ball to the dot, so the
// dot is the one that gets pushed back, and whatever it hands over is what the
// ball takes.
void PlaygroundRoom::pointAgainstBall(Vec3 normal) {
    Ball& ball = ball_;
    const double closing = dot(velocity_ - ball.velocity, normal);
    const double rebound = 0.0;
    if (closing < 0.0) {
        // A ball that a surface is already holding cannot give way: the dot is
        // stopped by the ball, not by half of it. Dividing the blow with a ball
        // that has nowhere to go leaves the dot falling into it a little more
        // every tick, which is what turns a dot resting on a ball into a dot
        // slowly winding it up.
        const bool ballGives = !ball.frozen && !(ball.touching && ball.standing && length(ball.velocity) < kRestSpeed);
        const double ballShare = ballGives ? 1.0 : 0.0;
        const double impulse = (rebound - closing) / (1.0 / kMass + ballShare / ball.mass);
        velocity_ = velocity_ + normal * (impulse / kMass);
        ball.velocity = ball.velocity - normal * (impulse * ballShare / ball.mass);
        ball.frozen = false;
        pointFrozen_ = false;
        resting_ = false;
        restingBody_ = -1;
    }
    const Vec3 gap = point_ - ball.position;
    const double distance = length(gap);
    const double overlap = kPointRadius + ball.radius - distance;
    if (overlap > 0.0 && distance > 1e-12) {
        point_ = point_ + gap * (overlap * 0.5 / distance);
        ball.position = ball.position - gap * (overlap * 0.5 / distance);
    }
}

// A body's own step can carry one of its corners into the ball after the ball has
// been stepped, because a corner is a point and the floor is the only thing that
// stops it. That overlap is answered as a contact -- the same code, so momentum
// still changes hands -- and the ball is then put back on the surface. Nothing
// invents a speed here: a ball that has been pushed out is placed, and what it
// does next follows from the contact and from gravity.
void PlaygroundRoom::ballOutOfSolids() {
    Ball& ball = ball_;
    if (ball.position.y < ball.radius) {
        ball.position.y = ball.radius;
        if (ball.velocity.y < 0.0) ball.velocity.y = 0.0;
    }
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        double nearest = ball.radius;
        Vec3 way{0, 1, 0};
        for (std::size_t edge = 0; edge < body.count; ++edge) {
            const Vec3 a = body.position[edge];
            const Vec3 b = body.position[(edge + 1) % body.count];
            const Vec3 side = b - a;
            const double sideLength = side.x * side.x + side.y * side.y;
            const double t = sideLength > 0.0 ? std::clamp(dot(ball.position - a, side) / sideLength, 0.0, 1.0) : 0.0;
            const Vec3 gap = ball.position - (a + side * t);
            const double distance = length(gap);
            if (distance < nearest) {
                nearest = distance;
                if (distance > 0.0) way = gap / distance;
            }
        }
        if (nearest >= ball.radius - 1e-12) continue;
        // Only put it back where it belongs. The contact has already been answered
        // by the walk, with the momentum that goes with it; answering it a second
        // time here would take a second blow out of the same meeting.
        ball.position = ball.position + way * (ball.radius - nearest);
        const double into = dot(ball.velocity, way);
        if (into < 0.0) ball.velocity = ball.velocity - way * into;
        ball.frozen = false;
    }
}

}  // namespace gtc::native
