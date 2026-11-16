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
constexpr int kBallSurfacePasses = 64;

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
        for (int pass = 0; pass < kBallSurfacePasses; ++pass) {
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

}  // namespace gtc::native
