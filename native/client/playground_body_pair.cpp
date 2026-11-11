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

// How many times a pair's contacts are solved before the tick ends. A contact
// solve is iterative: each answer changes what the others are closing at, so the
// last one to be answered is the one that is wrong. This is a guard against a
// numerical corner case looping for ever, not a tuning -- the solve leaves as soon
// as nothing between the two bodies is closing.
constexpr int kMaxContactPasses = 16;

// How many times a corner left on the far side of a face is put back onto it before
// the tick is accepted. A corner put onto one face can, without ever leaving the
// other body, find itself across a second one; two rounds is all it has ever taken.
constexpr int kCrossingRounds = 4;


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
// Being on the surface is not the same as having arrived at it -- that guard is what
// stops a body stuttering as it slides along another one. But a body that is on the
// surface and moving *into* it has arrived, at this very instant: there is no later
// moment when it is inside that could be called the meeting. The sweep has to say so,
// or the body walks its whole tick into the other one, and being pushed back out
// afterwards is not a hitscan, it is a repair -- a shape that is shoved out at a slant
// every tick is a shape that creeps sideways with nothing pushing it.
//
// A corner clear of the surface has arrived when its own step reaches the surface.
// A corner on it, or inside it, has arrived already if its step is going further in.
bool arrivedAt(double along, double gap, Vec3 travel, Vec3 outward) {
    if (along > 1e-9) return true;
    return gap <= kSkin && dot(travel, outward) < 0.0;
}
}  // namespace

// Where a body's outline stands at one fraction of the tick: every corner carried
// along its own step. A shape that is turning is its corners going round, so this is
// the whole of what turning does to where its sides are -- there is no separate
// turning case to get wrong, and no side that moves without its ends moving.
void PlaygroundRoom::outlineAt(const Body& body, const Vec3* target, double fraction, std::array<Vec3, kMaxCorners>& out) {
    for (std::size_t i = 0; i < body.count; ++i) out[i] = body.position[i] + (target[i] - body.position[i]) * fraction;
}

// The first instant a corner's step carries it through one of another body's sides,
// and which way that side faces.
//
// Both of them are moving: the corner along its own step, and the side carried
// between the two places its ends are at. How far the corner is from the *line* of
// that side is, at time t, one cross product of two things that are each linear in t,
// so it is a quadratic and its roots are solved outright. Nothing is sampled, so no
// speed is too fast to see, and a side that swings onto a corner is caught in the
// tick it swings -- which is what a sweep that only carried the shapes' middles
// could not do, and what let a turned body walk a corner through a face.
//
// Only a crossing *into* the other body counts. A corner resting on a side and
// sliding along it crosses the line of that side twice in a tick, and answering that
// is a body that stutters instead of sliding; a corner that begins the tick already
// touching is a contact, and the tick's own contact check says so at time nought.
bool PlaygroundRoom::cornerCrossesSide(Vec3 from, Vec3 to, const Body& solid, const std::array<Vec3, kMaxCorners>& solidEnd,
                       Vec3 inside, double* along, Vec3* normal, Vec3* contact) {
    double best = 2.0;
    Vec3 bestNormal{0, 1, 0}, bestContact{};
    const Vec3 move = to - from;
    for (std::size_t k = 0; k < solid.count; ++k) {
        const Vec3 a0 = solid.position[k], b0 = solid.position[(k + 1) % solid.count];
        const Vec3 a1 = solidEnd[k], b1 = solidEnd[(k + 1) % solid.count];
        const Vec3 s0 = b0 - a0;
        const Vec3 z = (b1 - b0) - (a1 - a0);
        const Vec3 r0 = from - a0;
        const Vec3 w = move - (a1 - a0);
        const double length0 = std::sqrt(s0.x * s0.x + s0.y * s0.y);
        if (!(length0 > 1e-12)) continue;
        // Which side of the line the corner starts on, and which side the other body
        // is on: a corner that starts on the body's side of it is already through the
        // face and is not arriving at it.
        const double start = s0.x * r0.y - s0.y * r0.x;
        const double inward = s0.x * (inside.y - a0.y) - s0.y * (inside.x - a0.x);
        if (!(start * inward < 0.0)) continue;
        if (!(std::abs(start) > 1e-9 * length0)) continue;
        const double quadratic = z.x * w.y - z.y * w.x;
        const double linear = (s0.x * w.y - s0.y * w.x) + (z.x * r0.y - z.y * r0.x);
        const double constant = start;
        double found = 2.0;
        if (std::abs(quadratic) <= 1e-18) {
            if (std::abs(linear) > 1e-18) {
                const double one = -constant / linear;
                if (one > 0.0 && one <= 1.0) found = one;
            }
        } else {
            const double disc = linear * linear - 4.0 * quadratic * constant;
            // A root it only touches the line at is not a crossing: the corner goes up
            // to the face and comes back without ever being through it.
            if (disc > 0.0) {
                const double root = std::sqrt(disc);
                for (const double each : {(-linear - root) / (2.0 * quadratic), (-linear + root) / (2.0 * quadratic)})
                    if (each > 0.0 && each <= 1.0 && each < found) found = each;
            }
        }
        if (!(found <= 1.0) || !(found < best)) continue;
        // Where along the side it went through. A corner that crosses the line of a
        // side it is beside, rather than the side itself, has not met that side.
        const Vec3 side = s0 + z * found;
        const Vec3 reach = r0 + w * found;
        const double sideSquared = dot(side, side);
        if (!(sideSquared > 1e-24)) continue;
        const double place = dot(reach, side) / sideSquared;
        if (!(place >= -1e-9 && place <= 1.0 + 1e-9)) continue;
        const Vec3 endA = a0 + (a1 - a0) * found;
        const Vec3 endB = b0 + (b1 - b0) * found;
        Vec3 face{side.y, -side.x, 0};
        const double faceLength = std::sqrt(dot(face, face));
        if (!(faceLength > 1e-12)) continue;
        face = face * (1.0 / faceLength);
        if (dot(face, (endA + endB) * 0.5 - inside) < 0.0) face = face * -1.0;
        best = found;
        bestNormal = face;
        bestContact = from + move * found;
    }
    if (!(best <= 1.0)) return false;
    *along = best;
    *normal = bestNormal;
    *contact = bestContact;
    return true;
}

// How far this body's step can be walked before one of its corners crosses another
// body's outline, and which way that surface faces.
//
// Both bodies are swept over the same window of the tick: this one along the step it
// is about to take, the other along the step its own speeds would give it. The two
// are stepped one after the other, so a sweep that only counted what the other had
// already done would answer a different question depending on which of them came
// first -- the body stepped second would walk into a face that was about to come to
// it, and the face would then have to be put back out of it. Swept over the same
// window, what comes back is a time in the tick that both of them agree on.
//
// A corner against a side is the whole of it: two convex outlines can only be through
// each other if one of them has a corner on the far side of one of the other's
// sides, so sweeping every corner against every side, from both sides, misses
// nothing -- an edge through an edge has both of its crossings at corners.
double PlaygroundRoom::bodiesReach(std::size_t index, const Vec3* target, double fraction, Vec3* wayOut,
                                  std::size_t* metWith, Vec3* metAt) const {
    const Body& body = bodies_[index];
    *metWith = kBodyCount;
    if (body.count == 0) return 1.0;
    double first = 1.0;
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index) continue;
        const Body& solid = bodies_[other];
        if (solid.count == 0) continue;
        // Where the other body's corners will be at the end of this window of the
        // tick, from its own speeds: the same window this body's step is the rest of.
        std::array<Vec3, kMaxCorners> solidTarget{};
        for (std::size_t i = 0; i < solid.count; ++i)
            solidTarget[i] = solid.position[i] + solid.velocity[i] * (kTick * fraction);
        std::array<Vec3, kMaxCorners> here{};
        outlineAt(body, target, 1.0, here);
        const Vec3 insideBody = centroid(body.position.data(), body.count);
        const Vec3 insideSolid = centroid(solid.position.data(), solid.count);
        // A corner already against the surface and driving into it has arrived
        // already, at this very instant: there is no later moment when it is inside
        // that could be called the meeting. The same guard the dot's walk uses.
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 travel = target[i] - body.position[i];
            const OutlineGap gap =
                outlineGap(body.position[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
            if (!(gap.gap <= kSkin)) continue;
            if (!(dot(travel, gap.normal) < 0.0)) continue;
            if (!(0.0 < first)) continue;
            first = 0.0;
            *wayOut = gap.normal;
            *metWith = other;
            *metAt = body.position[i];
        }
        // The same from the other side: a corner of the other body already against
        // this one and driving into it is a contact now, and this body gives way.
        for (std::size_t i = 0; i < solid.count; ++i) {
            const Vec3 travel = solidTarget[i] - solid.position[i];
            const OutlineGap gap =
                outlineGap(solid.position[i], body.position.data(), body.position.data(), body.count, 1.0);
            if (!(gap.gap <= kSkin)) continue;
            if (!(dot(travel, gap.normal) < 0.0)) continue;
            if (!(0.0 < first)) continue;
            first = 0.0;
            *wayOut = gap.normal * -1.0;
            *metWith = other;
            *metAt = solid.position[i];
        }
        // This body's corners through the other body's sides, and the other body's
        // corners through this one's: one meeting, seen from either side.
        double along = 2.0;
        Vec3 normal{0, 1, 0}, contact{};
        for (std::size_t i = 0; i < body.count; ++i) {
            if (!cornerCrossesSide(body.position[i], target[i], solid, solidTarget, insideSolid, &along, &normal,
                                   &contact))
                continue;
            if (!(along < first)) continue;
            first = along;
            *wayOut = normal;
            *metWith = other;
            *metAt = contact;
        }
        for (std::size_t i = 0; i < solid.count; ++i) {
            if (!cornerCrossesSide(solid.position[i], solidTarget[i], body, here, insideBody, &along, &normal,
                                   &contact))
                continue;
            if (!(along < first)) continue;
            first = along;
            // The side met here is this body's, so the way this body gives is the
            // opposite of the way that side faces.
            *wayOut = normal * -1.0;
            *metWith = other;
            *metAt = contact;
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
// A correction is a rigid motion of the whole body at the place it is needed, in the
// same terms an impulse is given it by -- one mass, one moment, one lever: the piece
// of the body at the contact moves by the amount it is out by, and the rest of it
// comes along as the fixed distances require. The floor, the ball and another body
// all correct a body this way; correcting it any other way -- pinning the one point
// that is out and letting the distances sort out the rest -- is what let a cube come
// down at seventeen degrees and land flat, its far corners thrown sideways.
void PlaygroundRoom::pushBodyAtPoint(std::size_t index, Vec3 at, Vec3 displacement,
                                    std::array<Vec3, kMaxCorners>* lift) {
    Body& body = bodies_[index];
    if (body.count == 0) return;
    const double error = length(displacement);
    if (!(error > 0.0)) return;
    const BodyInertia inertia = bodyInertia(body);
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
}

double PlaygroundRoom::pushBodyOutOfBody(std::size_t index, std::array<Vec3, kMaxCorners>* lift) {
    Body& body = bodies_[index];
    if (body.count == 0) return 0.0;
    // A correction is a rigid motion of the whole body at the place it is needed, in
    // the same terms an impulse is given it by -- one mass, one moment, one lever:
    // the piece of the body at the contact moves by the amount it is out by, and the
    // rest of it comes along as the fixed distances require. Pushing corners about
    // one at a time was pushing the ends of a long base two different distances, and
    // a body corrected that way creeps along the surface it is standing on.
    double deepest = 0.0;
    const auto push = [&](Vec3 at, Vec3 displacement) {
        if (!(length(displacement) > 0.0)) return;
        pushBodyAtPoint(index, at, displacement, lift);
        deepest = std::max(deepest, length(displacement));
    };
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index) continue;
        const Body& solid = bodies_[other];
        if (solid.count == 0) continue;
        // This body's corners, out of the other body's outline, along the closest way
        // out of it -- pushOutOfPolygon, the function the dot is put back out by.
        //
        // Only the body whose own corner is through the other one is moved here, and
        // only by its share of the overlap, because the other body is doing exactly
        // the same thing from its own side in its own tick. Correcting the overlap
        // from both sides at once -- this body out of the other's outline and, in the
        // same breath, this body away from the other's corner that is through it --
        // moves the same overlap twice and in opposite directions: a triangle with a
        // corner resting a hair inside a cube pushed the cube down out of its way
        // every tick, the floor pushed the cube straight back up, and the body was
        // left with a velocity that was nothing but the two corrections cancelling.
        // That is a body creeping along another with nothing pushing it. Half each,
        // which is what the fixed distances already do between two equal masses; the
        // whole of it when what is under it cannot move at all.
        const double share = (solid.frozen || bodyHeld(other)) ? 1.0 : 0.5;
        for (std::size_t i = 0; i < body.count; ++i) {
            Vec3 escaped = body.position[i];
            if (!pushOutOfPolygon(escaped, solid.position.data(), solid.count)) continue;
            push(body.position[i], (escaped - body.position[i]) * share);
        }
    }
    return deepest;
}

// A corner that was carried across a line is put back on it.
//
// The sweep is what keeps a corner on its own side of a face: no step of this body is
// ever taken without the segment it takes being tested. What the sweep cannot speak
// for is the *correction* -- the fixed distances pushing a body back into shape about
// a corner that was stopped, which moves corners that were never swept, and by an
// amount that is not a speed of anything. A corner can be carried across a face that
// way, and a corner on the far side of a face is a corner inside another body.
//
// So the same hitscan is asked once more, about the segment each corner actually
// travelled this tick: from where it was when the tick began to where the solve has
// left it. Where that segment crosses a face, the corner is put at the crossing --
// on the line, not past it -- and the whole body is moved with it, because a body is
// one object and a corner put back on its own would bend it. The distances are solved
// again afterwards, so the body is still the shape it was.
//
// This is not a push-out: nothing is ever put anywhere it has not been. The corner is
// stopped where it crossed, on the first instant it was ever across, and the body is
// walked back to that instant with it.
bool PlaygroundRoom::undoCrossings(std::size_t index) {
    Body& body = bodies_[index];
    bool moved = false;
    // A corner that is on the far side of a face is put back on the face, and the body
    // goes with it.
    //
    // The sweep above is what keeps a corner on its own side: no step of a body is
    // taken without the segment it takes being tested, against the other body's faces
    // and along every side of both outlines. What it cannot speak for is a corner that
    // moves without being stepped -- the fixed distances re-arranging the shape about a
    // corner that was stopped, and the other body simply being elsewhere than where
    // this one's sweep expected it, because the two are stepped one after the other.
    // A corner left on the far side of a face is a corner inside another body, and it
    // is put back onto the line at the first instant it is ever across it: onto the
    // outline itself, not to some pushed-out distance from it, and the whole body is
    // moved with it so the shape is not bent.
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index) continue;
        const Body& solid = bodies_[other];
        if (solid.count == 0) continue;
        for (std::size_t i = 0; i < body.count; ++i) {
            const OutlineGap gap =
                outlineGap(body.position[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
            if (!(gap.gap < -kSkin)) continue;
            // The way out is the outline's own normal; the distance to the line is the
            // gap. Putting the corner on the line is moving it by the gap and no more.
            const Vec3 onto = gap.normal * gap.gap;
            for (std::size_t k = 0; k < body.count; ++k) body.position[k] = body.position[k] + onto;
            moved = true;
        }
    }
    // And the ball, whose outline is a circle: a corner inside it is put on it.
    for (std::size_t i = 0; i < body.count; ++i) {
        const Vec3 arm = body.position[i] - ball_.position;
        const double distance = length(arm);
        if (!(distance < ball_.radius - kSkin)) continue;
        const Vec3 onto = distance > 1e-12 ? arm * ((ball_.radius - distance) / distance) : Vec3{0, ball_.radius, 0};
        for (std::size_t k = 0; k < body.count; ++k) body.position[k] = body.position[k] + onto;
        moved = true;
    }
    return moved;
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
// How many times one contact's press and its grip are answered against each
// other. Two answers are enough to be right to a thousandth of a millimetre a
// second on a contact of two points; the rest is what a corner between two faces
// needs, which is the one place the two keep spoiling each other's answer.
constexpr int kContactSettle = 8;

void PlaygroundRoom::bodiesAnswer() {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        bodies_[index].touched = false;
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
        // Which corner of this body met the other one hardest: it is the point the
        // contact is answered at, because a contact is between points.
        std::size_t corner = 0;
    };
    //
    // Every answer a contact gives is one point's speed, and one point's speed is
    // not a body moving until the fixed distances have carried it to the rest of
    // it. So the two are solved together, the way the shape and the plane are: a
    // sweep answers every contact of every pair, the distances of every body that
    // was answered are solved, and that repeats until nothing is closing any more.
    // Answering all the contacts first and only then making the bodies rigid is
    // answering the second contact against a body the first one never touched --
    // and an uneven pair is a body tipped by the order its own corners happen to
    // be in. The bound is a guard against a numerical corner case looping for
    // ever, not a tuning: the loop leaves as soon as nothing is closing.
    const auto solveContacts = [&]() {
    double closingTotal = 0.0;
    for (std::size_t a = 0; a < kBodyCount; ++a) {
        for (std::size_t b = a + 1; b < kBodyCount; ++b) {
            Body& one = bodies_[a];
            Body& two = bodies_[b];
            if (one.count == 0 || two.count == 0) continue;
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
                // Where the other body's travel was taking it this tick, before the
                // sweep cut it short -- the same target the plane's hitscan is given,
                // and not where the body ended up. A body held still by the solve has
                // an achieved travel of nothing, and reading that as "it is not moving"
                // is what let a pair of bodies that are pressed together never be
                // answered at all.
                Vec3 solidStep{};
                for (std::size_t i = 0; i < solid.count; ++i) solidStep = solidStep + solid.velocity[i];
                solidStep = solidStep * (tick::kTick / static_cast<double>(solid.count));
                for (std::size_t i = 0; i < body.count; ++i) {
                    // The contact's normal comes from the same hitscan the sweep uses,
                    // asked the same question about the same travel: is this corner on
                    // the other body's outline now, or does this tick's own step take it
                    // there? A surface and a step, in one rule.
                    //
                    // Asking instead where the corner will *end up* -- "is it inside
                    // after its step" -- is a contact that comes and goes. A body turning
                    // about the corner it is standing on lifts that corner a few
                    // millimetres off the face as it turns; the contact is lost, the body
                    // falls until it is inside, the contact comes back as a blow and spins
                    // it further. That cycle is a body creeping sideways along a surface
                    // with nothing pushing it, and it is what the triangle balanced on one
                    // corner on the cube did.
                    const Vec3 travel = body.velocity[i] * tick::kTick - solidStep;
                    const OutlineGap gap =
                        outlineGap(body.position[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
                    double along = 2.0;
                    Vec3 hitNormal{0, 1, 0};
                    segmentHitsPolygon(body.position[i], body.position[i] + travel, solid.position.data(),
                                       solid.count, along, hitNormal);
                    // A meeting is a contact -- and what a contact is, is a corner that
                    // this tick's own step has taken into the surface, or one already on
                    // it. The two are one test, not two: the gap the corner has left is
                    // compared with how far its own step closes it.
                    //
                    // Testing for a crossing with the hitscan alone misses every corner
                    // that is already against the surface and resting on it, because a
                    // resting corner's step is along the surface and crosses nothing --
                    // and a skin of a millionth of a metre is not a test either, it is a
                    // coin toss: a corner resting a hair above the surface loses its
                    // contact, falls for a tick, and comes back as a blow, so nothing
                    // that is standing on anything ever has a contact it can grip with.
                    // That is a triangle balanced on a corner on the cube turning about
                    // it and walking out from under itself with nothing pushing it.
                    //
                    // What is not a contact is a corner clear of the surface and leaving
                    // it: its step closes nothing.
                    const double closes = -dot(travel, gap.normal);
                    const bool meeting = gap.gap <= kSkin + std::max(0.0, closes);
                    if (!meeting) continue;
                    // The way out of the surface is the same whether the corner is on it
                    // or a step short of it: outlineGap's own normal, from the surface to
                    // the point. A corner in the air is answered along it exactly as a
                    // corner resting on the surface is, which is what makes the last
                    // fraction of a step before a landing the same contact as the landing.
                    const Vec3 normal = gap.normal;
                    // Standing on the other body: the way out of its surface points
                    // up, so what is under this corner is what has to hold this body's
                    // weight up -- and a way out that points down is the same meeting
                    // with the other body on top, which is the pair seen from the side
                    // that did not move.
                    if (std::abs(normal.y) > 1e-6) {
                        Body& held = normal.y > 0.0 ? body : solid;
                        const Body& under = normal.y > 0.0 ? solid : body;
                        held.onBody = true;
                        held.supportLow = std::min(held.supportLow, body.position[i].x);
                        held.supportHigh = std::max(held.supportHigh, body.position[i].x);
                        held.supportFrozen = under.frozen;
                    }
                    // The contact is a place on two solids, and a solid struck at a
                    // place is struck as a solid. So the answer is the ball's answer, to
                    // the letter: what a side gives at the contact is one over its mass
                    // plus the lever over its moment (bodyShareAt), and what it is
                    // handed is a rigid turn about its own middle (giveBodyTurn).
                    //
                    // Giving the blow to the one corner instead -- "two corners, equal
                    // mass, half each" -- and letting the fixed distances carry it is
                    // not the same thing at all. The distances then turn one point's kick
                    // into a shove of the whole body, so a corner that friction has just
                    // pinned still walks: the triangle balanced on one corner on the cube
                    // crept half a metre sideways with nothing whatever pushing it.
                    // A corner is a point with no radius: where it meets a surface is
                    // the corner itself. The hitscan's fraction along the step says a
                    // meeting happens this tick, not where the corner is standing -- and
                    // taking that fraction as the place of contact carries the contact
                    // point forward along every step, which is a lever that grows with
                    // every tick. The corner is the place.
                    const Vec3 at = body.position[i];
                    const BodyInertia mine = bodyInertia(body);
                    const BodyInertia theirs = bodyInertia(solid);
                    // A side that cannot move gives nothing, and one that can move
                    // only along part of the normal gives that part. A body standing on
                    // the plane cannot be driven down into it, a body the mouse holds
                    // cannot go anywhere, and a sleeping body is a wall until something
                    // wakes it: all three are the dot's own rule for a blow. answerFor
                    // answers with the way the side is free to go, so the freedom is the
                    // part of that way that lies along the normal.
                    //
                    // Asking instead whether that answer is merely not zero is the
                    // difference between a cube the floor is holding and a cube that
                    // flies. The blocked answer comes back as the normal's own x-lever
                    // -- three times ten to the minus sixteen -- and counting that as
                    // "this side gives" hands a share of every blow to a body that will
                    // not move: the momentum is delivered, the floor deletes it with its
                    // own correction, and the other side keeps its half of a blow that
                    // was never paid for. That is a body pushed along a surface with
                    // nothing pushing it, which is exactly what the triangle balanced on
                    // one corner on the cube did.
                    const Vec3 mineWay = answerFor(self, normal);
                    const Vec3 theirsWay = answerFor(them, normal * -1.0);
                    // Whether a side gives at all is a question about the amount, not
                    // about an exact zero: the blocked answer is the normal's own x
                    // lever, a few times ten to the minus sixteen, and counting that as
                    // a side that gives hands a share of every blow to a body that will
                    // not move. One part in a million of the normal is the same skin the
                    // contacts are taken within; below that a side is a wall.
                    constexpr double kGives = 1e-6;
                    const double freedomMine = dot(mineWay, normal);
                    const double freedomTheirs = dot(theirsWay, normal * -1.0);
                    const bool givesBody = freedomMine > kGives;
                    const bool givesSolid = freedomTheirs > kGives;
                    // What the other body is doing at that place: a piece of an outline
                    // between two corners moves at the lever's interpolation of them, which
                    // is the ball's own outlineTouch. Reading one corner's speed instead is
                    // reading a face's motion at a point that is not on it.
                    // What the other body is doing at that place: a piece of an outline
                    // between two corners moves at the lever's interpolation of them, which
                    // is the ball's own outlineTouch. Reading one corner's speed instead is
                    // reading a face's motion at a point that is not on it.
                    //
                    // Along the surface the same rule decides who gives: the floor holds
                    // a corner's height and nothing else, so a corner on it still slides.
                    const Vec3 tangent{normal.y, -normal.x, 0};
                    const Vec3 mineSlideWay = answerFor(self, tangent);
                    const Vec3 theirsSlideWay = answerFor(them, tangent * -1.0);
                    const double freedomMineSlide = dot(mineSlideWay, tangent);
                    const double freedomTheirsSlide = dot(theirsSlideWay, tangent * -1.0);
                    const bool slidesBody = freedomMineSlide > kGives;
                    const bool slidesSolid = freedomTheirsSlide > kGives;
                    const double shareMineSlide = slidesBody ? bodyShareAt(body, mine, at, tangent) : 0.0;
                    const double shareTheirsSlide = slidesSolid ? bodyShareAt(solid, theirs, at, tangent) : 0.0;
                    const double giveSlide = shareMineSlide + shareTheirsSlide;
                    const double shareMine = givesBody ? bodyShareAt(body, mine, at, normal) : 0.0;
                    const double shareTheirs = givesSolid ? bodyShareAt(solid, theirs, at, normal) : 0.0;
                    // The two sides pull the closing speed apart between them, so the two
                    // shares add: the whole of the approach is answered between them, and a
                    // side that cannot move simply contributes nothing and takes nothing.
                    const double give = shareMine + shareTheirs;
                    // The contact answers two questions -- how hard the two press together,
                    // and how much grip that pressure buys along the surface -- and neither
                    // answer leaves the other alone. An impulse along the surface at a point
                    // off the normal line moves that point along the normal as well, by the
                    // lever it has about the middle: answering the normal once and the
                    // friction once leaves the contact still closing by six thousandths of a
                    // metre a second after both, and a contact that is still closing when
                    // the tick ends is a body sinking into the one it is standing on. So the
                    // two are answered in turn until neither has anything left to say, which
                    // is the same walk the fixed distances and the floor are solved by.
                    //
                    // A side that cannot move gives nothing here either, and by the same
                    // amount test: an answer that only exists as rounding noise is a wall.
                    double carried = 0.0;
                    for (int settle = 0; settle < kContactSettle; ++settle) {
                        const OutlineTouch here = outlineTouch(at, solid.position.data(), solid.count);
                        const Vec3 otherNow =
                            solid.velocity[here.a] * (1.0 - here.t) + solid.velocity[here.b] * here.t;
                        const double closing = dot(body.velocity[i] - otherNow, normal);
                        // The blow this contact carries, which is also what the surface
                        // under it is being pressed with.
                        if (closing < 0.0 && give > 1e-12) {
                            const double impulse = -closing / give;
                            carried += impulse;
                            closingTotal = std::max(closingTotal, -closing);
                            if (givesBody) giveBodyTurn(self, mine, at, mineWay * impulse);
                            if (givesSolid) giveBodyTurn(them, theirs, at, theirsWay * impulse);
                            body.touched = true;
                            solid.touched = true;
                            if (-closing > kRestSpeed) {
                                body.frozen = false;
                                solid.frozen = false;
                            }
                        }
                        // Friction at that same place, and only where something is
                        // pressing the two together: mu_s holds a slide it can carry and
                        // mu_k takes its share of one it cannot, which is the floor's own
                        // rule. It is answered as a solid too, at the same place and
                        // through the same two functions, or it pins one corner of a body
                        // that is turning.
                        if (!(giveSlide > 1e-12)) break;
                        const OutlineTouch held = outlineTouch(at, solid.position.data(), solid.count);
                        const Vec3 solidNow =
                            solid.velocity[held.a] * (1.0 - held.t) + solid.velocity[held.b] * held.t;
                        const double slide = dot(body.velocity[i] - solidNow, tangent);
                        if (!(std::abs(slide) > 0.0)) break;
                        const double pressed =
                            std::max(carried, tick::kGravity * tick::kMass * std::abs(normal.y) * tick::kTick);
                        const double hold = body.staticFriction * pressed;
                        const double slip = body.kineticFriction * pressed;
                        // What it would take to stop the slide, and whether the grip can
                        // carry it: if it can, the two do not move against each other at
                        // all, and if it cannot, they rub at mu_k and lose that much.
                        const double needed = std::abs(slide) / giveSlide;
                        const double rubbed = needed <= hold ? needed : std::min(needed, slip);
                        // The same sign for both sides: `mineSlideWay` is the way this side
                        // may slide and `theirsSlideWay` is the way the other one may, which
                        // is already the opposite direction. Flipping it a second time for
                        // the other side pushed both bodies the same way, so the slide they
                        // were meant to lose was left exactly where it was and a little
                        // momentum was added to the pair on every pass -- a resting body
                        // taking off along the surface it was resting on.
                        const double grip = slide > 0.0 ? -rubbed : rubbed;
                        if (slidesBody) giveBodyTurn(self, mine, at, mineSlideWay * grip);
                        if (slidesSolid) giveBodyTurn(them, theirs, at, theirsSlideWay * grip);
                    }
                }
            }
        }
    }
    return closingTotal;
    };
    // A body is its points held at fixed distances. The distances are solved on the
    // positions every tick, but a contact answers one point's speed, and a point's
    // speed that the distances know nothing about is a shape being pulled apart:
    // the unsupported points go on falling while the supported one is held, which is
    // a body that tips over from nothing at all. So the same fixed distances are
    // solved once on the velocities: what two joined points are closing at is taken
    // out of both of them, equally, because they are equal masses. That is the whole
    // of what makes a body move as one piece after it is struck at a point, and it
    // is what carries the weight of a body held at its corner to the rest of it.
    const auto solveDistances = [&]() {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (body.count == 0 || !body.touched) continue;
        for (int pass = 0; pass < tick::kMaxDistancePasses; ++pass) {
            double worst = 0.0;
            for (std::size_t pair = 0; pair < body.pairCount; ++pair) {
                const std::size_t a = std::size_t(body.pairs[pair][0]), b = std::size_t(body.pairs[pair][1]);
                const Vec3 delta = body.position[b] - body.position[a];
                const double span = length(delta);
                if (!(span > 1e-12)) continue;
                const Vec3 direction = delta * (1.0 / span);
                const double closing = dot(body.velocity[b] - body.velocity[a], direction);
                if (!(std::abs(closing) > kRestSpeed)) continue;
                worst = std::max(worst, std::abs(closing));
                const Vec3 share = direction * (closing * 0.5);
                body.velocity[a] = body.velocity[a] + share;
                body.velocity[b] = body.velocity[b] - share;
            }
            if (worst <= kRestSpeed) break;
        }
    }

    };
    // Contacts until nothing between the bodies is closing. The distances are not
    // solved here: the answer is given to each body as a solid, so the shape is rigid
    // by construction and there is nothing left for a second pass over the fixed
    // distances to repair. Running one anyway takes a velocity field that is already
    // rigid and bends it -- a contact's grip, which was meant to pin a point, comes
    // back out as a shove of the whole body, and a triangle turning about a corner on
    // a cube walks sideways along it.
    for (int solve = 0; solve < kMaxContactPasses; ++solve) {
        const double closing = solveContacts();
        if (closing <= kRestSpeed) break;
    }
    // And once every body in the room has been stepped, no corner is left on the far
    // side of a face: the last word on the tick is that a corner is never inside
    // anything. Nothing here is motion -- it happens after every speed has been
    // decided -- so it can neither add to one nor take from one.
    for (int round = 0; round < kCrossingRounds; ++round) {
        bool moved = false;
        for (std::size_t index = 0; index < kBodyCount; ++index) moved = undoCrossings(index) || moved;
        if (!moved) break;
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
