#include "client/playground_ball_shape.h"
#include "client/playground_room_tick.h"
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
    // A tick starts straight: whatever the last one bent at is spent.
    ball.bent = false;
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
        // The surface under it carries it, and that force is not written here: it is
        // written at the end of the tick, by the account below, together with
        // everything else the ground had to supply. Writing it here as well was a
        // floor drawn carrying the ball's weight twice -- and, worse, on the tick the
        // ball fell asleep it was written and then the speed the weight had just been
        // given was thrown away, so the account had a weight in it with nothing
        // answering it. The account closes, and it closes where the answer is.
        ball.force = ball.force - weight;
    } else {
        const double speed = length(ball.velocity);
        if (speed > 0.0) {
            const double area = kPi * ball.radius * ball.radius;
            const double drag = 0.5 * kAirDensity * kBallDragCoefficient * area * speed * speed;  // N
            addForce(ball.position, ball.velocity * (-drag / speed), ForceKind::Drag, &ball.force, kBallOwner);
        }
    }
    // And the room's resolution, as everywhere else: a ball that cannot carry
    // itself a micrometre in a tick is not moving.
    ball.velocity = playground_tick::resolved(playground_tick::accelerated(ball.velocity, ball.force, ball.mass));

    // What the ball is standing on is not relearned halfway through the tick. It
    // is a fact about the tick -- the state it began in -- and both halves of the
    // tick need it: the shapes are solved first, where a shape landing on a ball the
    // floor is holding has to know that the floor is holding it, and the ball's own
    // contacts are answered here, where the same question is asked again. Clearing
    // it here said, for the rest of the tick, that the ball was in mid-air, and a
    // ball in mid-air takes the whole of any blow: a cube landing flat on a ball
    // resting on the floor drove that ball straight down through the floor at six
    // metres a second, the floor stopped it, and the cube was left with the wreckage
    // of a blow that had been given to something that could not take it. The list of
    // what it is touching is written afresh at the end of the tick, below, from where
    // everything in the room has ended up.
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
    // The bodies this tick has already stopped at the ball. A body that has met the
    // ball has spent its step there: it is carried back to the instant of the
    // meeting -- the turn and the translation by the same fraction -- and what is
    // left of the tick is the ball's alone. Prevention is the whole point: the shape
    // stops at the ball instead of being kicked out of it afterwards.
    std::array<bool, kBodyCount> bodyStopped{};
    // Nothing is driven into the ball. A shape that was clear of it when the tick
    // began, and whose own step would carry its outline inside it, is stopped at the
    // instant the two touch -- the shape carried back to that instant, since the
    // meeting is one moment and not two.
    //
    // It is asked of every shape once, before the ball's own walk begins, and not
    // from inside the walk: the walk moves the ball and answers contacts as it meets
    // them, so a shape re-measured halfway through it is being measured against a
    // ball that is no longer where it was, and a step that is already partly spent.
    // One question, one answer, at the one moment when both are still where the tick
    // found them.
    //
    // What it prevents is the arrival, not the contact. A shape already against the
    // ball is in contact, and contact is answered by the contact -- refusing its step
    // every tick would hold it up for ever with the speed it arrived with, which is
    // not a solve, it is a shape standing on nothing.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (body.count == 0) continue;
        // Where the step it has already taken would leave the two of them. This is the
        // question prevention is really being asked: not "did the shape come near the
        // ball", which is true of every shape touching it and of every shape sliding
        // along it, but "would its step have put the ball inside it". Asked the other
        // way -- as a sweep for the first instant the distance falls to the ball's
        // radius -- a shape that is already against the ball meets that instant at the
        // very start of its step, every tick, and its whole step is thrown away. What
        // it then does is keep the speed and lose the travel: a cube squeezed against
        // the ball by the ball's own grip gathered a metre a second and moved four
        // millimetres, which is a force that never moved anything, and the energy it
        // was supposed to have spent never left it. That is a pump, and it is where
        // the King's squeeze came from.
        const double endGap =
            ball_shape::outlineGap(ball.position, body.position.data(), body.position.data(), body.count, 1.0).gap;
        if (endGap >= ball.radius - kTouch) continue;
        // Was it already there? A shape whose step *began* against the ball is in
        // contact, and contact is the contact's business: the patch answers it, and
        // answering it is what stops it. Prevention is for the arrival -- the shape
        // that was clear when the tick started and would be inside the ball when it
        // ends.
        //
        // Asked only of the end of the step, a shape resting on the ball is caught
        // here every tick: it began touching, so the sweep from its previous pose
        // meets the ball at the very start of the step, the fraction is nought, and
        // its whole travel *and* its whole turn are handed back -- with the speed
        // that made them still in it. That is a shape held still by its own place,
        // which the room forbids: it is the state the comment below calls a shape
        // standing on nothing. A cube on the crown of a ball, its middle a hand's
        // width off the crown, was put back where it stood sixty times a second and
        // turned faster every time it was told it had not moved -- energy out of a
        // turn that never happened, and the rise the King saw.
        const double startGap =
            ball_shape::outlineGap(ball.position, body.previous.data(), body.previous.data(), body.count, 1.0).gap;
        if (startGap < ball.radius - kTouch) continue;
        const double limit = ball.radius;
        // It may come as near as that and no nearer, and where that is is solved
        // rather than sampled: the speed of the ball and the speed of the turn make
        // no difference to where it lands.
        OutlineHit stop;
        if (!sweptBallAgainstMoving(body.previous.data(), body.position.data(), body.count, ball.position,
                                    ball.velocity, limit, kTick, stop, false))
            continue;
        if (!(stop.along < 1.0)) continue;
        body.centre = body.previousCentre + (body.centre - body.previousCentre) * stop.along;
        body.angle = body.previousAngle + (body.angle - body.previousAngle) * stop.along;
        placeCorners(body);
        bodyStopped[index] = true;
        ++bodyStepClamped_;
    }
    const auto anybodyMoving = [this]() {
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            if (bodies_[index].frozen) continue;
            for (std::size_t i = 0; i < bodies_[index].count; ++i)
                if (length(bodies_[index].position[i] - bodies_[index].previous[i]) > 0.0) return true;
        }
        return length(point_ - Vec3{}) >= 0.0 && !pointFrozen_ && length(velocity_) > 0.0;
    };
    double remaining = 1.0;
    // What the ball was doing as the walk began. The drawing needs the piece of the
    // path before the first meeting, and it is this velocity that made it.
    const Vec3 walkStart = ball.velocity;
    // Set while the meeting is being recorded and cleared once the contact has had
    // its say: what the ball leaves with is the answer, not what it arrived with.
    bool bendRecording = false;
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
            Body& body = bodies_[index];
            // A body that is already stopped against the ball this tick is not
            // moving any more: its step was spent at the meeting (above). What is
            // left of the tick is swept against where it came to rest.
            const Vec3* was = bodyStopped[index] ? body.position.data() : body.previous.data();
            // The body is stepped before the ball, and stepping it moves the piece of
            // outline the ball would have met. So the meeting is solved between two
            // *moving* things: the ball's travel and the body's, corner by corner,
            // through the tick. Conservative advancement at a micrometre a pass, with
            // the moment bracketed and closed in on, so neither how fast the ball is
            // going nor how fast the body is turning can hide the arrival between two
            // samples. That is the hitscan: solved outright, not stepped towards.
            OutlineHit hit;
            if (!sweptBallAgainstMoving(was, body.position.data(), body.count, ball.position, ball.velocity,
                                        ball.radius, kTick * remaining, hit))
                continue;

            // Already on this surface when the tick began: a contact for the whole
            // tick, answered here and then travelled along, not a meeting to stop
            // at. Stopping there eats every tick's travel and the ball sits
            // gathering speed and spinning instead of rolling.
            // A ball within a skin of the face is against it, not through it. The
            // arithmetic that rests a ball at exactly one radius lands it a
            // nanometre either side, and reading that as inside pinned a ball under
            // a shape where it was, gathering turn on the spot.
            // A ball on a surface, including one that is through it. The first case is
            // a contact for the whole tick. The second is not a meeting at all: it is a
            // state that should never have existed, and speed has nothing to say about
            // it -- "do not close" is already true of something that is not closing.
            // It was answered with nothing, and the walk then gave up on it entirely
            // (see the break below), so a ball that got through a face stayed through
            // it for the rest of the session: the cube in front of it moved away and
            // the ball sat inside where the cube had been.
            //
            // So it is put back on the surface, along the way out that is nearest, by
            // position and nothing else -- the same answer a corner of a shape gets.
            // Nothing is added to its speed, so nothing is conjured; it is a place
            // corrected, once, and then the ordinary contact rules apply to it again.
            if (hit.resting && !restingAnswered[index]) {
                restingAnswered[index] = true;
                const OutlineGap on = outlineGap(ball.position, body.position.data(), body.position.data(),
                                                 body.count, 1.0);
                // A micrometre in is touching. Only a depth the room's own resolution
                // can tell from touching is one to put back, and only then is it a
                // miss worth counting.
                if (ball.radius - on.gap > playground_tick::kQuantum) {
                    // The shape gives way first. It is the one that arrived, and the
                    // ball is the one thing in the room whose path may never jump: a
                    // ball that is teleported out of what it is in is exactly the
                    // "kicked out" this room is not allowed to do. Only when the shape
                    // cannot move -- the mouse is holding it, or moving it would put it
                    // through the floor -- does the ball step aside, and only then is
                    // it a miss worth counting.
                    const double through = ball.radius - on.gap;
                    if (!pushBodyOutOfBall(index, on.normal, through)) {
                        ++ballFoundThrough_;
                        ball.position = ball.position + on.normal * through;
                    }
                }
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
            if (hit.depth <= kSkin && hit.along <= kTouch) {
                const Vec3 surfaceVelocity =
                    pointSpeed(body, hit.contact) ;
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
            ball.position = playground_tick::resolvedMove(ball.position, next);
            break;
        }
        ball.position = playground_tick::resolvedMove(ball.position, ball.position + travel * first);
        // The bent tick, recorded where the ball is stopped and before anything has
        // had a chance to answer it: where the meeting was, when it was, and what
        // the ball was doing on each side of it. Only the first one in a tick is
        // kept -- the rest are a ball lying on what it has just hit, and the piece
        // of path between two of those is too short to draw.
        if (!ball.bent) {
            ball.bent = true;
            bendRecording = true;
            ball.bendAt = (1.0 - remaining) + first * remaining;
            ball.bendPoint = ball.position;
            ball.bendIn = walkStart;
        }
        remaining *= 1.0 - first;
        // Has this body already had its answer this tick? Asked before the mark is
        // put down, or the first meeting in a tick would be counted as the second.
        // The shape is carried back to the instant the two met. The ball was stopped
        // there; leaving the shape at the end of its own step would put the ball
        // inside it the moment the tick ended, and the next tick would find it there
        // and have to lift it out -- which is the kick this prevents. The centre and
        // the turn are the body's whole step, so both go back by the same fraction of
        // the tick that the ball had spent when it arrived.
        // A body the ball is already against has not arrived at it this tick, and
        // carrying it back to where the tick found it is not prevention -- it is
        // erasing the step it was entitled to take. A shape resting on the ball was
        // held in place this way on every tick, however hard it was pushed along the
        // surface: it kept a speed it never spent, and the ball's grip read that
        // speed as the face it was standing on moving and span, tick after tick,
        // under a weight that was not doing anything.
        //
        // The question is whether the ball *travelled* to this meeting. Below the
        // room's resolution there is no travel to speak of: a meeting that cost the
        // ball less than a micrometre of path is a contact it already had, and the
        // shape keeps the step it took. A shape the ball really did arrive at -- or
        // one it finds inside itself, which is the same prevention asked afterwards
        // -- is carried back to the instant of the meeting, as before.
        const double spent = length(walkStart) * kTick * (first * remaining);
        if (what >= 0 && !bodyStopped[std::size_t(what)] &&
            (depth > 0.0 || spent >= playground_tick::kQuantum)) {
            Body& met = bodies_[std::size_t(what)];
            const double when = (1.0 - remaining) + first * remaining;
            met.centre = met.previousCentre + (met.centre - met.previousCentre) * when;
            met.angle = met.previousAngle + (met.angle - met.previousAngle) * when;
            placeCorners(met);
            bodyStopped[std::size_t(what)] = true;
        }
        const bool exchange = what < 0 || !answered[std::size_t(what)];
        if (what >= 0 && std::size_t(what) < kBodyCount) answered[std::size_t(what)] = true;
        if (what == -2) {
            floorAnswered = true;
            // The floor is met at the ball's underside, not at its middle: an arrow
            // drawn from the centre of a ball says the ground is pressing the ball
            // through its own middle, and a lever drawn that way turns nothing.
            ballContact(normal, -1, held, ball.position - normal * ball.radius);
            if (bendRecording) { ball.bendOut = ball.velocity; bendRecording = false; }
        } else {
            // One blow per body per tick. A body met again in what is left of the
            // tick has already given its answer, so this time it only stops the
            // ball: it is a resting contact for the rest of the tick, which is what
            // stops a ball sliding into a closing wedge and being left inside it.
            ballContact(normal, what, held, contactPoint, depth, exchange);
            if (bendRecording) { ball.bendOut = ball.velocity; bendRecording = false; }
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
        // A surface that is still met at the very start of the remaining time has
        // already been answered as far as it can be, and going round again would
        // divide the tick by zero. Every surface that could be is put back out above,
        // so what is left here is a guard and not an escape: the ball is not left
        // inside anything, it is only not advanced further this tick.
        if (depth > kSkin && first <= kTouch) break;
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
        ball.touching = false;
        ball.standing = true;
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
        // A ball on the floor is a ball within the world's own resolution of it, not
        // a ball at the one height that arithmetic happens to produce. Aiming at
        // exactness here said a ball rolling along the ground at a ten-millionth of
        // a millimetre above it was a ball in mid-air: nothing held it, so nothing
        // was written holding it, and every blow it took had no partner on screen.
        if (ball.position.y <= ball.radius + kTouch) {
            ball.touching = true;
            ball.standing = ball.standing && holds(Vec3{0, 1, 0});
            if (ball.velocity.y < 0.0) ball.velocity.y = 0.0;
        }
    }

    // The floor, met without being travelled into.
    //
    // A ball resting on the floor is in contact with it every tick, and this walk
    // only finds the floor when the ball's travel would take it below: a ball that
    // is being held up does not move at all, so the floor never appeared. It was
    // still there. The consequence was not that the ball fell through: it was that
    // the grip of the floor was missing from the one solve that has to know about
    // it -- a ball pinched between the floor and a weight standing on it was
    // answered against the weight alone, and the weight span it out sideways just
    // as a grip should. The face is what it is whether or not anything moved.
    if (!floorAnswered && ball.position.y <= ball.radius + kTouch)
        ballContact(Vec3{0, 1, 0}, -1, held, ball.position - Vec3{0, ball.radius, 0.0});

    // Every face the tick met, solved against the others: what is left of the ball's
    // speed and its turn is what their grips all allow at once.
    settleBallFriction();

    // A whole tick of travel below a picometre is not motion a double can carry:
    // that is the floor of what this simulation can represent, not a speed chosen
    // out of the air. Below it the ball is at rest.
    if (length(ball.velocity) < kRestSpeed) ball.velocity = {};

    // What the ball's supporters had to supply, and it is the only force in the room
    // whose source is a surface with no shape: the floor under the ball.
    //
    // The ball's whole momentum change over the tick is known exactly -- it is where
    // the ball ends up going, less where it started. The blows it was given are known
    // too, because every one of them was written down when it landed. What is left
    // over is what nothing solid gave it, and there is exactly one such thing under a
    // ball: the ground. Writing it is what makes the picture add up. Without it a blow
    // that the floor answered was drawn as a lone arrow on the shape -- a cube thrown
    // off a ball by a force that no ball and no floor ever felt -- and that is what a
    // conjured force looks like, even when the arithmetic underneath is right.
    //
    // It is also the honest place for the two things the ball does with momentum that
    // no contact of its own accounts for: the weight it gains every tick, and the
    // speed the floor clips off its underside. Both are the ground's doing, and both
    // arrive here as the difference between the account and the truth.
    // A ball that is against anything at all, and not only one that is standing on
    // it. A ball bouncing off the floor is not "held up" by it -- it is not standing
    // -- but it is still the floor that answers what the bounce did not, and leaving
    // the answer out because the ball happened to be on its way up lost it from the
    // picture: the ball's account was out by a fifth of a newton second, in both
    // directions at once, on exactly the ticks it was being knocked about. Whatever
    // is left over is what nothing solid gave it, and under a ball that is the
    // ground, so it is written wherever the ball has a surface to be against.
    if (ball.touching) {
        const Vec3 total = (ball.velocity - ball.arrival) * ball.mass;
        const Vec3 support = total - ballBlows_ - weight * kTick;
        if (length(support) > 1e-9) {
            const Vec3 where = length(ball.supportNormal) > 0.0 ? ball.supportNormal : Vec3{0, 1, 0};
            addForce(ball.position + where * (-ball.radius), support * (1.0 / kTick), ForceKind::Contact, nullptr,
                     kBallOwner);
        }
    }

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
