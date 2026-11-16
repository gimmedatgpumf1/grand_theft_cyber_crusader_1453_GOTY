#include "client/playground_ball_shape.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <cstdlib>

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

void PlaygroundRoom::tickBall(Ball& ball) {
    const std::size_t ballIndex = std::size_t(&ball - balls_.data());
    // Where this ball's own tick starts from, recorded before anything in it can return
    // early: what the room has already made of it this tick is the state a constraint
    // applied after the tick has to read.
    ball.began = ball.velocity;
    // Cleared here, and set only where the ball *arrives* at something below -- see
    // the note on the flag.
    ball.answered = ball.impacted = false;
    // The list of faces is not emptied here. The tick opens it before the bodies are
    // solved -- see tickBodies(), which fills it with every shape that is against the
    // ball -- and this half of the tick only adds the faces the ball's own walk meets.
    // Emptying it here threw away the shape the ball was carrying, which is the one
    // face it most needs a grip on: a cube resting on the crown was registered, then
    // erased, and the ball went on being greased under a weight it was pressing on.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        ball.tickSpeed[index] = bodies_[index].velocity;
        ball.tickSpin[index] = bodies_[index].spin;
    }
    ball.previous = ball.position;
    ball.previousAngle = ball.angle;
    // Where the ball began this tick. A ball whose travel the room resolves away ends
    // the tick exactly where it began it, and the speed that would have carried it is
    // nothing -- see the end of this function, and the same rule for a shape's step in
    // advanceBodies(). It is the room's own micrometre, not a new tolerance.
    const Vec3 beganAt = ball.position;
    // A tick starts straight: whatever the last one bent at is spent.
    ball.bent = false;
    if (ballHeld(ball)) {
        ball.velocity = {};
        ball.alone = ball.velocity;
        ball.answered = true;  // the hand is the answer, and it is not the rope's to change
        ball.spin = 0.0;
        ball.frozen = false;
        // It is being carried, so what it was touching before means nothing.
        ball.touching = false;
        ball.standing = true;
        return;
    }
    if (ball.frozen && !ballNeighboursMoving(ball)) {
        ball.alone = ball.velocity;
        ball.answered = true;  // asleep and still: nothing here for the rope to move
        return;
    }
    ball.frozen = false;
    // Was the ball already lying on the floor when this tick began? A ball that was
    // is in contact, and a contact is not an arrival: it may not bounce off the
    // ground every tick, or gravity's own half a metre a second would be handed back
    // three quarters of the way, sixty times a second, for ever. A ball that was not
    // is falling, and where it lands it bounces.
    const bool startedOnFloor = ball.position.y <= ball.radius + kTouch;

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
    // ...and "nothing it touches can move" is only an answer if it touches something.
    //
    // Asked as it was written -- `!supportCanMove()` -- an empty list of faces said
    // yes: a ball with nothing recorded against it was a ball held up by nothing that
    // could move, and so was held. Measured on a ball fired at a cube: mid-flight, with
    // the room's own contact list empty, it counted as held, its share of the blow it
    // was about to strike came out as 0.0000, and the cube took the whole of the
    // impulse -- 41.8 newton seconds out of a ball carrying 3.7, and a cube that left
    // at 9.65 m/s from a hit at 6. That is the side restitution of 0.0000 the King's
    // check has been failing on, and it was never about restitution at all: the ball
    // was stone for that one tick.
    // And the one surface that can always hold it is the ground. A ball resting on
    // the floor with a shape leaning against its side is held -- by the floor. Asking
    // "can anything it touches move" and answering no only when nothing can is to say
    // that a ball with a triangle at its shoulder is unsupported, and an unsupported
    // ball takes the whole of a tick's weight: 0.49 m/s downwards, twenty times a
    // second, every one of them answered by the floor after a walk of twelve
    // micrometres of sideways travel that no speed of the ball ever accounted for.
    //
    // And the floor is asked where the floor is, not where a contact list says.
    //
    // This used to be read out of ball.touches -- any face whose owner was not a
    // shape -- and that list has not been filled in yet when this line runs: it is
    // emptied at the top of the shapes' half of the tick, filled with the shapes'
    // own patches, and the ground is not one of those. So a ball lying on the ground
    // with a shape leaning on it counted as a ball with nothing under it, took the
    // whole of a tick's weight, and had it taken back by the ground at the end of a
    // walk of twelve micrometres that nothing in the room ever accounted for -- every
    // tick, for as long as a shape was beside it. The ground is under the ball when
    // the ball is on the ground; that is the same test the tick already opens with.
    const bool onTheFloor = startedOnFloor;
    // Held is about the surface, not about how fast the ball happens to be going.
    //
    // It used to be gated on the ball moving slower than the room can resolve, and
    // that gate is a trap the tick closes on itself: the weight is put into the ball
    // because it is not held, the contact leaves a residual of that weight in it, the
    // residual is a hair above the resolution, and the hair is what keeps it from
    // being held on the next tick. Measured in the King's corner: a ball on the floor
    // with the triangle's slope leaning on it settled at 2.3e-5 m/s of travel into the
    // corner and 2.3e-5 m/s for the next four hundred ticks, decaying by 2.6e-8 m/s a
    // tick -- a walk of 5.4 mm across the scene, and it is a fixed point, not a decay,
    // because the number it has to fall below is the number the same loop is holding
    // it at.
    //
    // What makes a surface a support is whether the ball is travelling into it or away
    // from it. A ball that is not leaving the floor has its weight carried by the
    // floor, at every speed; a ball on its way up is not carried by anything, and it
    // falls -- which is what the outgoing half of a bounce is, and it is the one thing
    // this rule has to keep. A ball pressed sideways along the floor is not leaving it.
    const bool leaving = ball.touching && dot(ball.velocity, ball.supportNormal) > 0.0;
    const bool held = ball.touching && ball.standing && !leaving && (onTheFloor || ballRestingOnStillSurface(ball));

    // Forces for this tick: weight, and the air it has to push through. Both are
    // forces in newtons in one accumulator, and the integrator turns the pair of
    // them into speed -- the ball is heavy, so its weight is a large force, and it
    // falls for exactly that reason.
    const Vec3 weight{0.0, -ball.mass * playground_tick::kGravity, 0.0};
    ball.force = {};
    addForce(ball.position, weight, ForceKind::Gravity, &ball.force, ballOwner(ballIndex));
    if (held) {
        // The surface under it carries it, and that force is not written here: it is
        // written at the end of the tick, by the account below, together with
        // everything else the ground had to supply. Writing it here as well was a
        // floor drawn carrying the ball's weight twice -- and, worse, on the tick the
        // ball fell asleep it was written and then the speed the weight had just been
        // given was thrown away, so the account had a weight in it with nothing
        // answering it. The account closes, and it closes where the answer is.
        ball.force = ball.force - weight;
    }
    // The air, on everything that moves through it -- held up or not. See
    // ballAirDrag() for the law and for why it is solved rather than stepped into.
    ball.velocity = ballAirDrag(ball, ball.velocity, kTick);
    // And the room's resolution, as everywhere else: a ball that cannot carry
    // itself a micrometre in a tick is not moving.
    ball.velocity = playground_tick::resolved(playground_tick::accelerated(ball.velocity, ball.force, ball.mass));
    // What the tick's own account comes to: the air and the weight, and the rope's own
    // answer, and nothing else. Everything after this line that changes the ball's speed
    // is something in the room answering it, and the rope has to be able to tell the two
    // apart -- see `alone`.
    ball.alone = ball.velocity;


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
    // What gravity still has to do to the ball in the time that is left of the tick.
    //
    // The tick's weight is put on the ball once, at the top, for a whole tick -- which
    // is right only while the ball is falling freely for that whole tick. The moment
    // a contact answers it, the ball's speed is the speed it has *then*, at the
    // instant of the meeting, and a ball that leaves a surface at four centimetres a
    // second with no weight acting on it for the rest of the tick flies that whole
    // piece of time -- it ends the tick two millimetres above the floor still going
    // up, and the next tick it is an arrival again. Measured: a ball rolled into the
    // foot of the triangle and left alone sat two millimetres off the floor, bouncing
    // twenty times a second, and because it was never *on* anything no friction and
    // no rolling resistance could touch it -- it kept a centimetre a second of drift
    // and crept into the wedge until it was thrown out of it. That micro-bounce is a
    // piece of time with no weight in it, and it is what the King saw shaking.
    //
    // So the weight is put back on for whatever is left of the tick after a contact,
    // which is the same force over the same time it would have had if it had never
    // been interrupted. A ball that leaves a face at a few centimetres a second is
    // pulled back down within the same tick, lands, and is answered as resting --
    // which is what a ball that cannot get off the ground is.
    const auto weightForTheRest = [&ball](double left) {
        ball.velocity.y -= playground_tick::kGravity * kTick * left;
    };
    for (int step = 0; step < kMaxContacts && remaining > 1e-12; ++step) {
        const Vec3 next = ball.position + ball.velocity * (kTick * remaining);
        const Vec3 travel = next - ball.position;
        double first = 2.0;
        double depth = 0.0;
        Vec3 normal{0, 1, 0};
        Vec3 contactPoint = ball.position;
        int what = -1;  // -2 the floor, -3 the other ball, otherwise a body
    // One blow from the bob per tick, like one per shape: a bob met again in what is
    // left of the tick has already given its answer, and this time it only stops the
    // ball.
    std::array<bool, kBallCount> otherAnswered{};
    std::size_t otherIndex = 0;
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
        // The other balls. A ball is a circle and so is this one, so their meeting is
        // solved outright -- see ballFirstTouch. The other ball's travel through this
        // tick is known, so the sweep is between two moving things and neither can be
        // overtaken by the other, at any speed either of them is going.
        for (std::size_t other = 0; other < kBallCount; ++other) {
            if (&balls_[other] == &ball || otherAnswered[other]) continue;
            double alongTheWalk = 2.0;
            Vec3 meetingNormal{}, meetingPoint{};
            bool alreadyAgainst = false;
            if (!ballFirstTouch(ball, balls_[other], ball.position, travel, remaining, alongTheWalk, meetingNormal,
                                meetingPoint, alreadyAgainst))
                continue;
            if (!(alongTheWalk < first)) continue;
            first = alongTheWalk;
            normal = meetingNormal;
            contactPoint = meetingPoint;
            what = -3;
            otherIndex = other;
            meetingResting = alreadyAgainst;
        }
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
                    if (!pushBodyOutOfBall(ball, index, on.normal, through)) {
                        ++ball.foundThrough;
                        ball.position = ball.position + on.normal * through;
                    }
                }
                ball.touching = true;
                const bool answer = !answered[index];
                if (answer) answered[index] = true;
                ballContact(ball, hit.normal, int(index),  hit.contact, 0.0, answer, hit.resting, 1.0 - remaining);
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
                    if (!hit.resting) ball.answered = ball.impacted = true;
                    ballContact(ball, endOut.normal, int(index),  endAt - endOut.normal * ball.radius, 0.0,
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
        // `remaining` was already reduced by this meeting above.
        const double reached = 1.0 - remaining;
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
        } else if (what == -3) {
            // The other ball, answered here and not by the shapes' solver: it is a
            // ball, so the answer is a ball's -- equal and opposite, at the place they
            // met, written on the ledger once for each of them.
            otherAnswered[otherIndex] = true;
            if (!meetingResting) ball.answered = ball.impacted = true;
            ballAgainstBall(ball, otherIndex, normal, meetingResting, reached);
            weightForTheRest(remaining);
            if (bendRecording) { ball.bendOut = ball.velocity; bendRecording = false; }
        } else if (what == -2) {
            floorAnswered = true;
            // The floor is met at the ball's underside, not at its middle: an arrow
            // drawn from the centre of a ball says the ground is pressing the ball
            // through its own middle, and a lever drawn that way turns nothing.
            if (!startedOnFloor) ball.answered = ball.impacted = true;
            ballContact(ball, normal, -1,  ball.position - normal * ball.radius, 0.0, true, startedOnFloor, reached);
            weightForTheRest(remaining);
            if (bendRecording) { ball.bendOut = ball.velocity; bendRecording = false; }
        } else {
            // One blow per body per tick. A body met again in what is left of the
            // tick has already given its answer, so this time it only stops the
            // ball: it is a resting contact for the rest of the tick, which is what
            // stops a ball sliding into a closing wedge and being left inside it.
            if (!meetingResting) ball.answered = ball.impacted = true;
            ballContact(ball, normal, what,  contactPoint, depth, exchange, meetingResting, reached);
            weightForTheRest(remaining);
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

    finishBallTick(ball, walkStart, beganAt, held, floorAnswered, startedOnFloor, remaining);
}

}  // namespace gtc::native
