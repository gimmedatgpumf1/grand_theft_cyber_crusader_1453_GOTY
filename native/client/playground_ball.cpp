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
// Rolling resistance, F = Crr * N. Rubber on a wooden floor: the ball does not
// roll for ever on a level floor, and this is the coefficient that says how fast
// a roll dies away. It is a property of the pair of materials, measured like mu,
// not a speed below which something changes.
constexpr double kBallRollingResistance = 0.010;
// A basketball is a gas-filled skin: it gives back three quarters of the speed it
// lands with, which is a little over half the height.
constexpr double kBallRestitution = 0.75;
// Air, from the drag equation: half the density of air, the drag coefficient of a
// sphere, and the area the ball presents. F = 0.5 * rho * Cd * A * v^2.
constexpr double kAirDensity = 1.225;  // kg/m^3
constexpr double kBallDragCoefficient = 0.47;
constexpr double kPi = 3.14159265358979323846;

// Is this point inside the outline? The same test the point uses to decide it
// has been left inside a body: it is inside when it is on the inner side of every
// side. Nothing is assumed about the order the sides are wound in beyond that.
bool insideOutline(Vec3 point, const Vec3* corners, std::size_t count) {
    for (std::size_t edge = 0; edge < count; ++edge) {
        const Vec3 a = corners[edge];
        const Vec3 b = corners[(edge + 1) % count];
        const Vec3 side = b - a;
        const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
        if (!(sideLength > 1e-12)) return false;
        const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        if (dot(point - a, outward) > 0.0) return false;
    }
    return true;
}

// One meeting of the ball's circle with one edge of a body's outline.
//
// This is the whole of the ball's collision detection, and it is a hitscan: the
// circle is swept along the tick's travel against the edge and against the two
// corner circles at its ends, and what comes back is the first instant they meet.
// A flat side is met where the ball's own surface reaches it -- one radius out
// from the ball's middle, not where the middle would cross the line -- and a
// corner is met when the middle is one radius from it. A circle against a convex
// outline has exactly those two kinds of contact, so nothing here is sampled,
// stepped or guessed.
//
// `depth` is the case with no crossing to find: the ball's surface is already past
// that piece of the outline when the tick starts. That is reported at once, with
// how far through it is, so it can be put back on the surface and stopped from
// going further. It is the same test saying "you are touching already" rather
// than "you will touch" -- not a separate pass over the room afterwards.
// How close two surfaces have to be before they are touching rather than merely
// near. It is a rounding allowance, not a distance anything is allowed to sink: a
// ball whose surface is on a face sits at exactly one radius from it, and the
// arithmetic that got it there is good to about this much.
constexpr double kTouch = 1e-9;

struct OutlineHit {
    double along = 2.0;    // where in the tick, 0..1
    Vec3 normal{0, 1, 0};  // which way the surface faces, out of the body
    Vec3 contact{};        // where the two surfaces touch
    double depth = 0.0;    // how far the ball's surface is already through it
};

// How far a point is from one edge, where the closest point is, and which way the
// edge faces. Being on the inner side of an edge's line is not the same as
// touching that edge: the far side of a triangle is on the inner side of a ball
// resting against the slope, and taking the line for the edge is what had the
// opposite face grinding the speed off a rolling ball.
void distanceToEdge(Vec3 point, Vec3 a, Vec3 b, double& distance, Vec3& closest, Vec3& outward) {
    const Vec3 side = b - a;
    const double length2 = dot(side, side);
    const double t = length2 > 0.0 ? std::clamp(dot(point - a, side) / length2, 0.0, 1.0) : 0.0;
    closest = a + side * t;
    distance = length(point - closest);
    const double sideLength = std::sqrt(length2);
    outward = sideLength > 1e-12 ? Vec3{side.y / sideLength, -side.x / sideLength, 0} : Vec3{0, 1, 0};
}

bool circleHitsOutline(Vec3 from, Vec3 travel, Vec3 a, Vec3 aTravel, Vec3 b, Vec3 bTravel, double radius,
                       Vec3 inside, OutlineHit& hit) {
    bool found = false;
    const auto take = [&hit, &found](double along, Vec3 normal, Vec3 contact, double depth) {
        if (!found || along < hit.along) {
            hit.along = along;
            hit.normal = normal;
            hit.contact = contact;
            hit.depth = depth;
            found = true;
        }
    };
    // The two ends of the edge, as circles of the ball's radius: a corner is met
    // when the ball's middle comes within one radius of it. Both travel in straight
    // lines inside a tick, so taking the corner's own travel off the ball's leaves
    // a straight path against a fixed circle -- exact.
    const Vec3 corners[2] = {a, b};
    const Vec3 cornerTravels[2] = {aTravel, bTravel};
    for (int i = 0; i < 2; ++i) {
        const Vec3 relative = travel - cornerTravels[i];
        const Vec3 offset = from - corners[i];
        const double start = dot(offset, offset) - radius * radius;
        if (start < -kTouch * radius) {
            // Already within a radius of the corner: touching, with the corner
            // showing through the ball's surface by this much.
            const double distance = std::sqrt(dot(offset, offset));
            const Vec3 normal = distance > 1e-12 ? offset / distance : Vec3{0, 1, 0};
            take(0.0, normal, corners[i] - normal * radius, radius - distance);
            continue;
        }
        const double quadA = dot(relative, relative);
        if (!(quadA > 1e-18)) continue;
        const double quadB = 2.0 * dot(offset, relative);
        const double discriminant = quadB * quadB - 4.0 * quadA * start;
        if (discriminant < 0.0) continue;
        const double root = (-quadB - std::sqrt(discriminant)) / (2.0 * quadA);
        if (root < 0.0 || root > 1.0) continue;
        const Vec3 at = offset + relative * root;
        const double gap = length(at);
        const Vec3 normal = gap > 1e-12 ? at / gap : Vec3{0, 1, 0};
        take(root, normal, corners[i] + cornerTravels[i] * root - normal * radius, 0.0);
    }
    // Reaching a side. A ball does not have to get its middle to the surface: it
    // touches when its middle is one radius away, so the side is met where the
    // ball's own surface would reach it, one radius further out.
    const Vec3 side = b - a;
    const double sideLength = std::sqrt(dot(side, side));
    if (sideLength > 1e-12) {
        // Which side of this edge is outside: the body's own middle is inside, so
        // the outward normal is the one the middle is not on.
        Vec3 outward = Vec3{side.y / sideLength, -side.x / sideLength, 0};
        if (dot(inside - a, outward) > 0.0) outward = outward * -1.0;
        const Vec3 relative = travel - (aTravel + bTravel) * 0.5;
        const double start = dot(from - a, outward) - radius;
        const double end = dot(from + relative - a, outward) - radius;
        // From touching (start exactly zero) is still reaching the face: a ball
        // sliding along a slope it is already resting on starts every tick at
        // exactly one radius from it, and a strict "more than zero" test would
        // leave that tick's walk with no face at all and let gravity push the ball
        // a little further in.
        if (start >= -kTouch && end < 0.0 && start - end > 1e-12) {
            const double crossing = std::clamp(start / (start - end), 0.0, 1.0);
            const Vec3 at = from + relative * crossing;
            const double position = dot(at - a, side) / dot(side, side);
            if (position >= 0.0 && position <= 1.0) {
                // Where a circle touches a face: straight in from its own middle,
                // one radius. Not a corner -- the arm from the body's middle to this
                // point is what a blow turns it by, and a corner arm spins a body
                // that was hit fair in the face.
                take(crossing, outward, at - outward * radius, 0.0);
            }
        } else if (start < -kTouch) {
            // The ball's surface is already past this edge's plane. It is a contact
            // only if the ball is opposite this edge and not another, so the
            // distance to the edge itself decides, not the distance to its line.
            double distance = 0.0;
            Vec3 closest{}, facing{};
            distanceToEdge(from, a, b, distance, closest, facing);
            if (distance < radius - kTouch) take(0.0, outward, from - outward * radius, radius - distance);
        }
    }
    return found;
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
    double remaining = 1.0;
    for (int step = 0; step < kMaxContacts && remaining > 1e-12; ++step) {
        const Vec3 next = ball.position + ball.velocity * (kTick * remaining);
        const Vec3 travel = next - ball.position;
        double first = 2.0;
        double depth = 0.0;
        Vec3 normal{0, 1, 0};
        Vec3 contactPoint = ball.position;
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
                // How far the body's own corners travelled this tick. The bodies are
                // stepped first, so this is not a prediction: it is what they did,
                // and the ball's own travel is tested against it. A corner's stored
                // velocity is cut to nothing by the floor while its step still
                // carries the body down into the ball, so the step is what counts.
                OutlineHit hit;
                if (!circleHitsOutline(ball.position, travel, body.previous[edge],
                                       body.position[edge] - body.previous[edge], body.previous[top],
                                       body.position[top] - body.previous[top], ball.radius,
                                       centroid(body.position.data(), body.count), hit))
                    continue;
                // Touching and getting no closer is touching, not arriving. A ball
                // already through the outline is never skipped: that is inside, and
                // it has to be answered.
                if (hit.depth <= 0.0 && hit.along <= 1e-9) {
                    const Vec3 stepFrom = body.position[edge] - body.previous[edge];
                    const Vec3 stepTo = body.position[top] - body.previous[top];
                    const Vec3 surfaceVelocity = (stepFrom + stepTo) * (0.5 / kTick);
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
        }
        if (first > 1.0) {
            ball.position = next;
            break;
        }
        ball.position = ball.position + travel * first;
        remaining *= 1.0 - first;
        ball.touching = true;
        if (what == -2) {
            ballContact(normal, -1);
        } else if (what == -3) {
            hitBallWithPoint(normal);
        } else {
            ballContact(normal, what, first, held, contactPoint, depth);
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
                if (normal.y > 1e-9) ball.standing = ball.standing && holds(normal);
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
void PlaygroundRoom::ballContact(Vec3 normal, int bodyIndex, double along, bool ballHeld, Vec3 contactPoint,
                                double depth) {
    Ball& ball = ball_;
    Body* body = bodyIndex >= 0 ? &bodies_[std::size_t(bodyIndex)] : nullptr;
    std::size_t struck = 0;
    Vec3 answer = normal;
    bool canMove = false;
    // The sweep already prevented this from happening to a moving ball, so a ball
    // found through the outline is a ball something else was driven into. The ball
    // goes back the exact amount it is through, along the surface's own normal, and
    // what is left of its speed pointing in is not speed it can spend. There is no
    // search for the shortest way out: the surface it is through says which way and
    // how far.
    if (depth > 0.0) {
        // Out along the surface's own normal, by exactly the amount it is through.
        // Unless that way is the floor: a ball under a body that is coming down on
        // it has nowhere to go downwards, and the body is the one with room, so the
        // body is lifted onto the ball's surface instead. Either way the two end the
        // tick touching, never through -- and the lift is taken out of the body's
        // step as well, so it is a correction and not a jump to be read back as
        // momentum.
        const bool intoFloor = normal.y < -1e-9 && ball.position.y <= ball.radius + 1e-9;
        if (intoFloor && body != nullptr) {
            for (std::size_t i = 0; i < body->count; ++i) {
                body->position[i] = body->position[i] - normal * depth;
                body->previous[i] = body->previous[i] - normal * depth;
                body->shown[i] = body->shown[i] - normal * depth;
            }
            contactPoint = contactPoint - normal * depth;
        } else {
            ball.position = ball.position + normal * depth;
        }
        const double into = dot(ball.velocity, normal);
        if (into < 0.0) ball.velocity = ball.velocity - normal * into;
    }
    // A body at rest is not stepped, and what it is not stepping it cannot give
    // either. Letting it take a share of the ball's momentum and then discarding
    // that share is not a contact, it is a pump: the ball keeps more speed than a
    // solid surface would leave it and ends up rolling under whatever is on top of
    // it faster and faster. So a resting body answers as the solid it is, and a
    // real blow wakes it -- which is the only thing that should ever move a body
    // that had come to rest.
    if (body != nullptr && body->frozen) {
        const Vec3 closingVelocity = ball.velocity - (body->position[struck] - body->previous[struck]) * (1.0 / kTick);
        if (std::abs(dot(closingVelocity, normal)) > kRestSpeed) body->frozen = false;
    }
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
        answer = (dragTarget_ == DragTarget::Body || body->frozen)
                     ? Vec3{}
                     : (supported && drivenDown ? Vec3{normal.x, 0, 0} : normal);
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
    // What the struck corner actually did this tick. Its stored velocity can be
    // zero -- the floor cuts it when the corner lands -- while the corner is still
    // travelling down into the ball, and the ball has to answer the travel.
    const Vec3 surfaceVelocity =
        canMove ? (body->position[struck] - body->previous[struck]) * (1.0 / kTick) : Vec3{};
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
            // A body struck by the ball takes the blow as the solid it is: the
            // impulse reaches the corner that was struck, and all four corners
            // move as one rigid shape -- together, and turning about the middle by
            // the amount that blow would turn it. Sharing it out unevenly corner by
            // corner is what had one corner stop dead while the other three kept
            // falling, which the fixed distances then had to fight, and what turns
            // a square being landed on into a shape that shudders and shears.
            const double answered = dot(answer, normal);
            const Vec3 middle = centroid(body->position.data(), body->count);
            // Where the two shapes are touching, not the nearest corner: the ball
            // meeting the middle of a face turns the body by nothing, and a corner
            // of a face is not where the blow lands.
            const Vec3 arm = contactPoint - middle;
            const double lever = arm.x * answer.y - arm.y * answer.x;
            const double inertia = [] (const Body& b, Vec3 centre) {
                double sum = 0.0;
                for (std::size_t i = 0; i < b.count; ++i) {
                    const Vec3 r = b.position[i] - centre;
                    sum += kMass * dot(r, r);
                }
                return sum;
            }(*body, middle);
            const double bodyShare = 1.0 / (static_cast<double>(body->count) * kMass) +
                                     lever * lever / (inertia > 0.0 ? inertia : 1.0);
            const double ballShare = ballHeld ? 0.0 : 1.0;
            const double impulse = (rebound - closing) / (ballShare / ball.mass + bodyShare);
            ball.velocity = ball.velocity + normal * (impulse * ballShare / ball.mass);
            const Vec3 push = answer * -(impulse / (static_cast<double>(body->count) * kMass));
            const Vec3 turn{0, 0, (impulse) * lever / (inertia > 0.0 ? inertia : 1.0)};
            for (std::size_t i = 0; i < body->count; ++i) {
                const Vec3 r = body->position[i] - middle;
                // Translation of the whole body plus the turn about its middle:
                // every corner of a solid moves together.
                const Vec3 rotate{-turn.z * r.y, turn.z * r.x, 0};
                handed[i] = push + rotate;
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
    // Only a surface that is underneath the ball is asked: a ball leaning on the
    // cube's upright side is held up by the floor, and letting that side answer for
    // the floor dropped a ball resting against it every other tick.
    // A face is never exactly level in floating point, so a normal that is level to
    // within this much is a wall, not a floor: it carries nothing, and asking it to
    // carry the ball made a ball resting against the cube drop and bounce every
    // other tick.
    if (normal.y > 1e-9)
        ball.standing = ball.standing &&
                        kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));

    // Contact friction and rolling resistance take energy out of a ball, they never
    // put it in: they are losses, and a solve that handed a ball more energy than it
    // arrived with would be inventing motion. A ball squeezed between two surfaces
    // can be made to gain a little every tick by two exact slip corrections taking
    // turns, and this is what stops that -- the loss is kept, the gain is not.
    const Vec3 velocityBeforeFriction = ball.velocity;
    const double spinBeforeFriction = ball.spin;
    const double energyBeforeFriction =
        0.5 * ball.mass * dot(ball.velocity, ball.velocity) +
        0.5 * (0.4 * ball.mass * ball.radius * ball.radius) * ball.spin * ball.spin;

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

    // Rolling resistance, F = Crr * N, at the same contact and in the same way:
    // the normal force presses the ball into the surface, the surface does not
    // give that back perfectly, and the loss acts against the way the ball is
    // going. It is applied through the same lever as friction, so a rolling ball
    // slows and its turn slows with it and it goes on rolling; it is not a rule
    // that switches anything off, and it does nothing to a ball that is not
    // touching anything. N here is exactly the impulse this contact carried.
    if (pressed > 0.0) {
        const double travel = dot(ball.velocity - surfaceVelocity, tangent);
        if (travel != 0.0) {
            const double limit = kBallRollingResistance * pressed;
            const double wanted = -travel / (1.0 / ball.mass + lever * lever / inertia);
            const double impulse = std::clamp(wanted, -limit, limit);
            ball.velocity = ball.velocity + tangent * (impulse / ball.mass);
            ball.spin += lever * impulse / inertia;
        }
    }

    // A surface that is not moving gives nothing: a ball can never come out of a
    // friction solve against it with more energy than it went in with. A surface that
    // is moving is another matter -- it is doing work, which is what being hit by a
    // moving body is -- so the check is only made against what is still.
    const double energyAfterFriction =
        0.5 * ball.mass * dot(ball.velocity, ball.velocity) +
        0.5 * inertia * ball.spin * ball.spin;
    if (length(surfaceVelocity) <= 0.0 && energyAfterFriction > energyBeforeFriction) {
        ball.velocity = velocityBeforeFriction;
        ball.spin = spinBeforeFriction;
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
    // A face is never exactly level in floating point, so a normal that is level to
    // within this much is a wall, not a floor: it carries nothing, and asking it to
    // carry the ball made a ball resting against the cube drop and bounce every
    // other tick.
    if (normal.y > 1e-9)
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

// Whether the ball can be put down at this spot. It is the mouse that needs this,
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

}  // namespace gtc::native
