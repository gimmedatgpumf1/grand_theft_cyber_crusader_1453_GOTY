// Two bodies meeting.
//
// There is no third kind of physics in this file. Everything here already existed:
//
//  - the meeting itself is segmentHitsPolygon, the hitscan the dot's walk uses
//    against a body, solved outright so no speed is too fast to see. A corner is a
//    point, so a corner against an outline is exactly a dot against an outline;
//  - a point left inside a body is put back on its outline by pushOutOfPolygon, the
//    function the dot is put back out by;
//  - being on a body's surface, and which way out of it, is outlineGap, the ball's
//    own test against a body's outline, and where along a face a contact is and
//    what that face is doing there is outlineTouch, the ball's;
//  - the answer is the ball's: giveBodyTurn and bodyShareAt are what the ball's
//    strike on a body already uses, and the ball's contact is solved exactly this
//    way -- the approach speed over the two reciprocal masses where the shapes met,
//    nothing given back, and a grip along the surface limited by mu times what the
//    contact carried.
//
// The two jobs a pair has that no single point does: sweep the step relative to the
// body being met (both bodies move, and only one of them is stepped at a time), and
// take the answer at a contact as two solids rather than as a point and a solid.
// Everything else is called, not rewritten.

#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gtc::native {
using namespace ball_shape;
using namespace playground_detail;
namespace tick = playground_tick;

// A corner lying on a face and moving along it is touching, not arriving:
// answering that as a contact would stop the step at its very first instant, every
// tick, and the body would stutter along the surface instead of sliding down it.
// The dot's own walk is held to exactly this rule.
// A corner that begins the tick on a surface has not arrived at it: it is on it. The
// dot's own walk is held to the same rule, and for the same reason -- answering a
// touch at the very first instant of every step stops the step dead.
//
// What such a corner does instead is go where its speed takes it and be put back out
// if that is inside, which is pushBodyOutOfBody's job and is the position solve's
// answer wherever the surface touched. That matters where the two meet: stopping the
// whole body at the first instant cuts every corner by the same fraction, which is
// the same thing as holding it under its middle -- and a body standing on the point
// of another is not held under its middle, it tips. Only a crossing counts here, so
// nothing is passed through: a corner that starts outside and ends inside has
// crossed the outline, and that crossing is solved for outright.
namespace {
bool arrives(double along) { return along > 1e-9; }
}  // namespace

// How far this body's step can be walked before one of its corners reaches another
// body's outline, and which way that surface faces.
//
// The sweep is the dot's: segmentHitsPolygon against a corner's segment, solved
// outright, so no speed is too coarse for it. What the dot does not need and a body
// does is the other body's travel: two bodies are stepped one after the other, so a
// corner and the face arriving at it are the same meeting seen from either side.
// Both are swept -- this body's corners through the other body's outline, and the
// other body's corners through this one's -- each against the travel between them,
// so a face landing on a corner is caught in the tick it lands and not a tick late.
double PlaygroundRoom::bodiesReach(std::size_t index, const Vec3* target, Vec3* wayOut, std::size_t* metWith) const {
    const Body& body = bodies_[index];
    *metWith = kBodyCount;
    if (body.count == 0) return 1.0;
    Vec3 ownStep{};
    for (std::size_t i = 0; i < body.count; ++i) ownStep = ownStep + (target[i] - body.position[i]);
    ownStep = ownStep * (1.0 / static_cast<double>(body.count));
    double first = 1.0;
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index) continue;
        const Body& solid = bodies_[other];
        if (solid.count == 0) continue;
        // What the other body has already done this tick, read the way the ball's
        // contacts read a body's motion: as its actual step, not as a velocity
        // gravity may not have reached yet. A body that has not been stepped yet is
        // still where it was, and this same sweep catches it when its turn comes --
        // an empty step is the honest answer for it, not last tick's.
        Vec3 solidStep{};
        for (std::size_t i = 0; i < solid.count; ++i) solidStep = solidStep + (solid.position[i] - solid.previous[i]);
        solidStep = solidStep * (1.0 / static_cast<double>(solid.count));
        // This body's corners through the other body's outline.
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 travel = (target[i] - body.position[i]) - solidStep;
            double along = 2.0;
            Vec3 normal{0, 1, 0};
            if (!segmentHitsPolygon(body.position[i], body.position[i] + travel, solid.position.data(), solid.count,
                                    along, normal))
                continue;
            if (!arrives(along) || !(along < first)) continue;
            first = along;
            *wayOut = normal;
            *metWith = other;
        }
        // And the other body's corners through this one's outline, which is the same
        // meeting: a face arriving at a corner is that corner arriving at the face.
        // Only one of the two bodies is stepped at a time, so a face landing on a
        // corner has to be found here as well -- the corner's own sweep is the other
        // body's tick, and by then this one has already moved a whole tick into it.
        for (std::size_t i = 0; i < solid.count; ++i) {
            const Vec3 travel = solidStep - ownStep;
            double along = 2.0;
            Vec3 normal{0, 1, 0};
            if (!segmentHitsPolygon(solid.position[i], solid.position[i] + travel, body.position.data(), body.count,
                                    along, normal))
                continue;
            if (!arrives(along) || !(along < first)) continue;
            first = along;
            *metWith = other;
            // The normal of a corner this body's own outline was met by points out
            // of this body's outline, which is the way the corner is pushed and the
            // opposite of the way this body is: it gives way along it.
            *wayOut = normal * -1.0;
        }
    }
    return first;
}

// No corner of a body may be inside another body, and no outline may be inside a
// corner. Both are the same statement from either side, and both are answered the
// way the ball's own contact is answered.
//
// A corner of this body that is inside another body is put back onto that body's
// outline at the closest point of it -- pushOutOfPolygon, the function the dot is
// put back out by, for the same reason: a point nudged out along an edge normal by a
// hair stays as deep as it was and creeps.
//
// The other way round is a corner of the other body inside this one, and that is a
// face lying on a point -- the cube set down on the point of the triangle. A point
// has no radius, and a point that is inside an outline is a ball of radius zero
// inside it, so the answer is pushBodyOutOfBall's, with the radius taken out: this
// body moves away from the corner along the opposite of the corner's way out, and
// the piece of this body's outline the corner is through is what moves, in
// proportion to how near the corner is to either end of it. That is what lets the
// face tip and slide off a point; lifting every corner of the body by the same
// amount is a body carried on a point without so much as leaning, which is what a
// cube left standing on the triangle's corner looked like. Nothing here is special
// to a corner of a shape: it is the ball's contact with the radius nought.
//
// Each body does this for itself in its own tick, so a pair is separated from both
// sides and neither is ever shoved by the other's business -- and a body that is not
// being stepped leaves the whole correction to the one that is, exactly as the ball
// resolves all of a crossing it finds on its own.
//
// `lift` remembers how far each corner was put back, because a correction is not
// motion: the body's tick takes it back out of that corner's speed, so a body
// pushed up out of another does not fly and a body resting on one does not gain
// speed every tick.
double PlaygroundRoom::pushBodyOutOfBody(std::size_t index, std::array<Vec3, kMaxCorners>* lift) {
    Body& body = bodies_[index];
    if (body.count == 0) return 0.0;
    // A correction is a rigid motion of the whole body at the place it is needed, in
    // the same terms an impulse is given it by -- one mass, one moment, one lever:
    // the piece of the body at the contact moves by the amount it is out by, and the
    // rest of it comes along as the fixed distances require. Pushing corners about
    // one at a time was pushing the ends of a long base two different distances, and
    // a body corrected that way creeps along the surface it is standing on.
    const BodyInertia inertia = bodyInertia(body);
    double deepest = 0.0;
    const auto push = [&](Vec3 at, Vec3 displacement) {
        const double error = length(displacement);
        if (!(error > 0.0)) return;
        const Vec3 normal = displacement * (1.0 / error);
        const Vec3 arm = at - inertia.middle;
        const double lever = arm.x * normal.y - arm.y * normal.x;
        const double share = 1.0 / static_cast<double>(body.count) + lever * lever / inertia.moment;
        if (!(share > 1e-12)) return;
        const double take = error / share;
        const double turn = take * lever / inertia.moment;
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 r = body.position[i] - inertia.middle;
            const Vec3 amount = normal * (take / static_cast<double>(body.count)) + Vec3{-turn * r.y, turn * r.x, 0};
            body.position[i] = body.position[i] + amount;
            if (lift != nullptr) (*lift)[i] = (*lift)[i] + amount;
        }
        deepest = std::max(deepest, error);
    };
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index) continue;
        const Body& solid = bodies_[other];
        if (solid.count == 0) continue;
        // This body's corners, out of the other body's outline, along the closest way
        // out of it -- pushOutOfPolygon, the function the dot is put back out by.
        for (std::size_t i = 0; i < body.count; ++i) {
            Vec3 escaped = body.position[i];
            if (!pushOutOfPolygon(escaped, solid.position.data(), solid.count)) continue;
            push(body.position[i], escaped - body.position[i]);
        }
        // And the other body's corners, out of this one's outline: a corner is a
        // point, so this is the ball's contact with the radius nought, and the piece
        // of this body's outline the corner is through is what the correction is
        // about.
        for (std::size_t i = 0; i < solid.count; ++i) {
            const OutlineGap gap = outlineGap(solid.position[i], body.position.data(), body.position.data(), body.count, 1.0);
            // Outside, or on it: an outline is allowed to be touched, and a corner
            // that is merely near nothing is not a contact.
            if (gap.gap >= 0.0) continue;
            // The way this body has to go: opposite the way the corner is nearest to
            // being out by, which is the ball's own `away`.
            push(solid.position[i], gap.normal * gap.gap);
        }
    }
    return deepest;
}

// What the two bodies are still doing where they meet, once every body has been
// stepped.
//
// The answer is the ball's answer, given from both sides at once. Where the two
// shapes touch is a point, and a solid struck at a point is struck as a solid:
// giveBodyTurn moves every corner of it together and turns it about its middle by
// the impulse's lever, and bodyShareAt is what the impulse is solved against on
// each side. Nothing bounces, exactly as nothing bounces off the plane or off the
// ball, so the contact takes the approach speed and gives none of it back.
//
// The two jobs a pair has that the ball's contact does not: what the surface does
// where it is touched -- the piece of an outline between two corners is moving at
// the lever's interpolation of those two corners, which is what outlineTouch
// returns -- and what each side is free to do along the normal. A body standing on
// the plane cannot be driven down into it, the same rule a dot's blow and a ball's
// strike are both held to; a body held by the mouse cannot be moved at all.
//
// Friction is at the same contact and in the same way: what is left running along
// the surface is answered by an impulse along it, solved against both sides' mass
// at that point and limited by mu times what the contact carried. mu_s holds the
// slide still while it can, which is the plane's own rule in tickBody; past that
// mu_k takes its share off every tick. Without it a body lying on another slides
// along it for ever, a micrometre at a time, and the pair never comes to rest.
//
// The same pass records what is holding what up, in the terms the plane's own test
// is written in: the places along a body's underside that a contact is at, and
// whether what it stands on is itself at rest. A body whose weight does not pass
// between those places is not carried by them and is not allowed to freeze there;
// it turns about the last of them instead, which is what a shape hanging over an
// edge does.
void PlaygroundRoom::bodiesAnswer() {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        bodies_[index].onBody = false;
        bodies_[index].supportLow = 1e30;
        bodies_[index].supportHigh = -1e30;
        bodies_[index].supportFrozen = false;
    }
    // What a body can do about a contact is only the part of its freedom that lies
    // along the normal. A body standing on the plane cannot be driven down into it
    // -- the floor answers that, not the body -- and a body held by the mouse
    // cannot go anywhere at all. The ball's own contact is filtered the same way,
    // with the same words.
    const auto answerFor = [&](std::size_t index, Vec3 push) {
        if (bodyHeld(index)) return Vec3{};
        const Body& body = bodies_[index];
        bool supported = false;
        for (std::size_t i = 0; i < body.count; ++i) supported = supported || body.position[i].y <= tick::kSurfaceY + tick::kContactTolerance;
        return supported && push.y < 0.0 ? Vec3{push.x, 0.0, 0.0} : push;
    };
    // What one pair is doing where they meet: one answer for the pair, not one per
    // corner. Two corners of a face lying on another face are one contact, and
    // answering each of them for the whole approach is answering it twice -- which
    // is what the ball's own walk guards against with its per-body `answered` flag.
    // The contact patch is the average of the places they met, so a face lying flat
    // has its answer under its middle and a body standing on a point has it at the
    // point, which is the difference between a body carried and a body tipped.
    struct PairContact {
        bool any = false;
        Vec3 normal{0, 1, 0};
        Vec3 at{};
        Vec3 relative{};
        double approach = 0.0;  // the most negative closing seen, 0 when they are not closing
        std::size_t contacts = 0;
    };
    for (std::size_t a = 0; a < kBodyCount; ++a) {
        for (std::size_t b = a + 1; b < kBodyCount; ++b) {
            Body& one = bodies_[a];
            Body& two = bodies_[b];
            if (one.count == 0 || two.count == 0) continue;
            const BodyInertia oneInertia = bodyInertia(one);
            const BodyInertia twoInertia = bodyInertia(two);
            // Both ways round: each body's own corners against the other's outline.
            // A corner on the surface and a corner inside it are the same meeting at
            // different depths, and outlineGap is the ball's own test for both --
            // negative is inside, and it says which way out and which way faces
            // what.
            for (int round = 0; round < 2; ++round) {
                Body& body = round == 0 ? one : two;
                const std::size_t self = std::size_t(round == 0 ? a : b);
                Body& solid = round == 0 ? two : one;
                const std::size_t them = std::size_t(round == 0 ? b : a);
                const BodyInertia& bodyFrame = round == 0 ? oneInertia : twoInertia;
                const BodyInertia& solidFrame = round == 0 ? twoInertia : oneInertia;
                PairContact meeting;
                const bool met = body.metBody == them || solid.metBody == self;
                for (std::size_t i = 0; i < body.count; ++i) {
                    const OutlineGap gap =
                        outlineGap(body.position[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
                    // Against it, not merely near it. The sweep stops the step where the
                    // two touch, and the solve that follows -- the fixed distances and
                    // the plane -- is exact only to a tolerance, so a pair that is
                    // genuinely touching ends the tick a hair apart. A hair is not a
                    // separation: the ball's own contacts are taken as touching within
                    // the same skin, and a pair that stops being a contact whenever the
                    // solve rounds is a pair whose closing speed is never answered, which
                    // is a body that hangs in the air gathering speed.
                    // The pair the sweep stopped this tick is a pair in contact,
                    // wherever the solve has since left the two of them: the sweep is
                    // where they met, and a contact measured by the gap alone is a
                    // contact that comes and goes with the solver's rounding.
                    // Against it, not merely near it -- and the same skin the ball's
                    // own contacts are taken within. The sweep stops the step exactly
                    // where the two touch, so a pair that is touching ends the tick a
                    // rounding error apart, and a contact that is only recognised while
                    // the error happens to fall one way is a contact that comes and
                    // goes: the closing speed is then never answered, and the body
                    // hangs where it is, gathering speed it can never spend.
                    (void)met;
                    if (gap.gap > kSkin) continue;
                    // The way out of the other body's outline, which is the normal
                    // of this contact: it points from what was touched towards this
                    // corner.
                    const Vec3 normal = gap.normal;
                    const Vec3 contact = body.position[i];
                    // The piece of the other body that is being touched, and what it
                    // is doing there: an edge's motion is its two corners' motion at
                    // the fraction along it where the contact is.
                    const OutlineTouch touch = outlineTouch(contact, solid.position.data(), solid.count);
                    const Vec3 solidVelocity =
                        solid.velocity[touch.a] * (1.0 - touch.t) + solid.velocity[touch.b] * touch.t;
                    const Vec3 relative = body.velocity[i] - solidVelocity;
                    const double closing = dot(relative, normal);
                    // Something really leaning on this body wakes it: a resting body
                    // is woken by a real blow and by nothing else.
                    if (closing < -kRestSpeed) body.frozen = false;
                    // Standing on the other body: the way out of its surface points
                    // up, so what is under this corner is what has to hold this
                    // body's weight up -- and a way out that points down is the same
                    // meeting with the other body on top, which is the pair seen
                    // from the side that did not move.
                    if (std::abs(normal.y) > 1e-6) {
                        Body& held = normal.y > 0.0 ? body : solid;
                        const Body& under = normal.y > 0.0 ? solid : body;
                        held.onBody = true;
                        held.supportLow = std::min(held.supportLow, contact.x);
                        held.supportHigh = std::max(held.supportHigh, contact.x);
                        held.supportFrozen = under.frozen;
                    }
                    if (!meeting.any) {
                        meeting.any = true;
                        meeting.normal = normal;
                        meeting.relative = relative;
                        meeting.approach = std::min(closing, 0.0);
                    } else if (closing < meeting.approach) {
                        // The hardest meeting is the one the pair is answered on; the
                        // rest of the patch only says where the blow lands.
                        meeting.normal = normal;
                        meeting.relative = relative;
                        meeting.approach = closing;
                    }
                    meeting.at = meeting.at + contact;
                    ++meeting.contacts;
                }
                if (!meeting.any || meeting.contacts == 0) continue;
                meeting.at = meeting.at * (1.0 / static_cast<double>(meeting.contacts));
                const Vec3 normal = meeting.normal;
                // How much of each side's mass is where the two met, and what each of
                // them is free to do there: the impulse is the approach speed over
                // that, exactly as the ball's strike is solved.
                const Vec3 bodyAnswer = answerFor(self, normal);
                const Vec3 solidAnswer = answerFor(them, normal * -1.0);
                // Each side is free in its own direction: this body along the way out
                // of the surface it is against, and the other body the other way. The
                // two directions are opposites, so each is measured against its own.
                const bool bodyMoves = dot(bodyAnswer, normal) > 1e-9;
                const bool solidMoves = dot(solidAnswer, normal * -1.0) > 1e-9;
                const double share = (bodyMoves ? bodyShareAt(body, bodyFrame, meeting.at, bodyAnswer) : 0.0) +
                                     (solidMoves ? bodyShareAt(solid, solidFrame, meeting.at, solidAnswer) : 0.0);
                if (!(share > 1e-12)) continue;
                if (meeting.approach < 0.0) {
                    const double impulse = -meeting.approach / share;
                    // `closing` is measured along the way out of the surface this
                    // corner is against, so the two of them are pushed apart along
                    // it: this body the way it is free to go, and the body it struck
                    // the other way -- which is what `solidAnswer` already is.
                    if (bodyMoves) giveBodyTurn(self, bodyFrame, meeting.at, bodyAnswer * impulse);
                    if (solidMoves) giveBodyTurn(them, solidFrame, meeting.at, solidAnswer * impulse);
                }
                // Friction, at that contact and on what is left running along it: the
                // slide is answered by an impulse along the surface, solved against
                // the same two masses and limited by mu times the impulse this
                // contact carried -- the weight pressing into it when the contact is
                // carrying nothing, which is the floor's own answer to a body lying
                // still on it.
                const Vec3 tangent{normal.y, -normal.x, 0};
                const double slide = dot(meeting.relative, tangent);
                if (!(std::abs(slide) > 0.0)) continue;
                const Vec3 along = slide > 0.0 ? tangent * -1.0 : tangent;
                const double grip = (bodyMoves ? bodyShareAt(body, bodyFrame, meeting.at, along) : 0.0) +
                                    (solidMoves ? bodyShareAt(solid, solidFrame, meeting.at, along) : 0.0);
                if (!(grip > 1e-12)) continue;
                const double needed = std::abs(slide) / grip;
                const double carried = tick::kGravity * tick::kMass * std::abs(normal.y) * tick::kTick;
                const double hold = body.staticFriction * carried;
                const double slip = body.kineticFriction * carried;
                const double rubbed = needed <= hold ? needed : std::min(needed, slip);
                const Vec3 friction = along * rubbed;
                if (bodyMoves) giveBodyTurn(self, bodyFrame, meeting.at, friction);
                if (solidMoves) giveBodyTurn(them, solidFrame, meeting.at, friction * -1.0);
            }
        }
    }
    // A frozen body has nothing holding it up the moment whatever was under it is
    // taken away. The plane never goes anywhere, so only a body that is neither on
    // the plane nor on a resting body can be left in the air.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (!body.frozen) continue;
        bool touchingPlane = false;
        for (std::size_t i = 0; i < body.count; ++i)
            touchingPlane = touchingPlane || body.position[i].y <= tick::kSurfaceY + tick::kContactTolerance;
        if (!body.onBody && !touchingPlane) body.frozen = false;
    }
}

}  // namespace gtc::native
