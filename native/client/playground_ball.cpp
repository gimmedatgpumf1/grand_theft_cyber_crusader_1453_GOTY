#include <cstdio>
#include "client/playground_ball_shape.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <cstdlib>

namespace gtc::native {
extern int gtcAudit;
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
        const auto holds = [](Vec3 normal) {
            return kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));
        };
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            Body& body = bodies_[index];
            for (std::size_t edge = 0; edge < body.count; ++edge) {
                const std::size_t top = (edge + 1) % body.count;
                const Vec3 a = body.position[edge];
                const Vec3 b = body.position[top];
                const Vec3 side = b - a;
                const double sideLength = side.x * side.x + side.y * side.y;
                const double t = sideLength > 0.0 ? std::clamp(dot(ball.position - a, side) / sideLength, 0.0, 1.0) : 0.0;
                const Vec3 gap = ball.position - (a + side * t);
                const double distance = length(gap);
                // Within the room's own resolution of the surface, not within a nanometre
                // of it. The sweep lands the ball on the face and the bisection leaves it
                // a few micrometres either side; read at a nanometre, a ball lying in a
                // pinch was not touching anything at all -- so no grip, no friction, and
                // no rest, and a millimetre a second of spin and slide that never died.
                if (distance <= 0.0) continue;
                const Vec3 normal = gap / distance;
                const Vec3 surface = pointSpeed(body, a + side * t);
                const double closing = dot(ball.velocity - surface, normal);
                // A face the ball will reach before this tick is over is a face it is
                // in contact with for the whole tick, and it is answered now.
                //
                // Answering only the faces it was *already* touching, to the room's
                // micrometre, is what made a ball on a moving shape chatter: the
                // shape moved out from under it, the ball was a millimetre clear, and
                // a contact the ball was about to have went unanswered -- it fell, met
                // the face, was answered, left it again, fell again, at ten hertz. The
                // distance a tick can close is `closing * kTick`, and inside that the
                // contact is already the truth about this tick.
                // `spare` is the gap left between the ball and this face *now*, at the
                // end of the tick, and only a ball that is against the face -- within
                // the room's own micrometre of it -- is answered by it.
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
                // How fast it may close is what the gap allows in what is left of the
                // tick: a ball a millimetre above a face may not cross more than a
                // millimetre of it, and a ball touching it may not close at all. That
                // is the whole of the constraint -- no correction of place, and no
                // speed taken that the surface did not take.
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
                    addForce(ball.position - normal * ball.radius, blow, ForceKind::Contact, nullptr, kBallOwner);
                }
            }
        }
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

void PlaygroundRoom::tickBall() {
    Ball& ball = ball_;
    // The list of faces is not emptied here. The tick opens it before the bodies are
    // solved -- see tickBodies(), which fills it with every shape that is against the
    // ball -- and this half of the tick only adds the faces the ball's own walk meets.
    // Emptying it here threw away the shape the ball was carrying, which is the one
    // face it most needs a grip on: a cube resting on the crown was registered, then
    // erased, and the ball went on being greased under a weight it was pressing on.
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
    // Was the ball already lying on the floor when this tick began? A ball that was
    // is in contact, and a contact is not an arrival: it may not bounce off the
    // ground every tick, or gravity's own half a metre a second would be handed back
    // three quarters of the way, sixty times a second, for ever. A ball that was not
    // is falling, and where it lands it bounces.
    const bool startedOnFloor = ball.position.y <= ball.radius + kTouch;
    if (gtcAudit) std::fprintf(stderr, "B start pos(%+.5f,%+.5f) v(%+.5f,%+.5f) frz%d touch%d\n", ball.position.x, ball.position.y, ball.velocity.x, ball.velocity.y, int(ball.frozen), int(ball.touching));

    // What the ball arrived with is recorded once for the whole tick, before
    // anything in the room moved -- see advance(). A push the dot gives it later
    // in the same tick is not something the ball arrived with.

    // A ball that ended the last tick at rest on something that can hold it is
    // held: the surfaces answer its weight, so there is no force left to give it
    // any speed, and it stays exactly where it is. This is the same rule the dot
    // follows -- a surface that can carry the weight leaves nothing for gravity to
    // do -- and it is what makes "rest" actually rest instead of a floor and a
    // weight trading the same impulse back and forth every tick.
    // A surface that can move does not carry the ball's weight: it is answered with
    // it, and it moves because of it. "Held" is only true of something that is
    // really underneath and really fixed -- the floor, or a shape the room is not
    // stepping -- and a ball at rest on a seesaw was held by it as if the plank were
    // the ground: the weight was taken off the ball and given to nobody, the plank
    // felt nothing, and a balanced see-saw with a ball sitting on one end of it sat
    // there for ever. The same is true of a ball resting on a shape that is itself
    // free to slide, and of one resting on a shape the mouse is carrying.
    const auto supportCanMove = [this]() {
        for (std::size_t i = 0; i < ballTouchCount_; ++i)
            if (ballTouches_[i].body < kBodyCount && !bodies_[ballTouches_[i].body].frozen) return true;
        return false;
    };
    const bool held =
        ball.touching && ball.standing && length(ball.velocity) < kRestSpeed && !supportCanMove();

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
    if (gtcAudit) std::fprintf(stderr, "B afterGravity v(%+.5f,%+.5f) held%d\n", ball.velocity.x, ball.velocity.y, int(held));

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
    // The shapes that have already refused this tick's travel. One refusal each: a
    // second visit is the same answer to the same question, and asking it again is
    // how the walk used to spend the whole tick on one shape and never reach another.
    std::array<bool, kBodyCount> refused{};
    // Set when a shape has refused the travel: what is left of the tick may not then be
    // spent, because the travel is exactly what would put the ball through it.
    bool blocked = false;
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
        // A pinned body has no step to refuse: its step is a turn, and the turn of a
        // plank is answered by the ball's own walk below, which sweeps the face
        // through the tick and carries the ball with it. Carrying the plank back
        // instead is not prevention, because there is nowhere to carry it back to --
        // its place is its axle -- so all it did was erase a turn the plank had taken
        // and leave the spin behind, which is a seesaw that stops and starts.
        if (body.pivoted) continue;
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
        if (startGap <= ball.radius + kTouch) continue;
        const double limit = ball.radius;
        // It may come as near as that and no nearer, and where that is is solved
        // rather than sampled: the speed of the ball and the speed of the turn make
        // no difference to where it lands.
        OutlineHit stop;
        if (!sweptBallAgainstMoving(body.previous.data(), body.position.data(), body.count, ball.position,
                                    ball.velocity, limit, kTick, stop, false))
            continue;
        if (!(stop.along < 1.0)) continue;
        // And the pose it stops in is a pose it may be in: a shape turning as it
        // travels bows its corner through the arc, and the straight line between
        // where it began and where it was going cuts inside that arc. Stopped
        // part-way, it is stopped at the last pose along its own step that is still
        // above the floor. See keepAboveFloor().
        const double along =
            keepAboveFloor(index, body.previousCentre, body.previousAngle, body.centre, body.angle, stop.along);
        absorbRefusedStep(body, index, along);
        body.centre = body.previousCentre + (body.centre - body.previousCentre) * along;
        body.angle = body.previousAngle + (body.angle - body.previousAngle) * along;
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
        // Whether the meeting that wins this iteration was already a contact when
        // the tick began. It travels with the meeting, not with the last body that
        // happened to be looked at.
        bool meetingResting = false;
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
            // The floor is a surface like any other, and it is met at the instant it
            // is met -- not cancelled at the end of the step. It used to be neither:
            // `what` was left at -1 for it, which is the walk's own word for "nothing
            // was met", so the floor's only answer was the velocity taken off at the
            // bottom of the loop. That made the ground perfectly inelastic while the
            // planks and the shapes bounced: a ball dropped 1.48 m came back up 0.00
            // m. The walk now carries the floor's identity with its meeting, and the
            // contact below answers it with the restitution everything else gets.
            what = -2;
        }
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            Body& body = bodies_[index];
            // A body that is already stopped against the ball this tick is not
            // moving any more: its step was spent at the meeting (above). What is
            // left of the tick is swept against where it came to rest.
            // The body's pose at the instant the walk has reached, not the pose the
            // tick began in. A sweep replays the body from `was` to where the tick
            // ends, over the time that is left -- and `previous` is the pose of a
            // *whole* tick ago. Measured from there, a body that has already spent
            // three quarters of its tick looks as if it is moving a quarter as fast,
            // so the face the ball has just been set down on appears a quarter of the
            // way back where it came from. The ball was left four centimetres above a
            // plank it was touching, fell that far, landed, was left four centimetres
            // up again: a bounce with no bounces in it, and the plank loaded on the
            // ticks the ball happened to catch it and coasting the ones between. The
            // interpolation is corner by corner and linear, so the pose at a fraction
            // of the tick is the same interpolation read from there: shifting the
            // start of the sweep is exactly right, and not an approximation of it.
            Vec3 start[kMaxOutlineCorners];
            for (std::size_t i = 0; i < body.count; ++i)
                start[i] = bodyStopped[index] ? body.position[i]
                                              : body.previous[i] + (body.position[i] - body.previous[i]) * (1.0 - remaining);
            const Vec3* was = start;
            // The body is stepped before the ball, and stepping it moves the piece of
            // outline the ball would have met. So the meeting is solved between two
            // *moving* things: the ball's travel and the body's, corner by corner,
            // through the tick. Conservative advancement at a micrometre a pass, with
            // the moment bracketed and closed in on, so neither how fast the ball is
            // going nor how fast the body is turning can hide the arrival between two
            // samples. That is the hitscan: solved outright, not stepped towards.
            OutlineHit hit;
            if (!sweptBallAgainstMoving(was, body.position.data(), body.count, ball.position, ball.velocity,
                                        ball.radius, kTick * remaining, hit)) {
                continue;
            }

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
                ballContact(hit.normal, int(index), held, hit.contact, 0.0, answer, hit.resting, 1.0 - remaining);
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
                // A meeting found at the very first instant is not an arrival: it is the
                // contact the ball already has, and a standing contact is the solver's,
                // which has already answered it. Stopping the walk for it stopped the
                // ball at a shape it was merely lying against and never got as far as the
                // shape it was actually falling onto -- a ball wedged between a cube and
                // a slope was answered for the cube, tick after tick, and kept the whole
                // of its fall, which is the buzz the King saw.
                //
                // What it may not do is *travel into* that shape. So the question is put
                // as the plain one: does what is left of the tick put the ball inside? A
                // ball rolling along a face it is touching ends the tick at one radius
                // from it and is left alone; a ball whose travel would take it in is
                // stopped there, whichever way the surface is going.
                //
                // That second case is why the old test was wrong. It asked which way the
                // two were moving, so a plank turning up out of a ball it was touching
                // was called "not closing" and the ball's travel carried it a centimetre
                // and a half through the face.
                const Vec3 endAt = ball.position + ball.velocity * (kTick * remaining);
                const OutlineGap endOut =
                    ball_shape::outlineGap(endAt, body.position.data(), body.position.data(), body.count, 1.0);
                if (endOut.gap >= ball.radius - playground_tick::kQuantum) continue;
                // It is going in, and the contact it already has is the answer to it:
                // the shape stops what is coming, once, and the ball keeps the rest of
                // its motion. Refused every time round the walk, though, it would stop
                // there for ever and the shapes on the other side of the ball would
                // never be asked -- so one refusal per shape per tick, and the walk goes
                // on to them.
                if (!refused[index]) {
                    refused[index] = true;
                    ball.touching = true;
                    // The face it is going into when the tick ends, not the one it is
                    // lying against now: in a converging corner the two are not the same
                    // direction, and stopping the ball along the wrong one left it
                    // rolling into the corner for ever at a hundredth of a metre a
                    // second -- speed with nowhere to go.
                    ballContact(endOut.normal, int(index), held, endAt - endOut.normal * ball.radius, 0.0,
                                answered[index], hit.resting, 1.0 - remaining);
                    // The answer has changed the ball's speed, so the rest of the tick
                    // is worked out again against it instead of being handed back.
                    //
                    // Handing it back is what this did, and it cost the ball *every
                    // other tick*: a ball lying on a plank and pressing into it had its
                    // whole travel refused, spent the tick where it was, and moved
                    // again on the next one when gravity had turned it sideways along
                    // the face. The ball stopped and went, stopped and went, twenty
                    // times a second, and the plank was loaded on the ticks it went.
                    // The refusal is one contact, not a lost tick: the speed into the
                    // face is gone, and what is left of the travel is the ball's.
                    restart = true;
                    continue;
                }
                blocked = true;
                first = 0.0;
                continue;
            }
            if (hit.along < first) {
                first = hit.along;
                normal = hit.normal;
                depth = hit.depth;
                what = int(index);
                contactPoint = hit.contact;
                meetingResting = hit.resting;
            }
        }
        if (restart) continue;
        if (first > 1.0) {
            if (blocked) break;  // it may not be spent: that travel goes through a face
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
        // Was the ball already against this shape when the tick began? Then this is a
        // contact, not an arrival, and a contact is the patch's to answer. Carrying
        // the shape back here was the plank's whole trouble: a ball rolling along a
        // seesaw spends eight millimetres a tick, so `spent` was always past the
        // room's resolution, the meeting was always found -- at the face the ball was
        // already lying on, a tenth of a picometre deep -- and the plank was put back
        // to an instant early in the tick every single tick. Its turn went, its spin
        // stayed, and it spent the next tick turning from a speed it had not spent.
        // On screen that is a seesaw that stops and starts: three quarters of its
        // turning ticks were losing part of their turn, and twenty of them in a row
        // lost all of it while the turn it was keeping grew to two thirds of a radian
        // a second.
        bool wasAgainst = false;
        if (what >= 0) {
            const Body& met = bodies_[std::size_t(what)];
            const double startGap =
                ball_shape::outlineGap(ball.previous, met.previous.data(), met.previous.data(), met.count, 1.0).gap;
            wasAgainst = startGap <= ball.radius + kTouch;
        }
        if (what >= 0 && !wasAgainst && !bodyStopped[std::size_t(what)] &&
            (depth > 0.0 || spent >= playground_tick::kQuantum)) {
            Body& met = bodies_[std::size_t(what)];
            // A pinned body is not carried back to the instant it was met. It has no
            // place to be carried to -- its place is its axle -- so all that ever
            // came of it was a turn erased and the spin that made it left behind. The
            // ball is not put inside the plank by leaving it alone: the ball's walk is
            // the prevention, and it has already found the meeting and stopped the
            // ball there. Measured over a 200-tick swing with a ball let go on the
            // plank, the ball's deepest incursion into the plank was zero and nothing
            // was ever lifted out of it -- see scratch/seesaw-carry.cpp.
            if (!met.pivoted) {
                const double when =
                    keepAboveFloor(std::size_t(what), met.previousCentre, met.previousAngle, met.centre, met.angle,
                                   (1.0 - remaining) + first * remaining);
                absorbRefusedStep(met, std::size_t(what), when);
                met.centre = met.previousCentre + (met.centre - met.previousCentre) * when;
                met.angle = met.previousAngle + (met.angle - met.previousAngle) * when;
                placeCorners(met);
            }
            bodyStopped[std::size_t(what)] = true;
        }
        // Where in the tick the two met: what the blow is answered to.
        const double reached = (1.0 - remaining) + first * remaining;
        const bool exchange = what < 0 || !answered[std::size_t(what)];
        if (what >= 0 && std::size_t(what) < kBodyCount) answered[std::size_t(what)] = true;
        // "Nothing was met" and "the floor was met" are two different answers, and
        // they used to be the same number. `what` starts at -1 for the first and the
        // floor is -2, so a walk that found no meeting -- which is what a *blocked*
        // walk is, and it is a state this room reaches most ticks a ball is against a
        // moving shape -- arrived here carrying -1 and a normal that had never been
        // written, the default {0, 1, 0}. It was answered as the floor: a bounce off
        // an upward surface that was not there, at three quarters of whatever speed
        // the ball had. The ball was thrown off the plank by nothing at all, every
        // other tick, and the plank was loaded on the ticks it fell back onto and
        // coasted the ones it did not -- the stop-move-stop-move, made of a contact
        // with a surface that does not exist.
        if (what == -1) {
            // Nothing to answer: the travel was refused by a surface that has already
            // said its piece this tick, or there was no travel to refuse.
        } else if (what == -2) {
            floorAnswered = true;
            // The floor is met at the ball's underside, not at its middle: an arrow
            // drawn from the centre of a ball says the ground is pressing the ball
            // through its own middle, and a lever drawn that way turns nothing.
            ballContact(normal, -1, held, ball.position - normal * ball.radius, 0.0, true, startedOnFloor, reached);
            if (bendRecording) { ball.bendOut = ball.velocity; bendRecording = false; }
        } else {
            // One blow per body per tick. A body met again in what is left of the
            // tick has already given its answer, so this time it only stops the
            // ball: it is a resting contact for the rest of the tick, which is what
            // stops a ball sliding into a closing wedge and being left inside it.
            ballContact(normal, what, held, contactPoint, depth, exchange, meetingResting, reached);
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
        if (gtcAudit) std::fprintf(stderr, "B   walk step what %d first %.4f depth %.5f normal(%+.3f,%+.3f) -> v(%+.5f,%+.5f) pos(%+.5f,%+.5f)\n", what, first, depth, normal.x, normal.y, ball.velocity.x, ball.velocity.y, ball.position.x, ball.position.y);
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
    // Every surface the ball is against, asked to hold it out of itself. This is the
    // contact constraint, and it is enforced here and again after the grip: see
    // settleBallSurfaces().
    settleBallSurfaces();
    if (gtcAudit) std::fprintf(stderr, "B afterWalk pos(%+.5f,%+.5f) v(%+.5f,%+.5f)\n", ball.position.x, ball.position.y, ball.velocity.x, ball.velocity.y);
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
        ballContact(Vec3{0, 1, 0}, -1, held, ball.position - Vec3{0, ball.radius, 0.0}, 0.0, true, startedOnFloor,
                    1.0 - remaining);

    // Every face the tick met, solved against the others: what is left of the ball's
    // speed and its turn is what their grips all allow at once.
    if (gtcAudit) {
        std::fprintf(stderr, "B beforeFriction v(%+.5f,%+.5f) w%+.3f touches%zu\n", ball.velocity.x, ball.velocity.y, ball.spin, ballTouchCount_);
        for (std::size_t i = 0; i < ballTouchCount_; ++i)
            std::fprintf(stderr, "     touch %zu body %zu n(%+.3f,%+.3f) sv(%+.4f,%+.4f) pressed %.6f\n", i,
                         ballTouches_[i].body, ballTouches_[i].normal.x, ballTouches_[i].normal.y,
                         ballTouches_[i].surfaceVelocity.x, ballTouches_[i].surfaceVelocity.y, ballTouches_[i].pressed);
    }
    settleBallFriction();
    if (gtcAudit) std::fprintf(stderr, "B afterFriction v(%+.5f,%+.5f) w%+.3f touches%zu\n", ball.velocity.x, ball.velocity.y, ball.spin, ballTouchCount_);
    // And the contact constraint once more, because the grip is the last thing that
    // moved the ball and it can push it into a surface it is lying against -- see
    // settleBallSurfaces(). Same rule, same place in the order: whatever changed the
    // speed last, the surfaces have the last word after it.
    settleBallSurfaces();
    if (gtcAudit) std::fprintf(stderr, "B afterSettle2 pos(%+.5f,%+.5f) v(%+.5f,%+.5f)\n", ball.position.x, ball.position.y, ball.velocity.x, ball.velocity.y);

    // A whole tick of travel below a picometre is not motion a double can carry:
    // that is the floor of what this simulation can represent, not a speed chosen
    // out of the air. Below it the ball is at rest.
    if (gtcAudit) std::fprintf(stderr, "B end pos(%+.5f,%+.5f) v(%+.5f,%+.5f)\n", ball.position.x, ball.position.y, ball.velocity.x, ball.velocity.y);
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
