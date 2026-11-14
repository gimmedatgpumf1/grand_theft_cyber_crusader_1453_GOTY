#include <cstdio>
#include <cstdlib>
#include "client/playground_ball_shape.h"
#include "client/playground_shapes.h"
#include "client/playground_room_tick.h"

namespace gtc::native {
using namespace ball_shape;
using namespace playground_detail;

// How many times a tick's standing contacts are solved over before their answers are
// taken to agree. A face or two settles in a couple of passes; a corner of three is
// slower, and a limit this high is never the answer the loop ends on -- it ends when a
// whole pass has nothing to add, or when what it does add is below the room's own
// resolution. It is here so that a machine cannot be left spinning on a corner that
// will not close.
constexpr int kBallSurfacePasses = 64;

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
void PlaygroundRoom::settleBallSurfaces() {
    Ball& ball = ball_;
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
        const auto holds = [](Vec3 normal) {
            return kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));
        };
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
                addForce(slot.at, slot.impulse, ForceKind::Contact, nullptr, kBallOwner);
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
void PlaygroundRoom::noteBallTouch(std::size_t body, Vec3 normal, Vec3 at) {
    if (body >= kBodyCount || ballTouchCount_ >= kMaxBallTouches) return;
    const Vec3 surfaceVelocity = pointSpeed(bodies_[body], at);
    // One face, one contact: the same meeting can be reported by the walk and by the
    // patch, and it is one surface. Two copies of it would each carry half the real
    // load, and the grip would be half what it should be.
    for (std::size_t i = 0; i < ballTouchCount_; ++i) {
        if (ballTouches_[i].body != body) continue;
        if (dot(ballTouches_[i].normal, normal) < 0.999) continue;
        if (length(ballTouches_[i].surfaceVelocity) < length(surfaceVelocity))
            ballTouches_[i].surfaceVelocity = surfaceVelocity;
        return;
    }
    ballTouches_[ballTouchCount_] = BallTouch{normal, surfaceVelocity, 0.0, body};
    ++ballTouchCount_;
}

void PlaygroundRoom::ballContact(Vec3 normal, int bodyIndex, bool ballHeld, Vec3 contactPoint, double depth,
                                bool exchange, bool resting, double reached) {
    Ball& ball = ball_;
    Body* body = bodyIndex >= 0 ? &bodies_[std::size_t(bodyIndex)] : nullptr;
    // What the ball was doing before this surface answered it. When the call is
    // done, the difference is the momentum the surface took from it or gave it,
    // which is the force the ball's arrows are drawn from -- recorded where the
    // two met, because that is where it acts.
    if (std::getenv("KV")) std::printf("  [bc] body%d n(%+.4f,%+.4f) exch%d rest%d depth%+.9f reached%+.4f v_in(%+.5f,%+.5f) at(%.9f,%.9f)\n", bodyIndex, normal.x, normal.y, int(exchange), int(resting), depth, reached, ball.velocity.x, ball.velocity.y, contactPoint.x, contactPoint.y);
    const Vec3 ballBefore = ball.velocity;
    const double spinBefore = ball.spin;
    // The body's side is written by giveBodyTurn, which is the call that hands it
    // over.
    // The ball's *turn* is written here; its speed is written further down, as the
    // pair of blows that changed it. A blow is one force at one place, and the ball
    // is the one thing in the room that is pressed from two sides at once: what hit
    // it, and what is holding it up. Written as a single net push it reads as a
    // force with no partner, which is what a conjured force looks like.
    const auto recordBall = [&]() {
        // And the turn it took, as the couple that turned it. A blow off the middle
        // of the ball changes its spin as well as its speed, and a blow is a force
        // whose moment is the arm it lands on -- so if the spin changed, the moment
        // that did it belongs in the ledger too, or the picture adds up in a straight
        // line and not about the middle.
        const double inertia = 0.4 * ball.mass * ball.radius * ball.radius;
        const double turned = inertia * (ball.spin - spinBefore) / kTick;
        if (std::abs(turned) > 1e-9) addCouple(contactPoint, turned, Vec3{}, ForceKind::Ball, kBallOwner);
    };
    Vec3 answer = normal;
    bool canMove = false;
    // The blow this shape gives the ball, as an impulse along the way out of the
    // shape. Every contact is two blows at one place, and both of them belong on
    // screen: this is the shape's, and it is written even when the ball does not
    // move under it -- because something under the ball did.
    Vec3 blown = {};
    // A body at rest is not stepped, and what it is not stepping it cannot give
    // either. Letting it take a share of the ball's momentum and then discarding
    // that share is not a contact, it is a pump: the ball keeps more speed than a
    // solid surface would leave it and ends up rolling under whatever is on top of
    // it faster and faster. So a resting body answers as the solid it is, and a
    // real blow wakes it -- which is the only thing that should ever move a body
    // that had come to rest.
    if (body != nullptr && body->frozen) {
        // A body asleep is a body the ball can wake, and only a real blow does it:
        // what the two are closing at, along the normal they met on.
        const Vec3 closingVelocity = ball.velocity - pointSpeed(*body, contactPoint);
        if (std::abs(dot(closingVelocity, normal)) > kRestSpeed) body->frozen = false;
    }
    if (body != nullptr) {
        // The ball's contact is answered by the body's own mass and its lever about
        // the body's centre -- the same law every other contact in the room
        // follows, at the place the two of them actually met. A body the mouse is
        // carrying has nowhere to go, and neither has one that is asleep until a
        // real blow wakes it.
        answer = (dragTarget_ == DragTarget::Body || body->frozen) ? Vec3{} : normal;
        canMove = dot(answer, normal) > 1e-9;
    }
    // What the body's step was before anything below shortens it. A corner's stored
    // velocity is not the body's motion -- the floor cuts it, and a shortened step
    // is not motion either -- so the step is read here, once, and used from here on.
    //
    // It is the speed *where the surfaces touch*, not the body's average speed: a body
    // tipping over a ball has one edge standing still and the other going twice as
    // fast as the middle, and the piece between the corners is moving at the lever's
    // interpolation of the two of them -- which is what outlineTouch already says for
    // the position. Reading the average instead says a body turning on the spot is not
    // moving at all where it is touching, so a shape rocking on a ball never rubbed it.
    const Vec3 surfaceVelocity = body != nullptr ? pointSpeed(*body, contactPoint) : Vec3{};

    // A ball found through an outline is the ball's own way out: the body's tick
    // keeps its outline clear of the ball -- that is what the sweep in tickBody is
    // for -- so what is left for the ball is where the ball itself has come to be
    // inside the shape. The ball goes back out along the way it is nearest to
    // being outside, and the floor is the one thing it cannot go out through.
    // A depth under a micrometre is touching, not buried. The sweep lands the ball
    // on the surface and the bisection leaves it a nanometre either side of it, so
    // "any depth at all" entered this recovery path on a contact that was perfectly
    // ordinary -- and the path cancels the speed the ball arrived with, before the
    // bounce has been worked out. That is a ball that stops dead on one bounce and
    // bounces properly on the next, decided by which side of a nanometre it landed.
    if (depth > playground_tick::kQuantum) {
        const OutlineGap now = outlineGap(ball.position, body ? body->previous.data() : nullptr,
                                          body ? body->position.data() : nullptr, body ? body->count : 0, 1.0);
        normal = now.normal;
        const double through = ball.radius - now.gap;
        // A ball a micrometre in is touching, not inside: the room's resolution is a
        // micrometre in a tick, so moving it back by less than one is moving it by
        // nothing, and only a depth the simulation can tell from touching is a depth
        // to be put right.
        if (through > playground_tick::kQuantum) {
            // The floor is under the ball, so a way out that goes downwards is no
            // way out: what is left over is the ball rolling along the floor, out
            // from under whatever is on it, rather than being left with a shape cut
            // into it.
            const double ground = (normal.y < 0.0)
                                      ? std::max(0.0, (ball.radius - ball.position.y) / normal.y)
                                      : through;
            const double byBall = std::min(through, ground);
            ballLiftedOut_ += byBall > kSkin ? 1 : 0;
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
        // The speed it arrived with is not cancelled here. Putting the ball back on
        // the surface says where it is; what it does next is the contact's answer,
        // and that is worked out below -- the bounce included. Cancelling it first
        // left the answer with nothing to be about, which is how a ball dropped on a
        // cube stopped dead halfway through its bounces instead of losing height
        // steadily. Anything still pointing into the surface after the answer is
        // taken off at the end of the tick, where it belongs.
    }

    if (!exchange) {
        // The blow for this body was struck earlier in the tick. What is left is
        // where the two are, and the ball may not travel into it any further.
        const double into = dot(ball.velocity - surfaceVelocity, normal);
        if (into < 0.0) ball.velocity = ball.velocity - normal * into;
        if (normal.y > 1e-9)
            ball.standing = ball.standing &&
                            kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));
        // That stop is a contact like any other, and a contact the ball feels is a
        // force on it -- so it is written, and it goes in the tick's account. A
        // second meeting with the same shape in one tick used to be answered in
        // silence: the ball was stopped by a surface that appeared nowhere on screen
        // and in no sum, and the ground's share (worked out as the remainder) carried
        // a blow the shape had really given. It is not a bounce -- the blow has
        // already been struck -- but it is not nothing either.
        const Vec3 stopped = (ball.velocity - ballBefore) * ball.mass;
        ballBlows_ = ballBlows_ + stopped;
        if (length(stopped) > 1e-9)
            addForce(contactPoint, stopped * (1.0 / kTick), ForceKind::Contact, nullptr, kBallOwner);
        recordBall();
        return;
    }

    // Both sides stop where they met: the ball loses the part of its speed that
    // was driving into the shape, and the shape takes the blow. The bodies are
    // stepped before the ball, so where the body is, is where it has already been
    // solved to be -- the ball is the one that has travelled since.
    // What the struck corner actually did this tick. Its stored velocity can be
    // zero -- the floor cuts it when the corner lands -- while the corner is still
    // travelling down into the ball, and the ball has to answer the travel.

    // How fast the two are closing *where and when they meet* -- both measured at
    // one instant, the instant of the contact itself.
    //
    // It used to be measured with the ball's speed from the start of the tick and
    // the surface's speed from the end of it, and those are not the same moment. In
    // one tick of this room gravity alone is half a metre a second, and a plank at a
    // radian a second moves its face by a third of one, so the two numbers were taken
    // half a metre a second apart and the difference was handed back as a bounce. A
    // ball resting on a moving seesaw was answered as if it had arrived at a metre a
    // second when it had arrived at nothing at all: a fresh bounce every tick, from a
    // contact that was never an impact.
    //
    // The ball's speed at the meeting is its speed now, and the surface's is read
    // from the very contact point it is being asked about.
    const double closing = dot(ball.velocity - surfaceVelocity, normal);
    // What the ball was doing at the instant it met the surface. Its speed is carried
    // through the tick from `arrival` to what it is now, so the fraction of the way
    // through the tick is the fraction of the way from one to the other: at the very
    // moment of a contact, that is the speed the blow is answered to.
    const Vec3 atMeeting = ball.arrival + (ball.velocity - ball.arrival) * std::clamp(reached, 0.0, 1.0);
    const double approach = dot(atMeeting - surfaceVelocity, normal);
    // A bounce is the answer to arriving at a surface. A ball that was already lying
    // on this one when the tick began did not arrive at it: the two are in contact,
    // the contact is being kept, and what it may not do is close. It gets no blow
    // back -- and that is not a damping rule, it is what restitution means. Applied
    // to a sustained contact it is a pump: a ball riding a turning plank is closing
    // on that plank's face every tick by a few millimetres, because the face is
    // moving, and three quarters of that was handed back as a launch each tick. The
    // ball left the plank, fell for a tick, landed, and was launched again -- the
    // plank was loaded on every other tick and coasted the ticks between, and that
    // square wave of angular acceleration is the stop-move-stop-move on screen.
    const double rebound = (approach < 0.0 && !resting) ? -kBallRestitution * approach : 0.0;
    double pressed = 0.0;
    if (closing < 0.0) {
        if (canMove) {
            // A body struck by the ball takes the blow as the solid it is: every
            // corner of it moves together, turning about the middle by the amount
            // that blow would turn it. Where the two shapes are touching, not the
            // nearest corner: the ball meeting the middle of a face turns the body
            // by nothing, and a corner of a face is not where the blow lands.
            // Is the shape the ball is hitting standing on the floor? Its lowest
            // corner within a micrometre of it is the shape being carried, and a
            // shape being carried cannot be pushed further into what carries it.
            bool onFloor = false;
            if (normal.y > 0.0 && body->count > 0) {
                double lowest = body->position[0].y;
                for (std::size_t i = 1; i < body->count; ++i) lowest = std::min(lowest, body->position[i].y);
                onFloor = lowest <= playground_tick::kSurfaceY + playground_tick::kQuantum;
            }
            const double bodyShare = giveAtHeld(*body, contactPoint, answer, onFloor);
            // What the ball can take of this blow is only what it is free to do with
            // it. A ball already held by a surface, or one on the floor that the
            // blow would drive downwards, has nowhere to go: the body takes the
            // whole answer, and stops on the ball instead of sinking into it and
            // being answered again next tick.
            // A face above the ball pressing it down is a face the ball has
            // nowhere to go under: the floor is under it and the floor wins. But
            // "above" has to mean above. A face that is level points sideways, and
            // in floating point it points sideways by about a part in a million
            // billion -- so testing the sign alone said a horizontal face was
            // pressing the ball into the floor whenever the ball happened to be
            // resting on it, gave the ball no say in the blow, and a ball that ran
            // into the side of a cube stopped dead instead of bouncing off it.
            // Bounciness is not a property of up, and a side hit is a hit.
            // "Over the ball" is a statement about the face, and the question it
            // answers is which of the two it is: a face that is more above than
            // beside, or more beside than above. A face whose normal is closer to
            // straight down than to level is over the ball and the floor under it
            // wins; a face that is nearer level is a wall, and a ball pressed against
            // a wall rolls along the floor out of its way. Anything with a number in
            // it -- "more than a microradian", "more than a millionth" -- answers a
            // different question, the one about how level a face happens to be in
            // floating point, and says a brick wall is a low ceiling.
            // The ball's say in the blow, as an inverse mass: none along the way the
            // surface under it will not let it go, and the whole of it along the way
            // that surface is not in the way. That is what turns "a shape landing on
            // the top of it" into the floor's answer while "a shape striking its
            // side" still shoves the ball across the room.
            const double ballShare = ballCompliance(normal, ballHeld);
            const double impulse = (rebound - closing) / (ballShare + bodyShare);
            ball.velocity = ball.velocity + normal * (impulse * ballShare);
            // Struck is not asleep. A ball given any of this blow has something to
            // do with it, and a frozen ball is not stepped at all.
            if (impulse != 0.0) ball.frozen = false;
            // What this surface can rub against the ball is what it can press the
            // ball with, and the ball is only pressed by the part of the blow it
            // actually took. A ball lying on the floor with a shape landing flat on
            // top of it takes none of that blow -- the floor under it does -- and a
            // friction solve given the whole of it span the ball sideways and down
            // at metres a second off a body that never touched it that way.
            // The shape is handed the whole of it, and the floor is left to answer
            // its own half.
            //
            // It used to be handed only the part that was not pointing down: the rest
            // was said to be "the floor's to answer", and it was taken off the ball
            // and given to nobody. That is momentum leaving the room. Measured on the
            // King's own drop -- the ball let go above the triangle's hypotenuse --
            // the ball gave up 3.39 m/s along the face's normal, the triangle received
            // (-1.487, +0.000) newton seconds, and the other 1.487 vanished: the ball
            // came off the slope at a fifth of the blow it had landed with, and the
            // slope had not moved to account for it. The floor can answer a downward
            // blow, and it does -- it is a contact like any other, it is in the ledger,
            // and it is solved on the next tick. What it cannot do is answer for a
            // force that was never applied.
            //
            // Handing the whole blow over also makes the pair add up in the arithmetic
            // that solved it: the impulse was worked out against the shape's own
            // compliance, and a shape given less than that keeps a speed that
            // disagrees with the relative velocity the solve was aiming for -- which
            // is restitution quietly not delivered.
            Vec3 handed = answer * -impulse;
            pushBody(*body, contactPoint, handed, ForceKind::Ball);
            blown = handed * -1.0;
            pressed = impulse * ballShare;
        } else {
            // Nowhere to go: the ball takes the whole answer itself.
            const double impulse = (rebound - closing) * ball.mass;
            ball.velocity = ball.velocity + normal * (impulse / ball.mass);
            blown = normal * impulse;
            pressed = impulse;
        }
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
    if (normal.y > 1e-9) {
        ball.standing = ball.standing &&
                        kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));
        // The surface that is under it, remembered: a ball left standing on
        // something is pressed at this point, so this is where its weight comes
        // back up and where the arrow for it belongs.
        ball.supportNormal = normal;
    }

    // A contact that is not a blow is still a contact. The surface carries the
    // ball's weight for as long as the ball is on it, and friction is that weight
    // times mu -- the same thing the floor already answers, because gravity drives
    // the ball into the floor and the ball loses that speed to it every tick. A
    // slope is not that: the ball runs *along* the face, so nothing drives it into
    // the face and the blow is nothing, and with no normal force there was no
    // friction at all. The seam stood still while the ball ran away down the face
    // and then jumped when the next blow arrived. What the surface carries when it
    // is only holding the ball up is the weight pressed into it, over the tick.
    // A face over the ball is not holding it up, so the ball's own weight is not
    // pressed into it: what is pressed into it is whatever stands on the ball, and
    // that is read from the body over it in settleBallFriction().
    if (!(pressed > 0.0) && normal.y > 0.0)
        pressed = kGravity * ball.mass * normal.y * kTick;

    // Friction is not applied here. A grip at one surface changes what is left to
    // slip at every other surface the ball is against, so the whole tick's contacts
    // are solved against each other once they are all known -- see
    // settleBallFriction(). What is recorded is what each contact is: the face, how
    // it is moving there, and how hard it is pressed.
    // Every face the tick met is recorded, whether or not this tick's answer gave it
    // a load of its own. A face with a weight standing on it carries one whether or
    // not anything moved, and that load is worked out over all the faces together in
    // settleBallFriction(): a face over the ball carries what stands on it, and a
    // face under it carries the rest. Dropping the ones whose own answer was nothing
    // is what left the ball with no grip underneath a weight that was resting on it.
    // One face, one contact. The same face can be met by more than one road in a
    // tick -- the walk, and the standing touch that has to exist even when the ball
    // did not travel -- and it is still one face. Answered twice it is not twice the
    // surface: it is one surface whose load is shared between two copies of itself,
    // so each copy presses with half the force that is really there, and the ball's
    // grip on it is half what it should be. A pinched ball had the floor counted
    // twice under it, and the floor was then no stronger than the weight above.
    if (ballTouchCount_ < kMaxBallTouches) {
        const std::size_t mate = bodyIndex < 0 ? kBodyCount : std::size_t(bodyIndex);
        std::size_t slot = ballTouchCount_;
        for (std::size_t i = 0; i < ballTouchCount_; ++i) {
            if (ballTouches_[i].body != mate) continue;
            if (dot(ballTouches_[i].normal, normal) < 0.999) continue;
            slot = i;
            break;
        }
        if (slot == ballTouchCount_) {
            ballTouches_[slot] = BallTouch{normal, surfaceVelocity, pressed, mate};
            ++ballTouchCount_;
        } else {
            ballTouches_[slot].pressed = std::max(ballTouches_[slot].pressed, pressed);
            if (length(ballTouches_[slot].surfaceVelocity) < length(surfaceVelocity))
                ballTouches_[slot].surfaceVelocity = surfaceVelocity;
        }
    }
    // Rolling resistance is not done here, and it is not a force taken off the
    // ball's speed. It is a couple, it is applied to the ball's turn, and it is
    // applied once for the whole tick in settleBallFriction(), against the load the
    // contacts are really carrying. See that function: a ball that is rolling
    // perfectly has nothing sliding anywhere for friction to act on, and something
    // has to be what stops it rolling for ever.

    // And whatever is left pointing into the surface is speed the ball cannot spend.
    // The bodies are stepped before the ball, so what a body gave way by is already
    // in its positions and not in what is left of the tick: for the rest of the tick
    // this surface is where it is, and a ball that kept its inward part would drive
    // itself through a face that has already finished moving -- which is exactly
    // what a ball landing on a shape that gives a little under it does. A bounce has
    // turned the ball around before this runs, so nothing that was leaving is caught.
    // Measured against the surface's own motion, not against the room. A face that
    // is travelling -- a turning plank, a shape a weight is pushing over -- is where
    // the ball may not go *relative to it*, and asking the question of the ball's
    // speed alone answered something else: it took the surface's motion for speed
    // the ball had and could not spend, and paid it back as a blow. On a seesaw at a
    // radian a second that was a third of a metre a second of speed conjured out of
    // nothing, every contact, along the normal -- which both threw the ball clear of
    // the very face it had just been answered on and left the plank unloaded for the
    // ticks the ball spent falling back. The ball was in the air half the time and
    // the plank was kicked the other half: stop, move, stop, move.
    {
        const double into = dot(ball.velocity - surfaceVelocity, normal);
        if (into < 0.0) ball.velocity = ball.velocity - normal * into;
    }
    // What this shape gave the ball, drawn where the two met.
    //
    // It is written even when the ball's speed does not change by it, because what
    // the ball's speed does is not the whole story: a ball on the floor is answered
    // by the floor, and the blow it took and the blow it handed back are two forces
    // at one place, not none. The other half -- what the ground supplied -- is worked
    // out once at the end of the tick, from the ball's account, in tickBall().
    ballBlows_ = ballBlows_ + blown;
    if (length(blown) > 1e-9) addForce(contactPoint, blown * (1.0 / kTick), ForceKind::Contact, nullptr, kBallOwner);
    recordBall();
}

// The ball runs into the dot. Both have mass, so they exchange momentum along the
// line between them.
void PlaygroundRoom::hitBallWithPoint(Vec3 normal) {
    Ball& ball = ball_;
    const Vec3 ballBefore = ball.velocity, pointBefore = velocity_;
    const double spinBefore = ball.spin;
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
    // The dot and the ball are two bodies in one blow: each one's share is written
    // down at the place they met, so F shows the pair of arrows the exchange is.
    {
        const Vec3 onBall = (ball.velocity - ballBefore) * (ball.mass / kTick);
        const Vec3 onPoint = (velocity_ - pointBefore) * (kMass / kTick);
        if (length(onBall) > 1e-6) addForce(ball.position, onBall, ForceKind::Ball, nullptr, kBallOwner);
        ballBlows_ = ballBlows_ + (ball.velocity - ballBefore) * ball.mass;
        const double inertia = 0.4 * ball.mass * ball.radius * ball.radius;
        const double turned = inertia * (ball.spin - spinBefore) / kTick;
        if (std::abs(turned) > 1e-9) addCouple(point_, turned, Vec3{}, ForceKind::Ball, kBallOwner);
        if (length(onPoint) > 1e-6) addForce(point_, onPoint, ForceKind::Ball, nullptr, kDotOwner);
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

// The dot runs into the ball. The normal points from the ball to the dot, so the
// dot is the one that gets pushed back, and whatever it hands over is what the
// ball takes.
void PlaygroundRoom::pointAgainstBall(Vec3 normal) {
    Ball& ball = ball_;
    const double closing = dot(velocity_ - ball.velocity, normal);
    // The ball is the elastic one, so a blow against it answers with the ball's
    // restitution -- the same number every other thing that meets it is answered
    // with. This used to be zero, which made the dot a special case that the ball
    // was not bouncy towards: a dot dropped on a ball sank into it and stopped, and
    // a ball with a dot on it never threw it back, whatever the material said.
    const double rebound = closing < 0.0 ? -kBallRestitution * closing : 0.0;
    if (closing < 0.0) {
        // The ball's say in the blow, along the direction the blow pushes it. The
        // dot pushes the ball along -normal (the normal runs from the ball to the
        // dot), and what the ball can do with that is whatever is not blocked by
        // the surface it is standing on. Straight down onto a ball on the floor is
        // the floor's to answer and the dot bounces off the ball; across it is the
        // ball's, and the ball goes.
        const double ballShare = ballCompliance(normal * -1.0, dragTarget_ == DragTarget::Ball);
        const double impulse = (rebound - closing) / (1.0 / kMass + ballShare);
        velocity_ = velocity_ + normal * (impulse / kMass);
        ball.velocity = ball.velocity - normal * (impulse * ballShare);
        ballBlows_ = ballBlows_ - normal * (impulse * ballShare);
        // A ball that has been struck is a ball that is doing something, and a ball
        // that is doing something is not asleep. It used to be woken here and then
        // given no speed at all, which is a ball that wakes up and stands still.
        ball.frozen = false;
        pointFrozen_ = false;
        resting_ = false;
        restingBody_ = -1;
    }
    const Vec3 gap = point_ - ball.position;
    const double distance = length(gap);
    const double overlap = kPointRadius + ball.radius - distance;
    if (overlap > 0.0 && distance > 1e-12) {
        // The dot is the thing that is where it should not be, so the dot is the
        // one that moves -- unless the ball can take its share, which is the same
        // question as above. Pushing the ball half the overlap downwards put it
        // through the floor it was resting on.
        const Vec3 way = gap * (1.0 / distance);            // ball -> dot
        const double ballCan = ballCompliance(way, dragTarget_ == DragTarget::Ball) * ball.mass;
        const double ballPart = std::clamp(0.5 * ballCan, 0.0, 1.0);
        point_ = point_ + way * (overlap * (1.0 - ballPart));
        ball.position = ball.position - way * (overlap * ballPart);
    }
}

}  // namespace gtc::native
