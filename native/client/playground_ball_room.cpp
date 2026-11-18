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
using namespace playground_detail;

// Defined with the contact response; shared by the standing-contact solve.
bool holdsBall(Vec3 normal);

// A ball that is asleep is not stepped, but only while nothing can reach it. The
// dot and every body count: if any of them is moving, the ball is looked at.
bool PlaygroundRoom::ballNeighboursMoving(const Ball& ball) const {
    if (!ball.active) return false;  // not in the room
    // Motion elsewhere in the room is not a reason to tick this ball. Wake it only
    // when a moving solid's swept reach can get to its surface this tick. Bodies are
    // already stepped when this is queried, so translation plus furthest-corner arc
    // bounds the whole path; the actual contact walk still decides the collision.
    if (!pointFrozen_ && length(velocity_) > 0.0 &&
        length(ball.position - point_) <= ball.radius + playground_detail::kPointRadius +
                                                length(point_ - previous_) + playground_tick::kQuantum)
        return true;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.frozen || body.count == 0) continue;
        double furthest = 0.0;
        for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
        const double reach = length(body.centre - body.previousCentre) +
                             std::abs(body.angle - body.previousAngle) * furthest;
        Vec3 way{}, at{};
        if (bodyBallGap(ball, index, way, at) <= reach + playground_tick::kQuantum) return true;
    }
    // Another ball wakes this one only if its swept circle can reach it. The moving
    // ball's own walk handles the pair impulse; this test merely avoids advancing
    // every sleeping ball whenever any unrelated ball moves.
    for (std::size_t other = 0; other < kBallCount; ++other) {
        if (&balls_[other] == &ball) continue;
        const Ball& neighbour = balls_[other];
        if (!neighbour.active) continue;  // not in the room
        const double reach = length(neighbour.position - neighbour.previous);
        if (reach > playground_tick::kQuantum &&
            length(neighbour.position - ball.position) <= ball.radius + neighbour.radius + reach +
                                                               playground_tick::kQuantum)
            return true;
    }
    // A taut rope at its static hanging equilibrium is already balanced by its
    // stored tension. It does not by itself make a sleeping ball active; impacts,
    // moving neighbours, and moving bodies above wake it through the checks above.
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
        if (!ball.active) continue;  // not in the room
        Rope& rope = ropeForBall(index);
        const bool traceBallTick = std::getenv("GTC_TRACE_CONTEXT") != nullptr;
        const double traceEnergyBefore = traceBallTick ? totalEnergy() : 0.0;
        const Vec3 tracePositionBefore = ball.position, traceVelocityBefore = ball.velocity;
        const double traceSpinBefore = ball.spin;
        const auto traceBallResult = [&](const char* path) {
            if (!traceBallTick) return;
            const double after = totalEnergy();
            std::printf("BALL_TICK_TRACE context=%s id=%zu path=%s dE=%+.12g p=(%.9g,%.9g)->(%.9g,%.9g) "
                        "v=(%.9g,%.9g)->(%.9g,%.9g) w=%.9g->%.9g\n",
                        std::getenv("GTC_TRACE_CONTEXT"), index, path, after-traceEnergyBefore,
                        tracePositionBefore.x,tracePositionBefore.y,ball.position.x,ball.position.y,
                        traceVelocityBefore.x,traceVelocityBefore.y,ball.velocity.x,ball.velocity.y,
                        traceSpinBefore,ball.spin);
        };
        // Near a body, the rope's curved path has to meet that body at the
        // actual instant of contact rather than a later free-fall projection.
        if (rope.attached && rope.ball == index && !ballHeld(ball) && walkRopeContacts(ball)) {
            traceBallResult("rope-contact-walk");
            continue;
        }
        const double ropeError = std::abs(length(ball.position - rope.anchor) - rope.length);
        const bool quietRopeBob = rope.attached && rope.ball == index && ball.restitution <= 0.02 && rope.taut &&
                                  ropeError <= playground_tick::kQuantum &&
                                  length(ball.velocity) <= playground_tick::kQuantumSpeed &&
                                  std::abs(ball.spin) * ball.radius <= playground_tick::kQuantumSpeed;
        if ((!ball.frozen && !quietRopeBob) || ballNeighboursMoving(ball)) tickBall(ball);
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
        if (rope.attached && rope.ball == index) {
            if (ballHeld(ball)) {
                // The hand, not gravity, owns the held position. Do not advance a
                // pendulum step or invent tension until the ball is released.
                rope.taut = length(ball.position - rope.anchor) >= rope.length - ball_shape::kTouch;
                rope.tension = 0.0;
            } else {
                holdOnRope(ball);
            }
        }
        traceBallResult("ordinary-ball-tick");
    }
}

// The surfaces the ball is against, holding it out of themselves.
//
// A contact says one thing: the two are not closing. Everything else in the tick --
// the walk's blows, a shape pressing the ball down, and above all the grip -- can leave
// the ball with speed pointing into a surface it is lying against, and nothing later
// would answer it. It is answered here, and answered wherever it is made: a ball
// pinched in a wedge between a cube's side and the floor kept a centimetre a second
// into the floor for ever that way, because the grip solved its friction along the
// cube's tilted face and a tangent on a tilted face has a downward part. The ball was
// stopped, moved nowhere, and shook for as long as the room was left alone.
void PlaygroundRoom::settleBallSurfaces(Ball& ball) {
    {
        ball.touching = false;
        ball.standing = true;
        // The faces are read as they are when this solve begins, and not as the
        // solve itself leaves them.
        //
        // Every pass that answers a face gives the shape its share of the blow, and
        // that share moves the shape: a shape standing on the floor takes a step of a
        // few tenths of a millimetre a second and its own support takes it back on
        // the next tick. Measured against *that*, the face looks to be running away
        // from the ball, the ball looks to be separating, and a pass that has
        // separated the ball from one face of a corner has nothing left to say about
        // the other -- so the corner stopped half answered, with a speed in it that
        // was never spent. The ball is measured against the surface it met, not
        // against the surface it is pushing.
        struct Face {
            Vec3 velocity{};
            Vec3 centre{};
            double spin = 0.0;
        };
        std::array<Face, kBodyCount> faces{};
        for (std::size_t index = 0; index < kBodyCount; ++index)
            faces[index] = Face{bodies_[index].velocity, bodies_[index].centre, bodies_[index].spin};
        const auto faceSpeed = [&faces](std::size_t index, Vec3 at) {
            const Vec3 arm = at - faces[index].centre;
            return Vec3{faces[index].velocity.x - faces[index].spin * arm.y,
                        faces[index].velocity.y + faces[index].spin * arm.x, faces[index].velocity.z};
        };
        const auto holds = [](Vec3 normal) { return holdsBall(normal); };
        // A corner has more than one face, and the faces have to agree.
        //
        // Answering each face once, in turn, is not solving a corner: the ball's speed
        // is cut along the first face, which leaves it sliding along that face, which
        // is *into* the second; the second then cuts it along itself, which puts it
        // back into the first. What is left after one pass over the faces is a speed
        // with nowhere to go -- the ball skating along the seam -- and nothing answers
        // it, because every face has already had its say. Measured in the King's corner
        // (a ball wedged between a cube's side and the triangle's slope, both standing
        // on the floor): after one pass the ball was left sliding down the slope at
        // 0.24 m/s, which is 0.17 m/s into the cube, and the corner never settled --
        // three bodies holding speed they could not spend, twenty times a second, with
        // nothing on the screen moving, and the next tick's solver throwing that speed
        // away unrecorded.
        //
        // So the faces are asked again, and again, until one whole pass finds nothing
        // left to answer. Each pass applies a real, paired impulse -- the ball's share
        // and the shape's, exactly as one pass always did -- so this is the same
        // contact solved to agreement, not a stronger one. What each face is owed is
        // added up over the passes and drawn as one arrow where it acts, because it is
        // one contact.
        struct FaceBlow {
            Vec3 at{};
            Vec3 impulse{};
            bool used = false;
        };
        std::array<FaceBlow, kBodyCount * kMaxOutlineCorners> blows{};
        for (;;) {
            bool answered = false;
            const Vec3 wasSpeed = ball.velocity;
            for (std::size_t index = 0; index < kBodyCount; ++index) {
                Body& body = bodies_[index];
                for (std::size_t edge = 0; edge < body.count; ++edge) {
                    const std::size_t top = (edge + 1) % body.count;
                    const Vec3 a = body.position[edge];
                    const Vec3 b = body.position[top];
                    const Vec3 side = b - a;
                    const double sideLength = side.x * side.x + side.y * side.y;
                    const double t =
                        sideLength > 0.0 ? std::clamp(dot(ball.position - a, side) / sideLength, 0.0, 1.0) : 0.0;
                    const Vec3 gap = ball.position - (a + side * t);
                    const double distance = length(gap);
                    // Within the room's own resolution of the surface, not within a
                    // nanometre of it. The sweep lands the ball on the face and the
                    // bisection leaves it a few micrometres either side; read at a
                    // nanometre, a ball lying in a pinch was not touching anything at
                    // all -- so no grip, no friction, and no rest, and a millimetre a
                    // second of spin and slide that never died.
                    if (distance <= 0.0) continue;
                    const Vec3 normal = gap / distance;
                    const Vec3 surface = faceSpeed(index, a + side * t);
                    const double closing = dot(ball.velocity - surface, normal);
                    // `spare` is the gap left between the ball and this face *now*, and
                    // only a ball that is against the face -- within the room's own
                    // micrometre of it -- is answered by it.
                    //
                    // It used to be answered for a face the ball would reach before the
                    // *next* tick was over: `spare > -closing * kTick` let the constraint
                    // reach a whole tick ahead, and the speed it took there had no blow in
                    // it. A ball dropped onto the triangle arrived at 5.29 m/s, was stopped
                    // by this rule 28 mm short of the face with the little normal speed it
                    // had left set to exactly nothing, and then slid down the hypotenuse
                    // with its tangent untouched and its spin growing: the arrival was
                    // spent here, so the walk never saw a meeting to bounce.
                    //
                    // An arrival is the walk's business, and the walk answers it with the
                    // restitution every other surface gets, in a pair of impulses, one on
                    // the ball and one on the shape. What is left for this constraint is
                    // the residual speed a ball *lying against* a face can be left with by
                    // the blow or by the grip -- which is the case it was written for.
                    const double spare = distance - ball.radius;
                    if (spare > playground_tick::kQuantum) continue;
                    ball.touching = true;
                    // A body above the ball is a weight on it, not something it stands
                    // on: only a surface underneath can hold it up.
                    if (normal.y > 1e-9) ball.standing = ball.standing && holds(normal);
                    // The surface is where the ball is, so nothing can be driven into
                    // it. Anything left over from an earlier contact in the same tick --
                    // a body pressing the ball down onto the floor -- is answered here,
                    // which is what lets a ball carry a weight instead of humming.
                    //
                    // How fast it may close is what the gap allows: a ball a millimetre
                    // above a face may not cross more than a millimetre of it, and a ball
                    // touching it may not close at all. That is the whole of the
                    // constraint -- no correction of place, and no speed taken that the
                    // surface did not take.
                    const double allowed = -std::max(0.0, spare) / kTick;
                    if (closing < allowed) {
                        // The speed the ball gives up here is a blow, and a blow has two
                        // ends. It used to be taken off the ball and written nowhere else:
                        // the ball was held up by a surface that was never given the load
                        // that held it, so a weight resting on a plank pushed on nothing,
                        // the plank was loaded only on the ticks the walk happened to catch
                        // a meeting, and it accelerated in a square wave -- stop, move,
                        // stop, move, at ten hertz, which is what the King can see.
                        //
                        // So the surface takes it, at the place they met, as the solid it
                        // is: the same pair as everywhere else in the room, applied to the
                        // ball and to the shape, in the ledger and in the motion.
                        const Vec3 at = a + side * t;
                        // The ball's own share, along the normal it is pushed out on:
                        // `closing - allowed` is negative here, and the ball's speed rises
                        // by the opposite of it. The shape takes exactly this much the other
                        // way -- a push and its partner, and no momentum from nowhere.
                        const Vec3 blow = normal * (-ball.mass * (closing - allowed));
                        ball.velocity = ball.velocity - normal * (closing - allowed);
                        // A shape asleep, or in the hand, is a surface and not a thing
                        // that may be shoved: this blow moved it and woke it out of
                        // nothing but the ball's own weight -- the ball with an infinite
                        // force to shove a triangle with. The ball is still stopped here;
                        // what holds the shape is what always held it.
                        if (!bodyHeld(index) && !body.frozen)
                            pushBody(body, at, blow * -1.0, ForceKind::Contact);
                        FaceBlow& slot = blows[index * kMaxOutlineCorners + edge];
                        slot.at = ball.position - normal * ball.radius;
                        slot.impulse = slot.impulse + blow;
                        slot.used = true;
                        answered = true;
                    }
                }
            }
            // Done when a whole pass has nothing to say, and also when what it did say
            // is smaller than the room can resolve. A corner does not give its answer
            // up in one pass: the cut along one face is a slide along the other, and
            // the cut along the other is a slide along the first, so what is left
            // shrinks by a fixed fraction of itself each time -- at the King's corner,
            // which is two faces a quarter of a turn apart, by half. Eight passes left
            // a fifth of a millimetre a second in a ball that was standing still, and
            // the room drew it and never spent it. Asking again until the answer stops
            // moving is the same rule the shapes' own load and grip are swept by, and
            // it is the only count that is not arbitrary: the loop ends on the answer.
            if (!answered) break;
            if (length(ball.velocity - wasSpeed) <= playground_tick::kQuantumSpeed) break;
        }
        for (const FaceBlow& slot : blows)
            if (slot.used && length(slot.impulse) > 1e-12)
                addForce(slot.at, slot.impulse, ForceKind::Contact, nullptr, ownerOf(ball));
        {
            // The dot is a surface when the ball is sitting on it, and a load when
            // it is sitting on the ball. Only the first can hold the ball up.
            const Vec3 gap = ball.position - point_;
            const double distance = length(gap);
            if (distance > 0.0) {
                const Vec3 normal = gap / distance;
                const double closing = dot(ball.velocity - velocity_, normal);
                const double spare = distance - (ball.radius + kPointRadius);
                if (spare <= std::max(0.0, -closing) * kTick) {
                    ball.touching = true;
                    if (normal.y > 1e-9) ball.standing = ball.standing && holds(normal);
                    const double allowed = -std::max(0.0, spare) / kTick;
                    if (closing < allowed) ball.velocity = ball.velocity - normal * (closing - allowed);
                }
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
        {
            const double spare = ball.position.y - ball.radius;
            if (spare <= std::max(0.0, -ball.velocity.y) * kTick) {
                ball.touching = true;
                ball.standing = ball.standing && holds(Vec3{0, 1, 0});
            }
            // The floor takes a speed away only from a ball that is *on* it.
            //
            // A ball still on its way down is not against the floor: it will be
            // against it at the instant the walk finds, and the walk answers that
            // instant with the same bounciness the planks and the shapes get. Taking
            // the speed off here, a tick early, left the ball arriving at the ground
            // at nothing at all -- the bounce had nothing to give back, and every
            // drop the King made ended with a ball lying on the floor instead of
            // coming up off it. Measured: dropped 1.48 m it came back 0.00 m.
            //
            // Nothing is lost by waiting for the contact. The walk solves the first
            // instant the ball's own travel meets the floor -- not a sample of it --
            // so it cannot be passed over however fast the ball is going; and this
            // constraint runs again after every contact and after the grip, which is
            // when a ball really is lying on the floor and really must not keep a
            // speed into it.
            if (spare <= playground_tick::kQuantum) {
                const double allowed = -std::max(0.0, spare) / kTick;
                if (ball.velocity.y < allowed) ball.velocity.y = allowed;
            }
        }
    }
}

// What the ball's contacts answer.
//
// A contact is two things at once: a blow along the normal, which both sides share,
// and a grip across it, which is friction -- and because friction acts at the bottom
// of the ball rather than at its middle, it turns the ball as well as slowing it.
//
// Everything here is answered where the two surfaces met, not where the tick began:
// the ball has moved since, and a blow measured at the start of a tick is a blow
// against a surface the ball never reached.

// The ball against something. The normal part is answered by both sides: if the
// other side can move, the ball keeps whatever it did not hand over. The rest of
// the contact is friction, and because it acts at the bottom of the ball rather
// than at its middle it turns the ball as well as slowing it.
// A face of the ball, recorded for the grip.
//
// The ball is one rigid body against every surface it meets at once, so its grip is
// solved as one system, once a tick, in settleBallFriction() -- and that solve can
// only be as right as the faces it is told about. It used to learn them from
// ballContact() alone, and ballContact() is the shape's meeting with the ball: a
// weight landing on the crown, and every shape that comes to rest on the ball, is
// answered as a *patch* in the body solver instead, which never passes through here.
// A cube standing on the ball was therefore a face nobody had recorded, and the ball
// gave it no grip at all -- it slid off a ball it was pulling with, and the ball
// squirted out from under it. That is the whole of "the ball is too slippery under
// shapes": not a coefficient, not a limit, a contact that was never in the solve.
//
// The normal is the one settleBallFriction() expects: out of the surface and into
// the ball, which is the opposite of a patch's, because a patch's normal runs out of
// the ball into the shape. The load is not written here: what a face carries is a
// question about the whole stack and settleBallFriction() works it out from what is
// standing on the ball.
void PlaygroundRoom::noteBallTouch(Ball& ball, std::size_t body, Vec3 normal, Vec3 at) {
    if (body >= kBodyCount || ball.touchCount >= Ball::kMaxTouches) return;
    const Vec3 surfaceVelocity = bodyFaceSpeed(body, at);
    // One face, one contact: the same meeting can be reported by the walk and by the
    // patch, and it is one surface. Two copies of it would each carry half the real
    // load, and the grip would be half what it should be.
    for (std::size_t i = 0; i < ball.touchCount; ++i) {
        if (ball.touches[i].body != body) continue;
        if (dot(ball.touches[i].normal, normal) < 0.999) continue;
        if (length(ball.touches[i].surfaceVelocity) < length(surfaceVelocity))
            ball.touches[i].surfaceVelocity = surfaceVelocity;
        return;
    }
    ball.touches[ball.touchCount] = BallTouch{normal, surfaceVelocity, 0.0, body};
    ++ball.touchCount;
}

// The gap between a body's outline and the ball's surface: how far apart they are,
// which way the body has to travel to be further from the ball, and the place the
// two of them would touch. Negative when the ball is already into the outline.
//
// The bodies are walked first and the ball afterwards, so for the length of the
// bodies' walk the ball is a surface that stays where it is; this is what the walk
// measures its step against, and what it stops at.
double PlaygroundRoom::bodyBallGap(const Ball& ball, std::size_t index, Vec3& way, Vec3& at) const {
    const Body& body = bodies_[index];
    way = Vec3{0, 1, 0};
    at = ball.position;
    if (body.count == 0) return 1e30;
    const OutlineGap gap =
        outlineGap(ball.position, body.position.data(), body.position.data(), body.count, 1.0);
    way = gap.normal * -1.0;
    // Where the two meet is on the ball's own surface, in the direction the body
    // lies from its centre. It is not the body's nearest point: a round ball pressed
    // against a flat face touches at the foot of the perpendicular, and the body's
    // nearest point is the same place only while the centre is over the face. Take
    // the body's point and a ball wedged on the end of a face is answered half a
    // metre from where it is really pressed, which is a lever about the ball's
    // centre that nothing in the room gave it.
    at = ball.position + way * ball.radius;
    return gap.gap - ball.radius;
}


// Complete the ball tick after the contact walk has spent its travel.
void PlaygroundRoom::finishBallTick(Ball& ball, Vec3 walkStart, Vec3 beganAt, bool held, bool floorAnswered,
                                    bool startedOnFloor, double remaining) {
    const std::size_t ballIndex = std::size_t(&ball - balls_.data());
    const Vec3 weight{0.0, -ball.mass * playground_tick::kGravity, 0.0};
    // What the ball is touching when the tick ends, and what may therefore not be
    // driven into. This runs whether or not the walk found anything, because the
    // walk answers contacts in the order it meets them and the last one it meets
    // is not necessarily the one that has the final say: a body pressing the ball
    // down onto the floor leaves the press in the ball's velocity, and it is the
    // floor that answers it. Touching when the tick ends counts as touching,
    // exactly as it does for the dot: the walk only reports arrivals, and a ball
    // already lying on something never arrives at it again.
    // Every surface the ball is against, asked to hold it out of itself. This is the
    // contact constraint, and it is enforced here and again after the grip: see
    // settleBallSurfaces(ball).
    settleBallSurfaces(ball);
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
        ballContact(ball, Vec3{0, 1, 0}, -1,  ball.position - Vec3{0, ball.radius, 0.0}, 0.0, true, startedOnFloor,
                    1.0 - remaining);

    // The grip is not solved here. It is the last thing the tick does, and the note
    // beside it says why: solved here, its answer is overwritten by the passes below
    // and the speed it was asked to rub out is the one that survives.

    // And the contact constraint once more, because the grip is the last thing that
    // moved the ball and it can push it into a surface it is lying against -- see
    // settleBallSurfaces(ball). Same rule, same place in the order: whatever changed the
    // speed last, the surfaces have the last word after it.
    //
    // The surfaces, though, are only half of that question in a corner. The ball is
    // pressed by two faces at once and both of those faces are standing on something;
    // a grip along one of them pushes the ball into the other, the other answers and
    // leaves a little along the first, and each answer also pushes the shape it came
    // from. A shape pushed off its own floor is a face whose speed is not the speed it
    // really has, and a ball measured against a face that is running away keeps a
    // speed it can never spend -- the churn the King saw in the picture, with the ball
    // standing still and everything in the room moving under it by a hundredth of a
    // metre a second, twenty times a second.
    //
    // The order is the answer, and it is three questions, not a loop:
    //
    //   1. the shapes' own supports, so that every face really is where it ends the
    //      tick -- a shape standing on the floor is a still face, and still is what
    //      the ball is measured against;
    //   2. the ball's faces, solved against those still faces (settleBallSurfaces(ball)
    //      reads them once, when it begins, so a pass cannot see its own push);
    //   3. the shapes' supports again, because the ball has just leaned on them and
    //      what it leaned with is the floor's to answer, exactly as another shape's
    //      weight would be.
    //
    // Repeating the whole thing until it agreed was tried and is wrong: each round
    // hands the shapes another blow, and a round that is asked twice for the same
    // contact applies the same impulse twice -- measured as 8.8 J out of nothing when
    // the dot was dropped on the ball's shoulder. The solve is one pass of the loop
    // the shapes' own solver does, not a repetition of it.
    answerSupportedShapes();
    settleBallSurfaces(ball);
    // And the shapes have the last word on themselves: the blow the ball has just
    // given them is answered by what is under them, exactly as it would be if it had
    // come from another shape. Without this the tick would end with a shape holding a
    // speed into its own floor -- a speed that is not a motion, because the floor
    // takes it back on the next tick, but an arrow and a number that are only there
    // because the ball spoke last.
    answerSupportedShapes();
    // And the grip has the last word on the ball.
    //
    // It is last, and that is not a detail of ordering: it is the difference between a
    // ball that settles and a ball that holds a speed nothing can take off it. The
    // King asked where the air, the rolling resistance and the friction were when a
    // ball wedged in the notch sat at nine tenths of a millimetre a second and never
    // lost it. Measured, they were all present and all doing nothing, for one reason:
    // **none of them was ever shown the speed that survived the tick.** The grip was
    // solved before the last two answers, and those answers changed the ball's speed
    // after it -- the grip measured a slip of 4.3 mm/s along the slope, answered it,
    // and then the contact passes and the shapes' supports left the ball with a
    // different velocity altogether (measured: grip left -0.0343,-0.1418; the tick
    // ended at +0.00089,+0.00089 with a slip of 1.26 mm/s at that same face). No
    // force can act on a speed that appears after the solve, so the slip was never
    // rubbed out; the ball carried it into the next tick, the wedge took the
    // gravity-driven part of it back, and the tick ended on the same number again.
    // That is a fixed point of the tick map, not a decaying motion. It also explains
    // why the ball *travelled* nowhere while holding it: the walk refuses the travel
    // of a ball already against two faces, so no displacement ever happened, and a
    // friction force with no displacement does no work -- the room drew the arrows
    // and the energy never moved.
    //
    // Rolling resistance was in the same position and for the same reason. It is a
    // couple on the turn (M = Crr * N * r, see settleBallFriction(ball)), and it was
    // taking the turn out of the ball every tick -- and every tick the grip, running
    // earlier, put it straight back by closing a slip that had already been answered.
    // The turn measured -0.0229 rad/s for as long as the room was left alone, exactly
    // the turn that 1.26 mm/s of slip at that face would keep feeding it.
    //
    // With the grip last, the state the tick ends in is one the whole contact system
    // has agreed on: friction is a push along a face, so it cannot open a closing into
    // one, and the surfaces do not need to answer it afterwards. Measured after the
    // change, at the same corner: 21 of 21 drop positions settle at exactly 0.0000
    // m/s, the ball's turn decays to exactly 0.0000 rad/s, and the room is unchanged
    // after 1400 ticks (70 s). Nothing was added to damp it -- the same air, the same
    // friction, the same Crr; they were simply given the speed to act on.
    settleBallFriction(ball);
    // And the shapes have the last word on themselves, again: the grip is a pair, and
    // the half of it that landed on a shape has just given that shape a speed its own
    // floor answers. Left unanswered until the next tick, that speed is a face moving
    // for exactly one tick -- long enough for the ball's grip to be measured against
    // it and dragged along by a surface that never moves.
    answerSupportedShapes();

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
        const Vec3 support = total - ball.blows - weight * kTick;
        if (length(support) > 1e-9) {
            const Vec3 where = length(ball.supportNormal) > 0.0 ? ball.supportNormal : Vec3{0, 1, 0};
            addForce(ball.position + where * (-ball.radius), support * (1.0 / kTick), ForceKind::Contact, nullptr,
                     ballOwner(ballIndex));
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
    // And the walk is finished the way a shape's is: with the trapezoid.
    //
    // The ball is carried through its tick at the speed it *arrived* with, and the
    // answers it gets -- the floor, a face, the grip -- take that speed away somewhere
    // along the way. A move that is one speed for the whole tick and a different speed
    // at the end of it is not the motion that happened: half of the change belongs in
    // the travel, which is exactly the rule the shapes follow (see advanceBodies).
    // Measured in the King's corner: the ball's place moved 12.2 um every second tick
    // while its speed at the end of every tick was 0.93 um a tick's worth, and the
    // difference -- eleven microns of travel that no speed of the ball accounts for --
    // is a ball that is standing still and still moving.
    if (!ball.bent && length(ball.velocity) < length(walkStart)) {
        const Vec3 slower = (ball.velocity - walkStart) * (0.5 * kTick);
        // Only ever backwards along the way it was already going, and never upwards:
        // a correction that lifted the ball would be a height it never fell from.
        if (slower.y <= 0.0 && dot(slower, walkStart) <= 0.0) ball.position = ball.position + slower;
    }
    // A ball that ends the tick where it began it went nowhere, so the speed that
    // would have carried it is not a speed: it is a number below the room's own
    // resolution that nothing in the room can ever spend, and every surface it leans
    // on reads it as if the ball were moving. Measured: a ball wedged under the
    // triangle's apex held (-1.2e-5, -8.9e-6) m/s tick after tick with its place fixed
    // to a hundredth of a micron, and that is what its grip was made of.
    if (ball.position.x == beganAt.x && ball.position.y == beganAt.y && ball.position.z == beganAt.z)
        ball.velocity = {};
    // A ball the ground is carrying is never in flight, and the distance it covers in
    // a tick is the speed it ends the tick with -- not the speed it was pushed to
    // part-way through it.
    //
    // This is the same rule the shapes follow: a body that is in contact travels as
    // the solver left it, and a body in free flight follows a parabola. The ball was
    // the one thing exempt, and it is the difference that would not die. A ball lying
    // on the floor with the triangle's slope leaning on it is pushed a third of a
    // millimetre a second along the floor by that slope during the shapes' half of
    // the tick, the grip takes all of it back at the end of the ball's half, and the
    // walk in between was already made -- so the ball crossed 12 um of floor, was
    // stopped by a friction that no displacement showed, and did it again next tick.
    // Measured over 400 ticks of the King's corner: 5.4 mm of travel across the scene
    // by a ball whose speed at the end of every tick was three hundredths of a
    // micrometre a tick. A ball in contact does not fly, so its travel is its answer.
    //
    // And it may only ever shorten the walk, never lengthen it and never turn it
    // round. A ball that met something had its place decided by the meeting -- the
    // wall stopped it, and a ball bouncing off a wall ends the tick with a speed
    // pointing the other way, so "where it would have got to at its final speed" is
    // behind where it really is. Taking that as the answer put the ball back through
    // the wall it had just bounced off: the check that fires one at a side read a
    // restitution of 0.0000 against the floor's 0.8120.
    if (held) {
        const Vec3 shorter = beganAt + ball.velocity * kTick;
        const Vec3 correction = shorter - ball.position;
        if (dot(correction, walkStart) <= 0.0) ball.position = shorter;
    }
    // The path the drawing is given, rebuilt from the place the tick really ended.
    // A tick whose travel is resolved away does not end at the speed it had after the
    // meeting: the walk stops at the face, and the speed that carried the ball there is
    // spent. Drawn from that speed, the ball's second piece of path flew on past the
    // face -- 6.2 cm into the cube in the King's corner -- and the frame at the end of
    // the tick snapped back to it: a ball that flies into a shape every tick and is
    // yanked back reads as lag, and it is a lie about where the ball went. Both ends of
    // the piece are known here, so it is read off directly. This is the drawing's copy
    // of the path only; no velocity, force or travel in the room reads it.
    if (ball.bent) {
        const Vec3 met = ball.bendPoint;
        if (ball.bendAt > 0.0) ball.bendIn = (met - ball.previous) / (kTick * ball.bendAt);
        const double left = 1.0 - ball.bendAt;
        if (left > 0.0) ball.bendOut = (ball.position - met) / (kTick * left);
    }
    // Whether anything in the room answered this tick. A ball against something has been
    // answered even if the answer was "no" -- a face holding it up is a face it is not
    // falling through -- and a ball whose speed is no longer the speed its own weight and
    // air would have left it with has been answered too. That second reading is what
    // catches the answer a blow leaves behind after the ball is already clear of what
    // struck it, and the solver's answers, which never pass through the walk at all.
    ball.answered = ball.answered || ball.touching ||
                    length(ball.velocity - ball.alone) > playground_tick::kQuantumSpeed;
    // And a turn that carries the rim less than a micrometre is not a turn.
    ball.spin = playground_tick::resolvedTurn(ball.spin, ball.radius);
    ball.angle += ball.spin * kTick;
}

// Every surface the ball is already resting on, gathered before the tick is solved.
//
// Only a surface it is *on*, not one it is falling towards: an arrival is the walk's
// business, where the bounce is. A ball within a micrometre of a surface is on it --
// that is the room's whole idea of touching.
void PlaygroundRoom::gatherBallSupports(Ball& ball) {
    ball.supportCount = 0;
    const auto hold = [&](Vec3 normal, std::size_t body) {
        const double size = length(normal);
        if (!(size > 0.0) || ball.supportCount >= Ball::kMaxSupports) return;
        ball.supports[ball.supportCount++] = BallSupport{normal * (1.0 / size), body};
    };
    // The floor, and only the floor.
    //
    // A support is what the ball may not be driven into and what takes the answer
    // when it is. That is a statement about something that cannot move: the ground.
    // A shape is not that. Listing a shape the ball is leaning on as a support says
    // the ball cannot move along the way that shape presses it -- true, as far as it
    // goes -- but then the whole of the impulse is handed to the shape on the other
    // side, as though the ball were stone, and the shape that is doing the pressing
    // is given a blow the ball never took. A ball wedged between the triangle's face
    // and the end of the seesaw's plank was declared unable to move, and the plank
    // was answered with four hundred and seventy newton seconds of impulse from a
    // ball carrying two: the plank left at fifteen radians a second and the ball at
    // thirty-three metres a second, out of a touch at three.
    //
    // A shape that holds the ball up is a contact and not a support, and it answers
    // through the contact: the ball takes the push, the shape takes it back, the
    // floor takes it from the shape. That chain is the room's own ledger, and every
    // step of it is momentum somebody actually had. What the floor answers is
    // answered by nobody and costs nothing, which is why it is the one that may be a
    // support.
    if (ball.position.y <= ball.radius + playground_tick::kQuantum) hold(Vec3{0, 1, 0}, kBodyCount);
}


}  // namespace gtc::native
