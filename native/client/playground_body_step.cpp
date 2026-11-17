// How much of a body's step is clear, and where it first is not.
//
// This is the room's one sweep: a body's pose moving to another pose, asked what it
// would pass through on the way. It is here rather than beside the pair work it grew
// out of because it is long and because it can be read on its own -- it moves nothing,
// changes nothing, and answers one number: the fraction of the step that is free.

#include "client/playground_ball_shape.h"
#include "client/playground_pair.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace gtc::native {
using namespace ball_shape;
using namespace playground_detail;


// How much of a body's step is clear.
//
// A step is a pose moving to another pose: the middle travels a straight line and the
// outline turns through an angle, and every corner of the body follows an arc laid
// over that line. Nothing in the room is allowed to be inside anything, and the
// question here is where along that step something first is.
//
// It is answered by conservative advancement, and the word conservative is the whole
// of it: the clearance now is a distance, the fastest any point of this body can be
// closing that distance is known -- the middle's travel plus the swing of the furthest
// corner, added -- so the clearance cannot be gone before that distance divided by
// that speed has passed. The step is advanced to there and looked at again. No speed
// is too fast to see and no turn is too quick, because nothing is sampled: the walk
// never steps *past* the instant a meeting could happen, it steps exactly to the
// soonest it could.
//
// The room's other sweeps do not cover this one, which is why a shape thrown across
// the room could go through the plank: the ball has a sweep of its own, and a pinned
// body's turn has one, but a free body's step was simply taken -- where its speed said
// it went, and answered afterwards. That answer is the recovery, and the recovery is a
// kick. This is the prevention.
//
// A body whose step *begins* against something is not swept at all: it is in contact,
// and contact is the solver's business -- refusing its step would hold it up for ever
// with the speed it arrived with, which is a shape standing on nothing. The same rule
// the ball's prevention uses, for the same reason.
double PlaygroundRoom::bodyStepAllowed(std::size_t index, Vec3 fromCentre, double fromAngle, Vec3 toCentre,
                                       double toAngle, bool mayBeginInContact, Vec3* blockedBy) const {
    if (blockedBy != nullptr) *blockedBy = Vec3{0, 1, 0};
    const std::size_t self = index < kBodyCount ? index : 0;
    const Body& body = bodies_[self];
    if (body.count == 0) return 1.0;
    const Vec3 travel = toCentre - fromCentre;
    const double turn = toAngle - fromAngle;
    double furthest = 0.0;
    for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
    if (!(length(travel) + std::abs(turn) * furthest > playground_tick::kQuantum)) return 1.0;

    std::array<Vec3, kMaxCorners> here{};
    const auto poseAt = [&](double t) {
        const Vec3 middle = fromCentre + travel * t;
        const double angle = fromAngle + turn * t;
        const double c = std::cos(angle), s = std::sin(angle);
        for (std::size_t i = 0; i < body.count; ++i)
            here[i] = Vec3{middle.x + body.local[i].x * c - body.local[i].y * s,
                           middle.y + body.local[i].x * s + body.local[i].y * c, 0.0};
    };

    // One thing standing between the outline and something else: how far there is to
    // go (`gap`, negative when already through), which way out (`normal`), and the two
    // speeds that decide how fast that distance is being eaten.
    struct Approach {
        double gap = 0.0;
        Vec3 normal{0, 1, 0};
        Vec3 closingSpeed{0, 0, 0};
    };
    std::vector<Approach> approaches;
    approaches.reserve(4 * kMaxCorners);
    const auto survey = [&] {
        approaches.clear();
        for (std::size_t i = 0; i < body.count; ++i) {
            Approach row;
            row.gap = here[i].y - playground_tick::kSurfaceY;
            row.normal = Vec3{0, 1, 0};
            row.closingSpeed = pointSpeed(body, here[i]);
            approaches.push_back(row);
        }
        for (std::size_t other = 0; other < kBodyCount; ++other) {
            if (other == self || bodies_[other].count == 0) continue;
            const Body& solid = bodies_[other];
            for (std::size_t i = 0; i < body.count; ++i) {
                const OutlineGap gap =
                    ball_shape::outlineGap(here[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
                Approach row;
                row.gap = gap.gap;
                row.normal = gap.normal;
                row.closingSpeed = pointSpeed(body, here[i]) - pointSpeed(solid, here[i]);
                approaches.push_back(row);
            }
            for (std::size_t i = 0; i < solid.count; ++i) {
                const OutlineGap gap =
                    ball_shape::outlineGap(solid.position[i], here.data(), here.data(), body.count, 1.0);
                Approach row;
                row.gap = gap.gap;
                row.normal = gap.normal;
                row.closingSpeed = pointSpeed(body, solid.position[i]) - pointSpeed(solid, solid.position[i]);
                approaches.push_back(row);
            }
        }
        // The balls, and the one kind of row in this survey whose normal is the wrong
        // way round.
        //
        // Every other row here is a point on *this* body against something else's
        // outline, and outlineGap() answers with the normal pointing out of that
        // outline towards the point -- so the closing speed is taken with a minus and
        // comes out positive when the body is going in. These rows are the other way
        // about: the point is a ball's middle and the outline is *this* body's, so the
        // normal it hands back points out of the body and into the ball, which is the
        // direction the body is travelling when it is going in.
        //
        // Read with the same minus as the rows above, it made every step into a ball
        // close at a *negative* rate: the row was marked as crossing and then skipped
        // for not closing, the sweep returned the whole step, and the shape walked
        // straight through the ball's surface. Measured in the King's corner: the
        // triangle's lower face went from 1.3 um clear to 7.7 mm into the ball in a
        // single tick, every tick, and was shoved back out afterwards by the ball's own
        // sweep -- a whole step refused after it had been taken, which is the walk that
        // never settles.
        //
        // Turned the right way round the row means what it says: the normal is out of
        // the ball and into the body, the same as every other surface's, and the
        // closing speed is the body's at that point less the ball's. One row per ball,
        // because a ball is a ball: the one on the rope is not a different kind of
        // obstacle.
        for (const Ball& ball : balls_) {
            const ball_shape::OutlineGap gap =
                ball_shape::outlineGap(ball.position, here.data(), here.data(), body.count, 1.0);
            Approach row;
            row.gap = gap.gap - ball.radius;
            row.normal = gap.normal * -1.0;
            row.closingSpeed = pointSpeed(body, ball.position) - ball.velocity;
            approaches.push_back(row);
        }
    };

    // Where the step may not take the body: through anything, deeper than a micrometre.
    // A body that begins its step already through something -- a hair through, from the
    // last tick's arithmetic -- may not get any deeper, and that is the same line: the
    // rule is about the body getting in, not about where it happened to start.
    poseAt(0.0);
    survey();
    double startGap = 1e30;
    for (const Approach& row : approaches) startGap = std::min(startGap, row.gap);
    const double limit = (mayBeginInContact && startGap < -playground_tick::kQuantum)
                             ? startGap
                             : -playground_tick::kQuantum;

    // The soonest any part of the body could come within `limit` of anything, as the
    // piece of *this step* at which that happens: how much distance is left, over how
    // fast it is closing, over how long the step lasts.
    //
    // The last division is the one that matters and it used to be missing. What is left
    // of a gap over the speed closing it is a *time* -- metres over metres per second --
    // and a step is one tick long, so that time over the tick is the fraction of the step
    // at which the two meet. Left as a time and then used as a fraction, the meeting was
    // put far too early: a square riding the plank, with more than a whole tick's
    // clearance in hand at its near corner, was told it would arrive in a fiftieth of
    // its step, was stopped there, and had the rest of its speed and turn taken off it
    // by absorbRefusedStep() -- every tick. A shape could not turn with the face it was
    // standing on, so it tipped up onto a corner and slid off the way a box on a plank
    // never does, and a shape walking along a face walked at a fifth of its speed.
    //
    // A part that is not closing at all is not going to close anything, however long
    // the step is. The face it was stopped by is kept for the caller: a step that was
    // refused is a contact, and a contact has a direction.
    // Which parts would actually be past the limit if the whole step were taken. The
    // sweep is about those and only those.
    //
    // A part that is touching a surface and travelling along it is at the limit now and
    // still at the limit at the end of the step: it is not closing on anything, and
    // asking it how long it has left to close gives a fraction of a step that never
    // arrives -- the walk below then creeps by a ten-thousandth of the step per pass and
    // caps the body at a few per cent of its travel, which is a body stopped for the
    // crime of being in contact. That is the same pump running backwards, and it is what
    // a cube riding a turning plank was doing: half its ticks stopped dead, its speed and
    // its turn taken by absorbRefusedStep(), and the face running out from under it.
    poseAt(1.0);
    survey();
    std::vector<char> crossing(approaches.size(), 0);
    bool anyCrossing = false;
    for (std::size_t i = 0; i < approaches.size(); ++i) {
        if (approaches[i].gap - limit < 0.0) {
            crossing[i] = 1;
            anyCrossing = true;
        }
    }
    // Nothing gets past the limit in this step: the step is the body's, whole.
    if (!anyCrossing) return 1.0;

    Vec3 stopping{0, 1, 0};
    const auto soonestMeeting = [&](double t) {
        poseAt(t);
        survey();
        double soonest = 1e30;
        for (std::size_t i = 0; i < approaches.size(); ++i) {
            if (!crossing[i]) continue;
            const Approach& row = approaches[i];
            const double need = row.gap - limit;
            const double closing = -dot(row.closingSpeed, row.normal);
            if (!(need > playground_tick::kQuantum)) {
                // The sweep is within one geometric quantum of contact. Stop at the
                // current conservative fraction instead of taking ever-smaller time
                // advances around two faces that meet at a corner. This gives a
                // scale-aware convergence condition without a fixed pass-count cap.
                if (!(closing > 0.0)) continue;
                stopping = row.normal;
                return 0.0;
            }
            if (!(closing > 0.0)) continue;
            const double answer = need / (closing * playground_tick::kTick);
            if (answer < soonest) {
                soonest = answer;
                stopping = row.normal;
            }
        }
        return soonest;
    };

    double t = 0.0;
    for (;;) {
        const double soonest = soonestMeeting(t);
        if (!(soonest > 0.0)) {
            if (blockedBy != nullptr) *blockedBy = stopping;
            return t;
        }
        const double next = t + soonest;
        if (!(next < 1.0)) return 1.0;
        // The next conservative advance is below the room's geometric resolution:
        // taking another survey cannot move any point by a resolvable amount. Stop
        // at the current safe pose rather than re-surveying the same near-contact
        // configuration through a long sequence of sub-quantum time advances.
        const double pointSpeed = length(body.velocity) + std::abs(body.spin) * furthest;
        if ((next - t) * kTick * pointSpeed <= playground_tick::kQuantum) {
            if (blockedBy != nullptr) *blockedBy = stopping;
            return t;
        }
        // Stop when floating-point precision can no longer advance time. This is
        // convergence at the representable contact instant, not a pass-count cutoff.
        if (!(next > t)) {
            if (blockedBy != nullptr) *blockedBy = stopping;
            return t;
        }
        t = next;
    }
}

// A shape may not be carried through the ball.
//
// The bodies are stepped before the ball, so between the contact solve and the
// ball's own tick there is nobody looking at the two of them. The contact solve
// answers the speed they are closing at along the normal of the contact point the
// tick *began* with -- and a shape that turns swings that point along its own
// outline, so the surface it actually carries into the ball is somewhere else by the
// end of the tick. A triangle rocking at one radian a second put a ball thirty three
// millimetres inside itself that way, and a hard one eighty seven; what the ball then
// did about it was the recovery, and the recovery is a kick.
//
// So the shape's step is stopped where the meeting is. This is the same hitscan the
// ball's own sweep uses, asked the other way round: the ball is standing where it is
// (it has not been stepped yet), the outline travels its tick, and what comes back is
// the fraction of the tick at which the two first touch. The shape is carried that
// far and no further, and its angle with it -- the body is a rigid thing and half of
// its motion is its turn.
//
// Nothing is added to anything by this. The shape is put where it would have been if
// it had stopped when it hit something, which is where a contact would have stopped
// it: no speed is changed, so no energy is made, and the ball's own tick then finds
// it exactly where a contact belongs and answers it as one.
bool PlaygroundRoom::pushBodyOutOfBall(const Ball& ball, std::size_t index, Vec3 normal, double through) {
    if (index >= kBodyCount || !(through > 0.0)) return false;
    // A shape the mouse is carrying gives nothing: the hand is the one thing in the
    // room that is not part of its own physics, and moving a shape against the hand is
    // the room pretending it owns it.
    Body& body = bodies_[index];
    // A body that is asleep is not a thing that can give way to anything, and neither
    // is one in the hand. Both are surfaces for the length of this contact, and a
    // surface that is moved out of the way by what leans on it is not a surface: the
    // ball on the rope, pressed by a slope being held still, moved that slope -- asleep,
    // and moved by a shape that was never asked -- by the whole of the press, only
    // because the ball was in its way and the ball could not be the one to give. A held
    // body gives way to nothing, so what is left is the ball, which is what the caller
    // does when this answers false.
    if (body.count == 0 || bodyHeld(index) || body.frozen) return false;
    if (body.pivoted) {
        // A plank on its axle has nowhere to go, but it has somewhere to *turn*, and
        // turning is how a thing on a pin gives ground: the ball presses on the face,
        // the face comes back, the bearing takes the couple. Refusing it outright is
        // what left the ball with the whole of a seesaw's sweep to answer, and a ball
        // with the floor behind it has nowhere to put it -- so the room slid the ball
        // out from under the plank by up to half a metre in a single tick, which is
        // the ejection that must never happen.
        //
        // The turn it gives back is the turn this tick gave it: no more. What it does
        // not spend, it does not have, and that is written down where the bearing can
        // account for it -- absorbRefusedStep, the same answer a refused step gets.
        const double turned = body.spin * playground_tick::kTick;
        if (std::abs(turned) > 0.0) {
            const double way = turned > 0.0 ? -1.0 : 1.0;  // back along the way it came
            // The closest the body comes to any ball at this angle: it is out of all
            // of them or it is out of none.
            const auto gapAt = [&](double angle) {
                body.angle = angle;
                placeCorners(body);
                double closest = 1e30;
                for (const Ball& ball : balls_) {
                    Vec3 out{}, at{};
                    closest = std::min(closest, bodyBallGap(ball, index, out, at));
                }
                return closest;
            };
            const double here = gapAt(body.angle);
            double best = 0.0, bestGap = here;
            for (int step = 1; step <= 24; ++step) {
                const double reach = std::abs(turned) * double(step) / 24.0;
                const double gap = gapAt(body.angle + way * reach);
                if (!(gap > bestGap)) continue;
                bestGap = gap;
                best = reach;
                if (gap >= playground_tick::kQuantum) break;  // out of it: no further
            }
            if (best > 0.0) {
                const double kept = std::abs(turned) > 0.0 ? std::clamp(1.0 - best / std::abs(turned), 0.0, 1.0) : 0.0;
                body.angle = body.angle + way * best;
                placeCorners(body);
                body.centre = body.pivotPoint;
                // What it did not turn is a step it was refused, and the room's own
                // answer for a refused step is this: the momentum goes, and what took
                // it is written down. For a pinned body it is written at the pin.
                absorbRefusedStep(body, index, kept);
                return true;
            }
            gapAt(body.angle);  // put it back exactly where it was
        }
        return false;
    }
    const Vec3 was = body.centre;
    // How far the shape already reaches below the floor, which is a state it may
    // keep -- it was put there by its own step and the separation above -- but may
    // not make worse. Measured against that, not against zero: a corner a micrometre
    // under the floor is not a reason to refuse, or the shape could never give way
    // about anything again.
    double below = 0.0;
    for (std::size_t i = 0; i < body.count; ++i)
        below = std::max(below, playground_tick::kSurfaceY - body.position[i].y);
    // Out of the ball means along -normal, because the normal runs from the shape to
    // the ball: the shape leaves the way the ball would have.
    body.centre = body.centre - normal * through;
    placeCorners(body);
    // The floor is under everything, so a shape cannot be the one to give way if
    // giving way would take it further through the floor. What is left is the ball
    // moving, which is the honest last resort.
    bool throughTheFloor = false;
    for (std::size_t i = 0; i < body.count; ++i) {
        if (playground_tick::kSurfaceY - body.position[i].y > below + playground_tick::kQuantum) throughTheFloor = true;
    }
    // Along the floor, then, if not up off it.
    //
    // A shape standing on the ground has one way of giving ground and it is the way
    // along the ground: its corner is on the floor, the way out of the ball runs partly
    // downward through the floor, and refusing the whole move leaves a ball pinched
    // between that shape and something that cannot move at all. Which is what was
    // happening: a ball caught between a pinned plank and the bottom corner of a
    // triangle stayed half a millimetre inside the triangle, tick after tick, because
    // the triangle's only way out of it was through the floor.
    //
    // So the same move is made with its part through the floor taken out -- the shape
    // slides, which is what a shape on a floor does when something presses on it -- and
    // only if the ball is genuinely clearer for it. Nothing is lifted and no height is
    // gained, so nothing is being conjured; the speed that made the move necessary was
    // taken off by the contact below, in whichever direction it was taken.
    Vec3 floorWay{};
    if (throughTheFloor) {
        body.centre = was;
        placeCorners(body);
        const double sideways = std::abs(normal.x);
        if (!(sideways > 1e-3)) return false;
        floorWay = Vec3{normal.x > 0.0 ? -1.0 : 1.0, 0.0, 0.0};
        body.centre = body.centre + floorWay * (through / sideways);
        placeCorners(body);
        for (std::size_t i = 0; i < body.count; ++i) {
            if (playground_tick::kSurfaceY - body.position[i].y > below + playground_tick::kQuantum) {
                body.centre = was;
                placeCorners(body);
                return false;
            }
        }
    }
    // And the motion that carried it in.
    //
    // Putting the shape back where it would have stopped is only free if nothing
    // was moving: a body lifted by its place, with the speed that took it there
    // still in it, is a body handed the height and keeping the speed -- work done
    // by moving a point, which is the one thing in this room that must never
    // happen. It is not a small slip either: it is repeated every tick, because a
    // shape resting on the ball arrives at the ball again every tick and is lifted
    // out again, and a cube that sits on a ball's crown with its middle a hand's
    // width off the crown was given its own fall back sixty times a second --
    // spinning faster and faster on the crown of a ball it never moved, with the
    // height it was standing at never changing. The energy it collected was real
    // and came from nowhere, which is the whole of what a conjured force is.
    //
    // So a placement answers the speed that made it necessary, along the way it was
    // made: what was going into the ball stops at the ball. That is a contact doing
    // its job, and it costs nothing -- the shape keeps every part of its motion
    // that the ball did not refuse.
    const Vec3 way = throughTheFloor ? floorWay : normal;
    const double closing = dot(body.velocity, way);
    if (closing > 0.0) {
        body.velocity = body.velocity - way * closing;
        // And it is a contact, so it is written down as one, at the place the ball
        // and the shape meet: what stopped the shape was the ball, and the arrows
        // have to say so or the account does not close. It did not before -- the
        // speed was taken here and nothing was drawn for it, and a shape lifted out
        // of a ball by its place kept the blow that put it there as a momentum the
        // room had quietly lost: measured over the seesaw sweep, one tick's arrows
        // came to 3.97 N.s for a body whose speed had changed by 12.48. A force that
        // is not drawn is a force the room is not keeping.
        //
        // The normal runs from the shape to the ball, so the shape leaves the way the
        // ball would have and the ball answers along the other way: the impulse on
        // the shape is against the closing, and it acts where the two are closest.
        recordImpulse(ball.position - normal * ball.radius, way * (-closing * body.mass), ForceKind::Ball, index);
    }
    ++bodyStepClamped_;
    return true;
}


}  // namespace gtc::native
