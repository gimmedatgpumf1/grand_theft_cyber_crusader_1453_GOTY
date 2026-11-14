// One tick of every solid in the room.
//
// A body is a rigid body: a mass, a moment of inertia, a place, a turn, a speed
// and a spin. Nothing here moves a corner. A force is applied at a place, the
// impulse it delivers over the tick is turned into a change of the body's speed
// and of its turn, and the corners are then read off the shape.
//
// The tick is walked, not sampled. Every instant at which anything would begin to
// touch anything else is solved for -- a corner reaching the floor, a corner
// reaching another body's outline, an outline reaching the ball -- and the bodies
// are carried exactly that far, the contact is answered with an impulse at the
// place it happens, and the walk goes on with what is left of the tick. That is
// what makes a fast body stop at the surface it hits instead of being found
// inside it afterwards, and it is why there is no position correction anywhere in
// this file: nothing ever gets in.

#include "client/playground_ball_shape.h"
#include "client/playground_pair.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gtc::native {
using namespace playground_detail;
using namespace playground_tick;

// Gravity, and nothing else, before the walk: m*g newtons at every corner, which
// is the whole body's weight at its centre of mass and no torque at all. The
// corners are treated exactly as the material points they are -- each one gets the
// impulse its own weight delivers over the tick -- so the body is not told that it
// is heavy, it is pulled at every point and the sum of those pulls is what it
// feels.
void PlaygroundRoom::applyBodyForces() {
    const double share = kMass;  // every corner is a point of the body's mass
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        body.previousCentre = body.centre;
        body.previousAngle = body.angle;
        body.previous = body.position;
        body.touchingPlane = false;
        body.onBody = false;
        // What was against the ball last tick is an arrival this tick only if it was
        // not.
        body.wasOnBall = body.onBall;
        body.onBall = false;
        body.supportLow = 1e30;
        body.supportHigh = -1e30;
        body.supportFrozen = false;
        if (body.count == 0) continue;
        if (body.frozen || bodyHeld(index)) continue;
        // Only a body that is about to be solved for forgets what it was given last
        // tick; a sleeping body keeps it, because that is what it is standing on.
        body.appliedCount = 0;
        applyBodyWeight(index);
    }
}

// One body's own weight, corner by corner.
void PlaygroundRoom::applyBodyWeight(std::size_t index) {
    Body& body = bodies_[index];
    const double share = kMass;  // every corner is a point of the body's mass
    for (std::size_t i = 0; i < body.count; ++i) {
        const Vec3 weight{0.0, -share * kGravity, 0.0};
        addForce(body.position[i], weight, ForceKind::Gravity, nullptr, index);
        // The impulse the weight delivers over one tick, at that very corner.
        // Written out rather than short-circuited to g*dt: what a body feels is
        // a force at a place, and this is that force at that place.
        const Vec3 jog = weight * kTick;
        const Vec3 arm = body.position[i] - body.centre;
        // A pinned body's weight still acts, corner by corner, exactly like this -- but
        // it cannot carry the body anywhere, because the axle will not have it, and the
        // corners' moments about the middle cancel to nothing when the middle is the
        // centre of mass. So the weight of a seesaw does nothing at all, which is the
        // honest answer: a plank balanced on its own middle stays where it is put.
        if (body.pivoted) continue;
        body.velocity = body.velocity + jog * (1.0 / body.mass);
        body.spin += (arm.x * jog.y - arm.y * jog.x) / body.moment;
    }
    // The pin's own bearing, which is the only thing that can ever take a turn out
    // of a body whose centre of mass is its pivot. It is a couple at the axle, against
    // the turn, and it is worth mu * N * r where N is the weight the axle carries --
    // the plank's own, corner by corner, and nothing else, because the pin carries the
    // plank and the floor carries what stands on the plank. It can stop the turn and
    // it can never drive one: the same law as the ball's rolling resistance, and
    // applied the same way, so a plank left spinning comes to rest instead of turning
    // for ever.
    if (body.pivoted && body.spin != 0.0) {
        const double carried = body.mass * kGravity;
        const double couple = kAxleBearingFriction * carried * kAxleRadius;  // N.m
        const double take = couple * kTick / body.moment;                    // rad/s
        const double turn = std::copysign(std::min(std::abs(body.spin), take), body.spin);
        body.spin -= turn;
        if (body.spin == 0.0) body.spin = 0.0;
        addCouple(body.pivotPoint, couple, Vec3{0.0, 0.0, 0.0}, ForceKind::Rolling, index);
    }
}

// The air, on everything in the room that moves through it.
//
// This is a force in its own right and it is applied in a pass of its own, before
// the weight and independently of it -- not as something that happens to a body
// because it is falling. A shape sliding along the floor at speed is slowed by the
// air exactly as a shape dropping through it is, a shape turning is slowed by the
// air it turns through, a dot thrown sideways is slowed the same way, and so is the
// ball. Nothing that moves is exempt, and nothing has to be falling to feel it.
//
// The law is the flat-plate pressure law, which is what drag *is* for a body like
// these: each face of the outline takes the pressure of the air it is pushing into,
// and the pressure is the drag equation with the area that face actually presents --
//
//     F = half * rho * Cd * L * depth * v_n^2,     along the face's outward normal,
//
// applied at the middle of the face, where that pressure acts. v_n is the speed of
// the face through the air *along its own normal*, so a face edge-on to the flow is
// not dragged at all, and only the faces the air is being driven into take anything:
// the lee side of a shape is not pushed, it is a shadow.
//
// Two things follow from doing it this way, and both of them matter. The area is the
// face's own L * depth, so a square presents the one face it is driving into and not
// the sum of every side it happens to have -- asking each corner for half of each of
// its sides asked for four times the shape. And the force acts along the normal of
// the face, at the middle of the face, so a shape moving straight through the air
// takes a force through its middle and does not turn: a body is only turned by the
// air when the air is taking hold of it unevenly, which is a real effect -- a shape
// falling at an angle does turn, and that is why a leaf tumbles -- but a shape with
// no turn of its own must not be given one.
void PlaygroundRoom::applyAirDrag() {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        // A body the mouse is carrying is not being accelerated by anything: its
        // speed is put there by the hand that holds it, and the hand is the force.
        // The weight pass leaves such a body alone for the same reason, and the air
        // does too -- otherwise a shape being carried across the room is drawn with
        // a drag that is nowhere in the motion that actually happened.
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 from = body.position[i];
            const Vec3 to = body.position[(i + 1) % body.count];
            const Vec3 along = to - from;
            const double span = length(along);
            if (!(span > 0.0)) continue;
            // The outline runs one way round, so this is the way out of the shape.
            const Vec3 out{along.y / span, -along.x / span, 0.0};
            const Vec3 at = from + along * 0.5;
            const double vn = dot(pointSpeed(body, at), out);
            // Only a face the air is being driven into is pressed; the one behind is
            // in the shape's shadow and the air there is not pushing it anywhere.
            if (!(vn < 0.0)) continue;
            const double press = 0.5 * ball_shape::kAirDensity * kPlateDragCoefficient * span * kRoomDepth * vn * vn;
            const Vec3 air = out * press;
            addForce(at, air, ForceKind::Drag, nullptr, index);
            const Vec3 puff = air * kTick;
            const Vec3 arm = at - body.centre;
            // A pinned body is carried by its axle and by nothing else: the air is
            // still a force on it, corner by corner, but the part of that force which
            // would take the middle somewhere is the axle's to refuse.
            body.velocity = body.velocity + puff * inverseMass(body);
            body.spin += (arm.x * puff.y - arm.y * puff.x) / body.moment;
        }
    }
}

// Carries every simulated body by `seconds` of its own speed and turn. The only
// place a body's place or turn is ever changed by the simulation.
double PlaygroundRoom::advanceBodies(double seconds) {
    if (!(seconds > 0.0)) return 0.0;
    // The step itself, under the room's resolution: a shape whose centre would
    // move less than a micrometre this tick does not move, and a shape whose turn
    // would carry its furthest corner less than a micrometre does not turn. Below
    // that the numbers are not describing the room any more; they are describing
    // the last few bits of a double, and a body that spends a second of them is a
    // body that is standing still and does not know it.
    const auto move = [&](double time) {
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            Body& body = bodies_[index];
            if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
            double furthest = 0.0;
            for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
            // A blow off the ball landed partway through the tick, so the tick is
            // two moves and not one: the fall up to the meeting, then the rebound
            // for what is left of the tick. One move at the rebound speed for the
            // whole tick is a body lifted off the surface it bounced on by a whole
            // tick of its own rebound, every bounce -- the trampoline.
            //
            // The weight of the tick is already in the speed: it was given to the
            // body at the top of the tick, before any contact, and it is the whole
            // of it. Adding it a second time over the rebound is the same weight
            // twice, and a body's account would be short by exactly that much -- so
            // the move is the two speeds and nothing else.
            if (body.pivoted) {
                // This comes before the bounce split below, and it has to. A body hit
                // by the ball part-way through a tick is stepped as two moves, and that
                // branch carried the turn with a plain multiplication -- sweeping
                // nothing. It is the one road a pinned body's turn could still take
                // without being asked what it turned through, and it was the last way
                // the plank got below the floor: a quarter-turn in one tick, straight
                // through the ground, with the sweep sitting right after it and never
                // reached. A pinned body has no fall to split -- its middle never
                // moves -- so there is nothing in the bounce split for it anyway: its
                // whole tick is its turn.
                //
                // It turns where it is, and there is no place to resolve, because its
                // place never changes: the pin is the one point of it that is not the
                // simulation's to move. There is no place to resolve, because its place
                // never changes: the pin is the one point of it that is not the
                // simulation's to move.
                //
                // The turn is a piece of travel and it is swept as one. A plank's far
                // end covers a long arc for one tick's spin, and the end of the arc is
                // not the arc: a turn that finishes above the floor can have gone
                // straight through it, and this room does not put anything inside
                // anything, not even for the length of one tick. The turn stops at the
                // first instant it meets the floor, a shape or the ball -- see
                // pivotTurnAllowed() -- and what is left of the spin is the contact's
                // to answer, exactly as a shape's speed is when it lands.
                const double turn = std::abs(body.spin) * furthest * time < kQuantum ? 0.0 : body.spin * time;
                double allowed = turn;
                if (turn != 0.0) {
                    const double fraction = pivotTurnAllowed(body, turn);
                    allowed = turn * fraction;
                    if (!(fraction > 0.0)) body.angle = body.angle;
                    // What the sweep refused, the axle takes. A turn is a piece of
                    // travel like any other, and refused travel is absorbed here exactly
                    // as it is for a free body: the spin that is left is the spin the
                    // plank actually spent, and the couple that took the rest is written
                    // down at the pin, where the bearing took it.
                    //
                    // Without this the plank keeps saying one thing with its spin and
                    // doing another with its angle, and the next tick's sweep is asked
                    // about a turn that begins from a speed the plank does not have --
                    // which is exactly a solved turn that disagrees with the truth. The
                    // room's own sweep check counted three such disagreements in 684
                    // turns before this, worst 0.0551 rad; it counts none now.
                    absorbRefusedStep(body, index, fraction);
                }
                body.angle += allowed;
                body.centre = body.pivotPoint;
                placeCorners(body);

                continue;
            }
            // And the step is swept before it is taken. A shape's speed says where it
            // would like to be; whether it may be there is a question about the space
            // between, and it is answered here, by the one function in the room that
            // asks it -- see bodyStepAllowed(). A body that begins against something
            // is not limited by this at all: it is in contact, and the contact solve is
            // what answers a contact.
            // The step, swept. A shape's speed says where it would like to be; whether
            // it may be there is a question about the space between, and it is answered
            // by the one function in the room that asks it -- see bodyStepAllowed().
            //
            // What a refused step must not do is leave the speed behind. It used to:
            // the step was shortened and the velocity was not touched, so the body kept
            // the whole of the momentum it had been refused and gravity went on adding
            // to it every tick. A shape held still by a corner therefore gathered speed
            // it never travelled, and the room drew it as a shape moving -- nineteen
            // newtons of air against a face that had not moved, a triangle with three
            // metres a second in it that had not been anywhere for a second, and then
            // the whole of it spent at once the moment the shape came free. That is the
            // pump, and this is where it was.
            //
            // A step is a tick of travel, and what the sweep refuses is a piece of that
            // travel: the shape did not go there, so it does not have the speed that
            // would have taken it. The refused piece is absorbed by whatever was in the
            // way -- a contact, perfectly inelastic, the same answer every other contact
            // in this room gives -- and it is written down as one, at the body, so the
            // picture accounts for it. Nothing is added to what is left: a shape sliding
            // along a face has nothing refused, so nothing is taken, and it keeps every
            // part of its motion the face did not stand in the way of.
            const auto sweptStep = [&](Vec3 middle, double angle) {
                if (body.count == 0) return 1.0;
                const double along = bodyStepAllowed(index, body.centre, body.angle, middle, angle, true);
                if (!(along < 1.0)) return 1.0;
                ++bodyStepClamped_;
                absorbRefusedStep(body, index, along);
                return along;
            };
            if (body.hitAt < 1.0) {
                const double fall = body.hitAt * time;
                const double rise = time - fall;
                const Vec3 step = body.hitArrival * fall + body.velocity * rise;
                const double turnHere = std::abs(body.spin) * furthest * time < kQuantum ? 0.0 : body.spin * time;
                const Vec3 wanted = body.centre + step;
                const double along = sweptStep(wanted, body.angle + turnHere);
                body.centre = along >= 1.0 ? resolvedMove(body.centre, wanted)
                                           : resolvedMove(body.centre, body.centre + step * along);
                body.angle += turnHere * along;
                placeCorners(body);
                continue;
            }
            {
                const double turnHere = std::abs(body.spin) * furthest * time < kQuantum ? 0.0 : body.spin * time;
                const Vec3 wanted = body.centre + body.velocity * time;
                const double along = sweptStep(wanted, body.angle + turnHere);
                body.centre = along >= 1.0 ? resolvedMove(body.centre, wanted)
                                           : resolvedMove(body.centre, body.centre + (wanted - body.centre) * along);
                body.angle += turnHere * along;
                placeCorners(body);
            }
        }
    };
    std::array<Vec3, kBodyCount> centres{};
    std::array<double, kBodyCount> angles{};
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        centres[index] = bodies_[index].centre;
        angles[index] = bodies_[index].angle;
    }
    move(seconds);
    return seconds;
}

// Every patch at the configuration the walk has stopped at.
//
// A body meets the floor with the face it is standing on, and a convex face on a
// plane touches along a segment: the two lowest corners of the body are that
// segment's ends. They are gathered as one patch, not as separate contacts,
// because the load between them is one distributed load -- and a load that can sit
// anywhere between two ends is the reason a body can stand on the floor at all.
// Both ends are gathered as soon as one of them is down, and each is answered
// against the distance it still has to fall, so a face that is a hair off the floor
// at one end is a face landing on it, not a face held up at one corner.
// A ball touches at one point and nowhere else, so its patch has one end.
void PlaygroundRoom::gatherContacts(ManifoldSet& patches, double band) const {
    patches.clear();
    // The floor, met by the lowest corners of each body.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        Manifold patch;
        patch.body = index;
        patch.other = kBodyCount;
        patch.normal = Vec3{0, 1, 0};
        double lowest[kMaxPatchEnds] = {1e30, 1e30};
        for (std::size_t i = 0; i < body.count; ++i) {
            const double height = body.position[i].y;
            // The corners that are *on* the floor, nearest first. A corner half a
            // metre up is not an end of a contact patch, it is a corner in the air,
            // and taking it as one asks the floor to hold the face by a corner it is
            // not touching -- which it cannot do without pulling, so it lets go.
            //
            // And "within the band" is not the same question as "on the floor". The
            // band is how far anything in the room can travel in a tick, and the far
            // end of a shape can be well inside it while being nowhere near the
            // ground: a seesaw lying on its tip has its other end a hand's width up,
            // and that end is closing at metres a second as the plank comes round, so
            // the band took it. What the floor then did was push a corner that was
            // not touching it -- upward, on the raised side of the pin -- which is a
            // couple nothing gave: the plank never lost the spin, and every tick its
            // turn was stopped by the sweep while its speed said it was still going
            // round. A body holding a speed that carries it nowhere is a body that
            // lies to everything it touches, and the ball beside that tip was told the
            // face was moving away from it and rolled through it -- the clipping.
            //
            // So a corner is an end when it can be at the surface before the tick is
            // over: it is there now, or it is coming down fast enough to arrive. That
            // is the whole rule, and it holds for a face lying flat on the floor as
            // well: its ends are both within the room's own definition of touching --
            // a micrometre -- so both are ends, and the face is held up by both of
            // them, which is what stops it rocking from corner to corner for ever.
            //
            // It used to have an exception: any corner within a millimetre of the
            // floor counted as an end whether or not it was coming down. That is a
            // thousand times the resolution of the room, and it bought the flat face a
            // rest pose a whole *millimetre* off level. Measured: a square that landed
            // on a corner and toppled onto its face came to rest 0.000232 m up at one
            // corner -- 0.02 degrees off level, and perfectly still, held there by
            // 19.59 N of floor reaction at a point 0.23 mm in the air while the other
            // corner carried 19.60 N on the ground. A contact can only push a body
            // away from something it is touching, so that was a force with nothing
            // under it, and the shape it held was never flat: every face it has was a
            // hundredth of a degree off true, which is why the air drag on a shape
            // thrown straight up came back 1e-7 short of the formula and a ball
            // bouncing off its side came back with 0.75 of its bounciness instead of
            // 0.81. Let go of that one exception and gravity does the rest: the raised
            // end falls the last fifth of a millimetre, arrives, and from then on the
            // face is flat because both of its ends are on the floor, not because the
            // floor is answering a corner that is above it.
            if (height > band) continue;
            if (height > playground_tick::kQuantum) {
                const double coming = -dot(pointSpeed(body, body.position[i]), Vec3{0, 1, 0});
                if (height > std::max(0.0, coming) * kTick) continue;
            }
            for (std::size_t k = 0; k < kMaxPatchEnds; ++k) {
                if (height < lowest[k]) {
                    for (std::size_t j = kMaxPatchEnds - 1; j > k; --j) {
                        lowest[j] = lowest[j - 1];
                        patch.corner[j] = patch.corner[j - 1];
                        patch.point[j] = patch.point[j - 1];
                        patch.gap[j] = patch.gap[j - 1];
                    }
                    lowest[k] = height;
                    patch.corner[k] = i;
                    // Where the corner meets the floor, not where it is now.
                    //
                    // The band gathers a contact that has not happened yet -- a corner
                    // a hand's width up is on its way down, and the load it will land
                    // with is answered now. Its lever arm is the line that load will
                    // act along, and that line meets the floor, not the air the corner
                    // is in. Drawn at the corner, the arrow hung in the air above the
                    // ground it was pressing on, and read as a force on nothing.
                    patch.point[k] = Vec3{body.position[i].x, playground_tick::kSurfaceY, 0.0};
                    patch.gap[k] = height;
                    break;
                }
            }
        }
        while (patch.ends < kMaxPatchEnds && lowest[patch.ends] < 1e29) ++patch.ends;
        // A face whose ends are all still in the air is a body falling, and a body
        // falling is the walk's business: it is not answered here, where all it
        // would get is a stop a tick early.
        if (patch.ends > 0 && patch.gap[0] <= band) patches.add(patch);
    }
    // The ball, met at the one place a body's outline comes closest to it.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        Vec3 way{0, 1, 0}, at{};
        const double gap = bodyBallGap(index, way, at);
        // Touching, not "within the reach of a tick".
        //
        // This used to be `gap > band`, and the band is as big as the furthest
        // anything can be carried in a tick -- which for a spinning shape is its
        // *turn*, and a triangle leaning on a ball with a slanted face turns. Measured
        // in the King's corner: the pair was answered as a contact at a gap of 36 um,
        // thirty-six times the room's own definition of touching, because the
        // triangle's turn swept forty microns of its outline in one tick. A shape is
        // not in contact with anything it is not touching, and a perfectly still pair
        // was being handed a contact it did not have: the triangle's spinning surface
        // was read as a surface sweeping into the ball, the ball was answered with that
        // surface speed -- five hundredths of a millimetre per second, every tick -- and
        // the friction solve turned it into a roll the ball could never lose.
        //
        // What is left is an arrival, and an arrival is answered as one: a shape that
        // really will touch the ball before this tick is over is answered here, at the
        // instant it arrives -- see hitAt -- instead of being carried into the surface
        // and having its whole step refused. A shape that is merely *near* the ball is
        // not: it is not touching it, and a contact it does not have is a contact that
        // answers a surface speed it never had.
        if (gap > playground_tick::kQuantum) {
            const double closing = -dot(pointSpeed(body, at) - ballPointSpeed(at), way);
            if (!(closing > 0.0) || !(gap <= closing * kTick)) continue;
        }
        // Does it get there? A contact is a meeting, and a shape that is still on its
        // way to the ball has not met it: it is falling, and falling is the walk's
        // business. The band is the reach of one tick, so a shape inside it may or may
        // not arrive before the tick is over -- and answering it either way is how the
        // bounce got its free height. A cube 90 mm above the ball's crown, closing at
        // 1.46 m/s, covers 73 mm in a tick and arrives at neither: answered, it was
        // turned around in mid-air and placed nine centimetres higher than it ever
        // fell to, which is a bounce off a surface it never touched. Small drops came
        // back at a hundred and fifty per cent of their own height that way.
        //
        // So the reach is the distance the tick can actually cover, and a shape beyond
        // it is not answered at all -- it falls, and next tick it arrives. A shape
        // within it is answered at the instant it arrives, which is what hitAt is for.
        //
        // The distance is the closing speed at the closest point, which is what
        // carries the outline into the ball; a shape sliding past does not close at
        // all and is not an arrival however near it is.
        const double reach = std::max(0.0, -dot(pointSpeed(body, at) - ballPointSpeed(at), way)) * kTick;
        // A shape already resting on the ball is not arriving anywhere, and asking it
        // to close a distance would throw its contact away every tick it stood still:
        // it would be held up for one tick, dropped for the next, and would walk its
        // way along the ball in a tremor. What is arriving is the shape with nothing
        // under it, and that is the only one the reach is for.
        // Only a shape that is *approaching* can fail to arrive. One that is not
        // closing at all is not on its way anywhere: it is resting, or sliding along
        // the ball, and that is a contact whether or not it covers any ground.
        if (reach > 0.0 && gap > reach) continue;
        // Whose contact is it? A meeting belongs to the one that arrived at it, and the
        // room has two solvers for it, which are not interchangeable.
        //
        // The ball's own walk answers *the ball* arriving at a shape: it carries the
        // ball to the instant of the meeting, it knows the ball is round and it bounces
        // it, which is the ball's restitution and its spin. This solver answers *a
        // shape* arriving at the ball: it is a contact against a surface, and it has no
        // restitution in it at all.
        //
        // So a ball flying at a shape has to be the walk's, and it was not: the ball was
        // gathered here, answered as a resting contact, and stopped dead. Measured, a
        // ball fired at 6 m/s at the flat middle of a cube's face came off at 0.00 m/s
        // of separation -- no bounce at all -- and the two of them left the meeting
        // travelling together at 0.57 m/s. The check that fires it every which way read
        // side restitution 0.0000 against the floor's 0.8120. What it was measuring was
        // this: the ball never got to bounce, because the shape's half had already
        // answered the blow as a push.
        //
        // What stays here is the shape's own arrival -- a weight coming down on the
        // crown -- and the resting pair, which is the case this patch was made for: a
        // shape standing on a ball, held up by it. A ball rolling along under a shape
        // is not closing on it either: a surface point of a rolling ball moves *along*
        // the shape it carries, not into it.
        {
            const double bodyClosing = -dot(pointSpeed(body, at), way);  // the shape, into the ball
            const double ballClosing = dot(ballPointSpeed(at), way);     // the ball, into the shape
            // A shape that is not moving towards the ball, and a ball that is, is the
            // ball's meeting: leave it to the walk.
            if (!(bodyClosing > 0.0) && ballClosing > 0.0) continue;
        }
        Manifold patch;
        patch.body = index;
        patch.ball = true;
        patch.ends = 1;
        patch.point[0] = at;
        patch.gap[0] = gap;
        patch.normal = way;  // out of the ball, into the body
        patch.kind = ForceKind::Ball;
        // Was this pair already resting against each other when the tick opened?
        // If so it is not an arrival, and this half of the tick has no blow to answer:
        // the ball's own half solves it, with the floor under the ball in the same
        // system. Measured in the King's corner -- a ball under the triangle's slope --
        // the shapes' half answered the *triangle's own weight* as though it were an
        // arrival at the ball: the ball was handed 0.155 m/s sideways and 3.2 rad/s of
        // spin every tick, the ball's grip took nine tenths of it back, and the tenth
        // that was left is the ball that would not stop creeping.
        {
            const Vec3 arm = at - body.centre;
            const Vec3 arrived{body.arrivalVelocity.x - body.arrivalSpin * arm.y,
                               body.arrivalVelocity.y + body.arrivalSpin * arm.x, 0.0};
            // Both sides as the tick found them: this runs before the solve, so the
            // ball's own speed and turn are still the ones it arrived with.
            if (dot(arrived - ballPointSpeed(at), way) >= 0.0) patch.quiet = true;
        }
        patches.add(patch);
    }
    // Another shape, met over the whole span the two of them share. The separating
    // axis gives the way the load can go and the span of the face lying on the other
    // one gives where it acts; each end of that span carries its own distance, so an
    // end that is still in the air is not answered as though it were down.
    for (std::size_t one = 0; one < kBodyCount; ++one) {
        const Body& body = bodies_[one];
        if (body.count == 0 || body.frozen || bodyHeld(one)) continue;
        for (std::size_t two = 0; two < kBodyCount; ++two) {
            if (two == one || bodies_[two].count == 0) continue;
            // One meeting, one patch. Two shapes touching are one load, not two: if
            // each of them gathered its own patch against the other, the pair would be
            // answered twice, in opposite directions, from two different ends -- and a
            // load that doubles itself every sweep is a shape that rocks for ever on
            // nothing at all. The lower-numbered awake body carries the meeting, and a
            // body asleep can only be the surface in someone else's patch, which is
            // why the ordering is skipped when the other one is not awake.
            if (two < one && !bodies_[two].frozen) continue;
            const Body& solid = bodies_[two];
            pair_shape::PairManifold meeting;
            if (!pair_shape::collide(body.position.data(), body.count, solid.position.data(), solid.count, band,
                                     meeting))
                continue;
            Manifold patch;
            patch.body = one;
            patch.other = two;
            patch.kind = ForceKind::Pair;
            patch.normal = meeting.normal;
            for (std::size_t k = 0; k < meeting.ends && k < kMaxPatchEnds; ++k) {
                patch.corner[k] = kMaxCorners;  // a place on a face, not a corner
                // On the face the load passes through, not on the face that is about
                // to arrive at it. The same rule as the floor above: a meeting within
                // the band has not happened yet, and the line its load acts along is
                // the one where the two outlines touch -- the incident point brought
                // back onto the reference face by its own separation. Left where the
                // incident point is, contact arrows were drawn up to a hundred and
                // fourteen millimetres off the surface they acted on, floating inside
                // the shape: friction pointing at nothing, which is what the King saw.
                patch.point[k] = meeting.at[k] - meeting.normal * (-meeting.depth[k]);
                // Signed, like every other gap: inside is negative, and what is left
                // of it after the tick is the position pass's business.
                patch.gap[k] = -meeting.depth[k];
                patch.normalImpulse[k] = 0.0;
                patch.tangentImpulse[k] = 0.0;
            }
            patch.ends = meeting.ends;
            patches.add(patch);
        }
    }
}

// This is the plainest thing a contact solver can do and the reason to do it is that
// it cannot lie about motion: a body's speed is what moves it, every tick, and there
// is no state in which a shape is held still while its speed keeps growing. That
// state is what a walk that refuses steps produces, and it is a body with a weight
// that never lands and a drag arrow from a speed it does not have.
void PlaygroundRoom::tickBodies() {
    // What every body arrived with, and how many contacts it had already been given.
    //
    // A body that spends a whole tick in free flight has followed a parabola, and a
    // parabola can be drawn exactly. One burst of speed at the top of the tick and a
    // whole tick's travel at that speed does not: it lands the body half a step
    // further along than the path went, every tick, in the direction of the weight --
    // energy out of the arithmetic and out of nothing else. The exact rule is the
    // trapezoid, and it is only a move: half of the speed the forces changed, times
    // the tick. Nothing is added to any speed, so it cannot speed a body up. A body
    // that was given contacts is left out of it, because its travel is the solver's
    // answer and not a parabola's.
    std::array<Vec3, kBodyCount> arrival{};
    // Where every body was when the tick opened, so that the tick's end can tell a body
    // that moved from a body that only said it did. See the resolve below.
    std::array<Vec3, kBodyCount> opened{};
    std::array<double, kBodyCount> openedAngle{};
    std::array<int, kBodyCount> metBefore{};
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        arrival[index] = bodies_[index].velocity;
        opened[index] = bodies_[index].centre;
        openedAngle[index] = bodies_[index].angle;
        bodies_[index].arrivalVelocity = bodies_[index].velocity;
        bodies_[index].arrivalSpin = bodies_[index].spin;
        bodies_[index].hitAt = 1.0;
        bodies_[index].hitArrival = bodies_[index].velocity;
        metBefore[index] = bodies_[index].met;
    }
    wakeUnsupportedBodies();
    // The air first, on the motion the body arrived with -- the speed its own weight
    // has given it this tick is not air it has travelled through, and a shape standing
    // still must feel none of it.
    applyAirDrag();
    applyBodyForces();
    // How far anything can get before this tick is over. Every end within that is a
    // contact *this tick*: it could arrive before the tick ends, so it is answered
    // now, against the distance it has left.
    double reach = kContactBand;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0) continue;
        // A sleeping body is still a surface something can arrive at, so its absence
        // of speed is not what the band is measured by -- but neither does it need to
        // be, because what arrives at it has its own speed and its own band.
        double furthest = 0.0;
        for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
        reach = std::max(reach, (length(body.velocity) + std::abs(body.spin) * furthest) * kTick);
    }
    reach = std::max(reach, length(ball_.velocity) * kTick);
    const double band = reach;
    // A load that arrives before this tick ends is a load arriving, so the wake is
    // asked with the same reach the patches are gathered with.
    wakeLoadedBodies(band);
    // What is holding the ball up, gathered before the tick's solve moves anything:
    // the patches are solved against the ball, and a ball resting on the floor is
    // answered through the floor. See gatherBallSupports().
    gatherBallSupports();
    // The faces of the ball are this tick's, and this is where the tick opens them:
    // before the bodies are solved, so that every shape against the ball is in the
    // list before the ball's own walk adds the ones it meets. Emptied at the start of
    // the tick and not at the end of the ball's half, or the shape being carried would
    // be registered and then erased. See noteBallTouch() and settleBallFriction().
    ballTouchCount_ = 0;
    ManifoldSet& patches = tickPatches_;
    gatherContacts(patches, band);
    // Every patch that is a meeting with the ball is a face the ball is against, and
    // the grip is solved over all of them at once later in the tick. See
    // noteBallTouch(): a shape resting on the ball is answered here and not through
    // ballContact(), and without this the ball gave it no grip whatsoever.
    for (std::size_t i = 0; i < patches.count; ++i) {
        const Manifold& patch = patches.items[i];
        if (!patch.ball) continue;
        noteBallTouch(patch.body, patch.normal * -1.0, patch.point[0]);
    }
    solveContacts(patches);
    advanceBodies(kTick);
    // The parabola, finished, for the bodies that were free to follow one.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index) || body.met != metBefore[index]) continue;
        body.centre = body.centre + (arrival[index] - body.velocity) * (0.5 * kTick);
        placeCorners(body);
    }
    // Whatever a spinning corner carried into a surface within the tick -- the one
    // thing a single straight step cannot see -- is put back out, by position alone.
    // It is the only thing in the room that moves a shape without an impulse, and
    // with the contacts answered against their own distances there is normally
    // nothing left for it to do.
    separateBodies();
    // When a shape is called still is not asked here. The tick still has its ball
    // half to run, and a body declared asleep now would be asleep for the rest of a
    // tick that goes on to solve it: `answerSupportedShapes()` answers the shape's
    // own patches again after the ball has touched them, and it was handing a body
    // that had just been put to sleep the leftovers of a contact it was no longer
    // part of -- impulses of 1e-32 N.s, which are nothing at all, but which woke it
    // and started its second of stillness over, every second, for ever. Measured:
    // a cube whose place and angle did not change in the twelfth decimal for three
    // hundred ticks never once stayed asleep, and the room went on drawing it as a
    // body in motion. Whether a body has been still is a question about the tick
    // that has *finished*, so it is asked at the end of the tick, in advance(), by
    // settleBodies(), after both halves and after separateBall() have had their say.
    // A place and a turn are resolved by `resolvedMove` and by the turn rule above: a
    // step shorter than the room can resolve does not happen, and the body arrives
    // where it started. What that leaves behind is a speed with nowhere to go -- and
    // a speed that carries the body nowhere is not a speed. The room already says so
    // in so many words (playground_room_tick.h, `resolved`), and applies it to the
    // ball's and the dot's own motion; a *body* was the one thing it was never applied
    // to, and the shape of the bug is a body that stands perfectly still, whose place
    // never changes to eleven decimal places, holding a speed of eighteen microns a
    // second for ever. Nothing ever asked it about that speed, because a contact only
    // asks what is actually touching it and this speed never touches anything -- it
    // only feeds the grip of whatever is leaning on it. Measured in the King's corner
    // (a ball wedged under the triangle's apex): the triangle's place and angle were
    // identical to twelve decimals for a thousand ticks while it carried
    // (-5.9e-6, -1.35e-5) m/s and turned at 7.3e-6 rad/s, and that speed is what
    // dragged the ball a hand's width along the floor.
    //
    // So: a body that the step did not move has no speed, and one the turn did not
    // turn has no spin. The two are resolved apart, because a body can creep without
    // turning and turn without creeping, and only the part that went nowhere is
    // nothing. Nothing is added and nothing is taken away from a body that moved: a
    // body travelling more than a micron in the tick keeps every bit of its speed,
    // and a body turning more than a micron at its furthest corner keeps its whole
    // spin. This is the room's resolution and not a brake: it is the same micrometre
    // that decides where a shape may be put and when two surfaces are touching.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        if (body.centre.x == opened[index].x && body.centre.y == opened[index].y &&
            body.centre.z == opened[index].z)
            body.velocity = {};
        if (body.angle == openedAngle[index]) body.spin = 0.0;
    }
    // What this half of the tick applied, turned into arrows. The ball's own
    // contacts are answered after this, and they are flushed by advance() at the end
    // of the tick -- see there. Twice is harmless (the list is emptied each time) and
    // it is what keeps a tick's own blows in its own picture: flushed only here, every
    // blow the ball gave or took was drawn a frame late, so each contact had one end
    // on screen and the other still to come.
    writeImpulses();
}

// The shapes' own supports, answered again after the ball has touched them.
//
// A tick is solved in two halves and that is what makes it affordable, but a blow
// the ball gives a shape lands after the solve that the shape's own supports were
// asked in. The shape is left carrying the speed of that blow -- and its floor, which
// holds it by friction, has not been asked what it thinks of it. The speed is
// cancelled on the next tick, before the shape moves a millimetre, so nothing on
// screen moves and nothing is lost: what is lost is the *answer*. The ball's contacts
// are measured against a face that is sliding away from them at a speed the face
// never really has, and the ball keeps a little speed it can never spend -- a ball
// wedged between a cube's side and a slope sat at a twentieth of a metre a second for
// as long as it was left there, with everything in the room standing perfectly still.
//
// So the shapes are asked once more, over the patches this tick already gathered. A
// warm start is the whole point of doing it this way: the floor's load and the grip
// holding the shape are already in the patches, so the second answer is the same
// contact carried further and not a new one. It is only the shapes that are asked --
// the ball's own faces are settled by the ball, in settleBallSurfaces(), and answering
// them here as well would answer one meeting twice.
void PlaygroundRoom::answerSupportedShapes() {
    ManifoldSet again;
    std::size_t slot[kMaxTickManifolds] = {};
    for (std::size_t i = 0; i < tickPatches_.count; ++i) {
        if (tickPatches_.items[i].ball) continue;
        slot[again.count] = i;
        again.add(tickPatches_.items[i]);
    }
    if (again.count == 0) return;
    // Every impulse this applies is written into the ledger where it acts, by
    // pushBody(), exactly as the first solve's were -- so the tick's arrows include
    // both answers and the tick's account still closes.
    solveContacts(again);
    // And what it applied is remembered by the patch it applied it to.
    //
    // A patch carries the impulses the tick has already given it, and the solve reads
    // those to know how much of this tick's load it has already laid down -- the
    // normal sweep adds the difference, and the grip is taken back before it is laid
    // afresh. Solved on a copy that is then thrown away, the patch never learned any
    // of it, and the next answer was a fresh one from nothing: this function is called
    // twice in a tick, and each call handed every resting shape another whole grip.
    // Measured on a cube struck sideways by the ball: the blow gave it 1.40 m/s and
    // this function took 0.81 m/s of it back in thirds -- exactly three times
    // mu_k * m * g * dt -- leaving the cube at 0.595 and 3.2 newton seconds of the
    // ball's momentum nowhere in the room. Friction is a force held over a tick and
    // bounded by the load it is measured against; it is not a budget a solve may spend
    // again because it was asked a second time.
    for (std::size_t i = 0; i < again.count; ++i)
        for (std::size_t k = 0; k < again.items[i].ends; ++k) {
            tickPatches_.items[slot[i]].normalImpulse[k] = again.items[i].normalImpulse[k];
            tickPatches_.items[slot[i]].tangentImpulse[k] = again.items[i].tangentImpulse[k];
        }
}

// Was the frozen body under `index` ever given this body's load?
//
// The forces a sleeping body replays are the ones the solver last left it with, so if
// it is really carrying something, the thing it carries is in that list: an impulse at
// the place the two meet, of the pair kind. If nothing of the sort is there, the pair
// was never solved with both sides, which means the shape above is leaning on a wall
// that no longer exists as a physical thing -- it is only frozen.
bool PlaygroundRoom::carriedBySolvedSupport(std::size_t index) const {
    const Body& body = bodies_[index];
    if (body.count == 0) return false;
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index) continue;
        const Body& support = bodies_[other];
        if (!support.frozen || support.count == 0) continue;
        pair_shape::PairManifold meeting;
        if (!pair_shape::collide(body.position.data(), body.count, support.position.data(), support.count,
                                 kContactBand, meeting))
            continue;
        for (std::size_t i = 0; i < support.appliedCount; ++i) {
            const Body::Applied& applied = support.applied[i];
            if (applied.kind != ForceKind::Pair) continue;
            for (std::size_t k = 0; k < meeting.ends; ++k)
                if (length(Vec3{applied.at.x - meeting.at[k].x, applied.at.y - meeting.at[k].y, 0}) < 0.05)
                    return true;
        }
    }
    return false;
}

}  // namespace gtc::native
