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

// One meeting of the ball's circle with a body's outline.
//
// This is the whole of the ball's collision detection, and it is a hitscan: the
// circle is walked along the tick against the outline where the body is, and what
// comes back is the first instant the two touch. The body is where it is, not where
// it was: the bodies are stepped before the ball and each already swept itself
// against it, so interpolating the body across the ball's sweep as well counted
// that step twice and left the ball reading itself inside a shape that had already
// been pushed clear of it.
//
// How close two surfaces have to be before they are touching rather than merely
// near. It is a rounding allowance, not a distance anything may sink.
constexpr double kTouch = 1e-9;
// And how far inside counts as inside: a micrometre. The sweep closes in by
// bisection, so where it stops is good to a fraction of the step, not to the last
// bit, and an atom inside must not read back as "inside all along" or every tick
// spends itself on a contact it has already answered.
constexpr double kSkin = 1e-6;
// How far a bracket is closed in on once it is known. The meeting itself is solved
// for outright, so this is only ever used to find a place along a surface, never
// to find a time: a time found by sampling is a time that can be missed between two
// samples, and the ball would be a hair into whatever it passed too fast to see.
constexpr int kBisections = 24;

struct OutlineHit {
    double along = 2.0;    // where in the tick, 0..1
    Vec3 normal{0, 1, 0};  // which way the surface faces, out of the body
    Vec3 contact{};        // where the two surfaces touch
    double depth = 0.0;    // how far the ball's surface is already through it
    // The ball was already on this surface when the tick began. It is not on its
    // way into it, it is on it: the ball rolls along, and the surface is a contact
    // for the whole tick rather than a meeting part-way through it.
    bool resting = false;
};

// How far a point is from a body's outline as it stands at one fraction of the
// tick, and which way the nearest piece of it faces. Negative means inside.
struct OutlineGap {
    double gap = 0.0;
    Vec3 normal{0, 1, 0};
};

// Which piece of a body's outline a point is nearest to, and where along it.
// A shape is held up by the piece of it that is touching, not by the shape as a
// whole: a ball under the middle of a long edge carries that edge, and the rest
// of the shape hangs off it.
struct OutlineTouch {
    Vec3 point{};    // the nearest point on the outline
    std::size_t a = 0, b = 0;  // the two corners of that piece
    double t = 0.0;  // how far along it, 0 at a, 1 at b
};

OutlineTouch outlineTouch(Vec3 point, const Vec3* corners, std::size_t count) {
    OutlineTouch touch;
    double nearest = 1e30;
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 a = corners[i];
        const Vec3 b = corners[(i + 1) % count];
        const Vec3 side = b - a;
        const double length2 = dot(side, side);
        if (!(length2 > 1e-18)) continue;
        const double t = std::clamp(dot(point - a, side) / length2, 0.0, 1.0);
        const Vec3 closest = a + side * t;
        const double distance = length(point - closest);
        if (!(distance < nearest)) continue;
        nearest = distance;
        touch.point = closest;
        touch.a = i;
        touch.b = (i + 1) % count;
        touch.t = t;
    }
    return touch;
}

OutlineGap outlineGap(Vec3 point, const Vec3* previous, const Vec3* current, std::size_t count,
                      double fraction) {
    Vec3 corners[8];
    Vec3 middle{};
    for (std::size_t i = 0; i < count; ++i) {
        corners[i] = previous[i] + (current[i] - previous[i]) * fraction;
        middle = middle + corners[i];
    }
    middle = middle * (1.0 / static_cast<double>(count));
    double nearest = 1e30;
    Vec3 nearestPoint{};
    Vec3 way{0, 1, 0};
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 a = corners[i];
        const Vec3 b = corners[(i + 1) % count];
        const Vec3 side = b - a;
        const double length2 = dot(side, side);
        if (!(length2 > 1e-18)) continue;
        const double t = std::clamp(dot(point - a, side) / length2, 0.0, 1.0);
        const Vec3 closest = a + side * t;
        const double distance = length(point - closest);
        if (!(distance < nearest)) continue;
        nearest = distance;
        nearestPoint = closest;
        const double sideLength = std::sqrt(length2);
        // Which side of this edge is outside: the body's own middle is inside, so
        // the outward normal is the one the middle is not on.
        Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        if (dot(middle - a, outward) > 0.0) outward = outward * -1.0;
        way = outward;
    }
    OutlineGap result;
    const bool within = insideOutline(point, corners, count);
    result.gap = within ? -nearest : nearest;
    // Which way is out. Away from the nearest piece of the outline -- and where
    // that piece is a corner, away from the corner, not along the face it ends.
    // A corner is where a falling shape meets a ball that is beside it, and
    // answering that with the face's own normal slides the shape along the face
    // and through the ball instead of off it.
    if (!within && nearest > 1e-12)
        result.normal = (point - nearestPoint) * (1.0 / nearest);
    else
        result.normal = way;
    return result;
}

bool circleHitsBody(Vec3 from, Vec3 travel, const Vec3* corners, std::size_t count, double radius,
                    OutlineHit& hit) {
    const OutlineGap atStart = outlineGap(from, corners, corners, count, 1.0);
    hit.resting = atStart.gap <= radius + kTouch;
    // On the surface already. That is a contact for the whole tick however the
    // ball spends it, and a ball rolling down a face spends every tick of it
    // touching: there is no meeting to find, and looking for one found nothing at
    // all -- so the surface was never asked about friction, and the ball ran down
    // the face with its seam standing still.
    if (hit.resting) {
        hit.along = 0.0;
        hit.normal = atStart.normal;
        hit.contact = from - atStart.normal * radius;
        hit.depth = std::max(0.0, radius - atStart.gap);
        return true;
    }
    if (atStart.gap <= radius - kSkin) {
        // Already through the outline: there is nothing to interpolate, the answer
        // is where the ball is now and how far through it is.
        hit.along = 0.0;
        hit.normal = atStart.normal;
        hit.contact = from - atStart.normal * radius;
        hit.depth = radius - atStart.gap;
        return true;
    }
    // Not through it. When the ball reaches the outline is a question that can be
    // answered outright: the circle's surface meets an edge where the face's line
    // comes to one radius of the ball's middle, and meets a corner where the ball's
    // middle comes to one radius of that corner. Both are solved for, and the
    // earliest of them is the meeting. Nothing is sampled, so no speed is too fast
    // to see: a ball crossing the whole room in a tick meets whatever lies across
    // its path, at the instant it crosses it.
    Vec3 middle{};
    for (std::size_t i = 0; i < count; ++i) middle = middle + corners[i];
    middle = middle * (1.0 / static_cast<double>(count));
    double first = 2.0;
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 a = corners[i];
        const Vec3 b = corners[(i + 1) % count];
        const Vec3 side = b - a;
        const double sideLength = length(side);
        if (!(sideLength > 1e-18)) continue;
        Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        if (dot(middle - a, outward) > 0.0) outward = outward * -1.0;
        // The face itself: where its line is one radius from the ball's middle.
        const double approach = dot(outward, travel);
        if (approach < 0.0) {
            const double t = (radius - dot(outward, from - a)) / approach;
            if (t >= 0.0 && t < first) {
                const Vec3 at = from + travel * t;
                const double along = dot(at - a, side) / (sideLength * sideLength);
                // Past either end of the face, that end is what the ball meets, and
                // the corners below are where that is worked out.
                if (along >= 0.0 && along <= 1.0) first = t;
            }
        }
        // And either end of it: a corner of the outline, met by the ball's surface.
        for (const Vec3 corner : {a, b}) {
            const Vec3 away = from - corner;
            const double square = dot(travel, travel);
            const double half = dot(away, travel);
            const double clear = dot(away, away) - radius * radius;
            const double disc = half * half - square * clear;
            if (!(square > 0.0) || disc < 0.0) continue;
            const double t = (-half - std::sqrt(disc)) / square;
            if (t >= 0.0 && t < first) first = t;
        }
    }
    if (first > 1.0) return false;
    // Which way the surface faces where the ball meets it. A corner is answered by
    // the line from the corner, not by either face that ends there.
    const Vec3 at = from + travel * first;
    const OutlineGap touched = outlineGap(at, corners, corners, count, 1.0);
    hit.along = std::min(first, 1.0);
    hit.normal = touched.normal;
    hit.contact = at - touched.normal * radius;
    hit.depth = 0.0;
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

// How far a body's step can go before the whole of its outline reaches the
// ball. A corner's own hitscan is no help with a face lying across a ball -- every
// corner of that face can be a quarter of a metre from the ball while the face
// itself is touching it -- so what is walked here is the outline: the corners
// taken as they are at a fraction of the step, and the nearest piece of the shape
// they make. The answer is the last fraction at which that piece is still clear.
double PlaygroundRoom::bodyBallReach(std::size_t index, const Vec3* target) const {
    const Body& body = bodies_[index];
    if (body.count == 0) return 1.0;
    if (outlineGap(ball_.position, body.previous.data(), target, body.count, 1.0).gap >= ball_.radius) return 1.0;
    double clear = 0.0, met = 1.0;
    for (int step = 0; step < kBisections; ++step) {
        const double middle = 0.5 * (clear + met);
        if (outlineGap(ball_.position, body.previous.data(), target, body.count, middle).gap >= ball_.radius)
            clear = middle;
        else
            met = middle;
    }
    return clear;
}

// The outline may not be inside the ball. What the ball is against is one piece
// of that outline, and that piece is what it pushes out -- in proportion to how
// close the ball is to either end of it, so a ball under the middle of a long
// edge lifts that edge and leaves the rest of the shape where it is. The fixed
// distances then carry that to every corner, and the shape turns about the ball.
// Lifting every corner by the same amount is a shape being carried by a ball
// without so much as tipping, which is what a shape sitting on top of a ball for
// ever looked like.
//
// Which way is out is the outline's own business: the piece the ball is against
// faces away from the shape's middle, and the shape goes the other way. Taking it
// from the ball's middle to the nearest point instead says a ball underneath the
// cube's bottom face pushes the cube downwards, which drives it into the floor.
double PlaygroundRoom::pushBodyOutOfBall(std::size_t index, std::array<Vec3, kMaxCorners>* lift) {
    Body& body = bodies_[index];
    if (body.count == 0) return 0.0;
    const OutlineGap gap = outlineGap(ball_.position, body.position.data(), body.position.data(), body.count, 1.0);
    const double through = ball_.radius - gap.gap;
    if (!(through > 0.0)) return 0.0;
    const Vec3 away = gap.normal * -1.0;
    const OutlineTouch touch = outlineTouch(ball_.position, body.position.data(), body.count);
    const auto push = [&](std::size_t corner, double share) {
        const Vec3 amount = away * (through * share);
        body.position[corner] = body.position[corner] + amount;
        if (lift != nullptr) (*lift)[corner] = (*lift)[corner] + amount;
    };
    if (touch.a == touch.b) {
        push(touch.a, 1.0);
    } else {
        push(touch.a, 1.0 - touch.t);
        push(touch.b, touch.t);
    }
    return through;
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
            normal = {0, 1, 0};
            what = -2;
        }
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            // One test per body, not one per edge: the sweep walks the ball's path
            // against the whole outline as the body moves through the tick, and
            // answers with the first piece of it the ball's surface reaches.
            OutlineHit hit;
            if (!circleHitsBody(ball.position, travel, body.position.data(), body.count, ball.radius, hit))
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
                Vec3 step{};
                for (std::size_t i = 0; i < body.count; ++i) step = step + (body.position[i] - body.previous[i]);
                const Vec3 surfaceVelocity = step * (1.0 / (kTick * static_cast<double>(body.count)));
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
        ball.touching = true;
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

// The ball against something. The normal part is answered by both sides: if the
// other side can move, the ball keeps whatever it did not hand over. The rest of
// the contact is friction, and because it acts at the bottom of the ball rather
// than at its middle it turns the ball as well as slowing it.
void PlaygroundRoom::ballContact(Vec3 normal, int bodyIndex, bool ballHeld, Vec3 contactPoint, double depth,
                                bool exchange) {
    Ball& ball = ball_;
    Body* body = bodyIndex >= 0 ? &bodies_[std::size_t(bodyIndex)] : nullptr;
    std::size_t struck = 0;
    Vec3 answer = normal;
    bool canMove = false;
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
    // What the body's step was before anything below shortens it. A corner's stored
    // velocity is not the body's motion -- the floor cuts it, and a shortened step
    // is not motion either -- so the step is read here, once, and used from here on.
    Vec3 surfaceVelocity{};
    if (body != nullptr) {
        for (std::size_t i = 0; i < body->count; ++i)
            surfaceVelocity = surfaceVelocity + (body->position[i] - body->previous[i]);
        surfaceVelocity = surfaceVelocity * (1.0 / (kTick * static_cast<double>(body->count)));
    }

    // A ball found through an outline is the ball's own way out: the body's tick
    // keeps its outline clear of the ball -- that is what the sweep in tickBody is
    // for -- so what is left for the ball is where the ball itself has come to be
    // inside the shape. The ball goes back out along the way it is nearest to
    // being outside, and the floor is the one thing it cannot go out through.
    if (depth > 0.0) {
        const OutlineGap now = outlineGap(ball.position, body ? body->previous.data() : nullptr,
                                          body ? body->position.data() : nullptr, body ? body->count : 0, 1.0);
        normal = now.normal;
        const double through = ball.radius - now.gap;
        if (through > 0.0) {
            // The floor is under the ball, so a way out that goes downwards is no
            // way out: what is left over is the ball rolling along the floor, out
            // from under whatever is on it, rather than being left with a shape cut
            // into it.
            const double ground = (normal.y < 0.0)
                                      ? std::max(0.0, (ball.radius - ball.position.y) / normal.y)
                                      : through;
            const double byBall = std::min(through, ground);
            ball.position = ball.position + normal * byBall;
            const double left = through - byBall;
            if (body != nullptr && left > kSkin) {
                const OutlineGap after = outlineGap(ball.position, body->previous.data(), body->position.data(),
                                                    body->count, 1.0);
                if (after.gap < ball.radius) {
                    const auto clearAt = [&](double x) {
                        Vec3 moved{ball.position.x + x, std::max(ball.position.y, ball.radius), 0};
                        return outlineGap(moved, body->previous.data(), body->position.data(), body->count, 1.0)
                                   .gap >= ball.radius;
                    };
                    // Which way is out along the floor: the way that puts the
                    // ball further from the shape.
                    const double side = clearAt(-0.01) || !clearAt(0.01) ? -1.0 : 1.0;
                    double failed = 0.0, found = -1.0;
                    for (int step = 1; step <= 200 && found < 0.0; ++step) {
                        const double reach = 0.001 * double(step);
                        if (clearAt(side * reach)) {
                            found = reach;
                            break;
                        }
                        failed = reach;
                    }
                    if (found > 0.0) {
                        double low = failed, high = found;
                        for (int step = 0; step < kBisections; ++step) {
                            const double middle = 0.5 * (low + high);
                            if (clearAt(side * middle)) {
                                high = middle;
                            } else {
                                low = middle;
                            }
                        }
                        ball.position.x += side * high;
                        ball.position.y = std::max(ball.position.y, ball.radius);
                        if (ball.velocity.x * side < 0.0) ball.velocity.x = 0.0;
                    }
                }
            }
            contactPoint = contactPoint - normal * (through);
        }
        const double into = dot(ball.velocity - surfaceVelocity, normal);
        if (into < 0.0) ball.velocity = ball.velocity - normal * into;
    }

    if (!exchange) {
        // The blow for this body was struck earlier in the tick. What is left is
        // where the two are, and the ball may not travel into it any further.
        const double into = dot(ball.velocity - surfaceVelocity, normal);
        if (into < 0.0) ball.velocity = ball.velocity - normal * into;
        if (normal.y > 1e-9)
            ball.standing = ball.standing &&
                            kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));
        return;
    }

    // Both sides stop where they met: the ball loses the part of its speed that
    // was driving into the shape, and the shape takes the blow. The bodies are
    // stepped before the ball, so where the body is, is where it has already been
    // solved to be -- the ball is the one that has travelled since.
    std::array<Vec3, kMaxCorners> handed{};
    // What the struck corner actually did this tick. Its stored velocity can be
    // zero -- the floor cuts it when the corner lands -- while the corner is still
    // travelling down into the ball, and the ball has to answer the travel.
    if (!canMove) surfaceVelocity = Vec3{};
    const double arrivedSpeed = dot(ball.arrival - surfaceVelocity, normal);
    const double closing = dot(ball.velocity - surfaceVelocity, normal);
    // A bounce is the answer to arriving at a surface, measured against what the
    // ball arrived with rather than the speed gravity lent it on the way: a ball on
    // the floor gets that every tick, and bouncing it would be bouncing it off
    // nothing.
    const double rebound = arrivedSpeed < 0.0 ? -kBallRestitution * arrivedSpeed : 0.0;
    double pressed = 0.0;
    if (closing < 0.0) {
        if (canMove) {
            // A body struck by the ball takes the blow as the solid it is: every
            // corner of it moves together, turning about the middle by the amount
            // that blow would turn it. Sharing it out unevenly corner by corner had
            // one corner stop dead while the other three kept falling.
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
            // What the ball can take of this blow is only what it is free to do with
            // it. A ball already held by a surface, or one on the floor that the
            // blow would drive downwards, has nowhere to go: the body takes the
            // whole answer, and stops on the ball instead of sinking into it and
            // being answered again next tick.
            const bool ballGives = !ballHeld && !(normal.y < 0.0 && ball.position.y <= ball.radius + kTouch);
            const double ballShare = ballGives ? 1.0 : 0.0;
            const double impulse = (rebound - closing) / (ballShare / ball.mass + bodyShare);
            ball.velocity = ball.velocity + normal * (impulse * ballShare / ball.mass);
            // What this surface can rub against the ball is what it can press the
            // ball with, and the ball is only pressed by the part of the blow it
            // actually took. A ball lying on the floor with a shape landing flat on
            // top of it takes none of that blow -- the floor under it does -- and a
            // friction solve given the whole of it span the ball sideways and down
            // at metres a second off a body that never touched it that way.
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
            pressed = impulse * ballShare;
        } else {
            // Nowhere to go: the ball takes the whole answer itself.
            const double impulse = (rebound - closing) * ball.mass;
            ball.velocity = ball.velocity + normal * (impulse / ball.mass);
            pressed = impulse;
        }
    }
    if (canMove) {
        // What the body is left doing is what it was doing plus the blow it took.
        // Its speed must not be read back off the shortened step: the step was cut
        // short by the ball, and reading it back would hand the body the whole fall
        // again on the next tick and let it through.
        for (std::size_t i = 0; i < body->count; ++i) body->velocity[i] = body->velocity[i] + handed[i];
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

    // A contact that is not a blow is still a contact. The surface carries the
    // ball's weight for as long as the ball is on it, and friction is that weight
    // times mu -- the same thing the floor already answers, because gravity drives
    // the ball into the floor and the ball loses that speed to it every tick. A
    // slope is not that: the ball runs *along* the face, so nothing drives it into
    // the face and the blow is nothing, and with no normal force there was no
    // friction at all. The seam stood still while the ball ran away down the face
    // and then jumped when the next blow arrived. What the surface carries when it
    // is only holding the ball up is the weight pressed into it, over the tick.
    if (!(pressed > 0.0)) pressed = kGravity * ball.mass * std::max(0.0, normal.y) * kTick;

    // Friction is not applied here. A grip at one surface changes what is left to
    // slip at every other surface the ball is against, so the whole tick's contacts
    // are solved against each other once they are all known -- see
    // settleBallFriction(). What is recorded is what each contact is: the face, how
    // it is moving there, and how hard it is pressed.
    if (pressed > 0.0 && ballTouchCount_ < kMaxBallTouches)
        ballTouches_[ballTouchCount_++] = BallTouch{normal, surfaceVelocity, pressed};
    const Vec3 tangent{normal.y, -normal.x, 0};

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
            // F = Crr * N against the way the ball is going, and only against that.
            // It is a loss, so it takes the travel and the turn together: the grip
            // re-establishes rolling on the next contact, which is where the turn
            // follows the slower roll. Handing it the same lever as friction -- as
            // though it were a fourth kind of friction -- turned the speed it took
            // out of the travel straight into spin, which is not a loss at all: a
            // ball rolled for ever, and the energy check below rightly refused it.
            // It acts on the travel: the ball is slowed, and the grip brings the
            // turn down to match it on the next contact. Doing it through the
            // friction lever as well would take the travel out of the ball and put
            // it straight into the turn, which is not a loss, and a ball would roll
            // for ever -- the energy check below is what caught that.
            const double limit = kBallRollingResistance * pressed * kTick;
            const double impulse = std::clamp(travel * ball.mass, -limit, limit);
            ball.velocity = ball.velocity - tangent * (impulse / ball.mass);
        }
    }

    // And whatever is left pointing into the surface is speed the ball cannot spend.
    // The bodies are stepped before the ball, so what a body gave way by is already
    // in its positions and not in what is left of the tick: for the rest of the tick
    // this surface is where it is, and a ball that kept its inward part would drive
    // itself through a face that has already finished moving -- which is exactly
    // what a ball landing on a shape that gives a little under it does. A bounce has
    // turned the ball around before this runs, so nothing that was leaving is caught.
    {
        const double into = dot(ball.velocity, normal);
        if (into < 0.0) ball.velocity = ball.velocity - normal * into;
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
