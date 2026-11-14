// Two bodies meeting.
//
// What is left here is the two questions a pair asks that no single body does: can
// the mouse put a shape down where it would overlap something, and are two outlines
// into each other at all. Neither of them is part of a tick: the tick's own walk is
// in playground_room_body.cpp, and it stops each body at the instant it would meet
// something, which is why nothing here ever has to put two shapes apart.

#include "client/playground_ball_shape.h"
#include "client/playground_pair.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gtc::native {
using namespace ball_shape;
using namespace playground_detail;

// Whether two bodies' outlines are into each other, how deep, and which way the
// first has to go to be out. The walk never lets this happen; the check is here so
// that the walk can prove it: a step that would have left one inside the other is
// not taken.
bool PlaygroundRoom::bodiesIntoEachOther(std::size_t one, std::size_t two, Vec3& way, double& depth) const {
    if (one >= kBodyCount || two >= kBodyCount) return false;
    const Body& a = bodies_[one];
    const Body& b = bodies_[two];
    if (a.count == 0 || b.count == 0) return false;
    Vec3 out{0, 1, 0};
    double amount = 0.0;
    if (!outlineOverlap(a.position.data(), a.count, b.position.data(), b.count, out, amount)) return false;
    // The direction the first body has to travel to be clear of the second.
    way = out * -1.0;
    depth = amount;
    return true;
}

// Whether a body can be put down with its corners where `wanted` says: not through
// another body's outline, not under a corner of one, not through the ball, and not
// through the floor.
//
// This is the same question the sweep asks, asked before the mouse lets go instead of
// after. Everything else in this room refuses to let two shapes be inside each other,
// and the mouse is the one thing that can put them there -- dragging is not simulated,
// so a body follows the cursor wherever it goes. A body released inside another is a
// state the tick cannot answer without inventing motion: the overlap has to be undone
// somewhere, and undoing a hundredth of a metre every tick is a shape creeping out of
// a shape it was never in. So it is not put there in the first place.
bool PlaygroundRoom::bodyPlacementClear(std::size_t index, const std::array<Vec3, kMaxCorners>& wanted) const {
    const Body& body = bodies_[index < kBodyCount ? index : 0];
    if (body.count == 0) return true;
    if (outlineGap(ball_.position, wanted.data(), wanted.data(), body.count, 1.0).gap <
        ball_.radius + kSkin)
        return false;
    for (std::size_t i = 0; i < body.count; ++i)
        if (wanted[i].y < kSurfaceY - kSkin) return false;
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        if (other == index || bodies_[other].count == 0) continue;
        const Body& solid = bodies_[other];
        for (std::size_t i = 0; i < body.count; ++i) {
            const OutlineGap gap = outlineGap(wanted[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
            if (gap.gap < kSkin) return false;
        }
        for (std::size_t i = 0; i < solid.count; ++i) {
            const OutlineGap gap = outlineGap(solid.position[i], wanted.data(), wanted.data(), body.count, 1.0);
            if (gap.gap < kSkin) return false;
        }
        // And a side of each through a side of the other, which is the one pair the
        // corner tests cannot see: two shapes lying across each other with every
        // corner of both outside the other's outline is not possible for convex
        // shapes, but a side passing exactly through a corner is, and it is the state
        // a body put down flush against another one lands in.
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 a = wanted[i], b = wanted[(i + 1) % body.count];
            double along = 2.0;
            Vec3 normal{0, 1, 0};
            if (segmentHitsPolygon(a, b, solid.position.data(), solid.count, along, normal)) return false;
        }
    }
    return true;
}

// Where a pinned body's corners are, turned about the pin.
void PlaygroundRoom::pivotCornersAt(const Body& body, double angle, std::array<Vec3, kMaxCorners>& corners) const {
    const double c = std::cos(angle), s = std::sin(angle);
    for (std::size_t i = 0; i < body.count; ++i)
        corners[i] = {body.pivotPoint.x + body.local[i].x * c - body.local[i].y * s,
                      body.pivotPoint.y + body.local[i].x * s + body.local[i].y * c, 0};
}

// How deep a pinned body would be inside anything at that angle: the floor, another
// shape, or the ball. Zero is clear.
double PlaygroundRoom::pivotDepthAt(const Body& body, double angle) const {
    return std::max(pivotFloorDepthAt(body, angle), pivotObjectDepthAt(body, angle));
}

// The same two questions asked apart, because they are answered by different rules:
// the floor is never negotiable at all, and everything else is a shape that can be a
// hair inside a body it is standing on.
double PlaygroundRoom::pivotFloorDepthAt(const Body& body, double angle) const {
    std::array<Vec3, kMaxCorners> corners{};
    pivotCornersAt(body, angle, corners);
    double deepest = 0.0;
    for (std::size_t i = 0; i < body.count; ++i)
        deepest = std::max(deepest, playground_tick::kSurfaceY - corners[i].y);
    return deepest;
}

double PlaygroundRoom::pivotObjectDepthAt(const Body& body, double angle) const {
    std::array<Vec3, kMaxCorners> corners{};
    pivotCornersAt(body, angle, corners);
    double deepest = 0.0;
    for (std::size_t other = 0; other < kBodyCount; ++other) {
        const Body& solid = bodies_[other];
        if (&solid == &body || solid.count == 0) continue;
        Vec3 way{0, 1, 0};
        double amount = 0.0;
        if (outlineOverlap(corners.data(), body.count, solid.position.data(), solid.count, way, amount)) {
            deepest = std::max(deepest, amount);
        }
    }
    const double gap = outlineGap(ball_.position, corners.data(), corners.data(), body.count, 1.0).gap;
    deepest = std::max(deepest, ball_.radius - gap);
    return deepest;
}

namespace {

// How far a point is inside a convex polygon: the least distance it is inside any of
// the polygon's faces, or nought if it is outside at least one of them.
//
// This is the measure a meeting is judged by. It is per face and not per separating
// axis, so it says "how deep in" and not "how far apart they would be if pushed" --
// and unlike the smallest separating axis it does not get smaller when a body turns
// further into a shape, which is what made a turn look like it was not going anywhere.
double pointInside(const Vec3* corners, std::size_t count, Vec3 point) {
    Vec3 middle{};
    for (std::size_t i = 0; i < count; ++i) middle = middle + corners[i];
    middle = middle * (1.0 / static_cast<double>(count));
    double deepest = 1e30;
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 a = corners[i];
        const Vec3 side = corners[(i + 1) % count] - a;
        const double span = length(side);
        if (!(span > 1e-12)) continue;
        Vec3 outward{side.y / span, -side.x / span, 0.0};
        if (dot(middle - a, outward) > 0.0) outward = outward * -1.0;
        deepest = std::min(deepest, -dot(point - a, outward));
    }
    return std::max(0.0, deepest);
}

// How deep two convex outlines are into each other: the deepest any corner of either
// is inside the other. Nought when they are apart or exactly touching, which is what
// makes "is it deeper than it was" a question worth asking.
double outlineInside(const Vec3* a, std::size_t na, const Vec3* b, std::size_t nb) {
    double deepest = 0.0;
    for (std::size_t i = 0; i < na; ++i) deepest = std::max(deepest, pointInside(b, nb, a[i]));
    for (std::size_t i = 0; i < nb; ++i) deepest = std::max(deepest, pointInside(a, na, b[i]));
    return deepest;
}

// The first crossing of  c1 cos(a) + c2 sin(a) = c3  over a turn that starts at `a0`
// and runs `span` radians one way (`way` is +1 or -1), as the turn's own magnitude.
//
// A sinusoid crosses a level twice a period and nowhere else, so the crossings are the
// two arcsines of the ratio, offset by the sinusoid's phase -- an answer, not a search.
// This is the whole of what a hitscan is: the meeting is where the equation holds, and
// it is found by solving for it. Nothing here samples a path and hopes the meeting fell
// between two samples, and there is no step size to pick.
//
// The turn is measured from where the body is now, so the phase it starts at is part of
// the equation and not something to be dropped: with a = a0 + way t,
//
//     c1 cos a + c2 sin a = cos t (c1 cos a0 + c2 sin a0) + sin t (way (c2 cos a0 - c1 sin a0))
//
// which is the same kind of sinusoid in t.
bool firstSinusoidCrossing(double c1, double c2, double c3, double a0, double way, double span, double& found) {
    const double k1 = c1 * std::cos(a0) + c2 * std::sin(a0);
    const double k2 = way * (c2 * std::cos(a0) - c1 * std::sin(a0));
    const double amplitude = std::hypot(k1, k2);
    if (!(amplitude > 0.0)) return false;
    // A body resting on a face is *exactly* on it, which puts this ratio at one to
    // within rounding -- and a tangency is a crossing. Asked strictly, "the line is a
    // radius away" is false by a part in ten billion for a shape standing on that line,
    // and the turn into it would not be seen at all. So the ratio is only refused when
    // it is outside the sine's range by more than the arithmetic can explain; a root
    // found from a rounded tangency is verified afterwards anyway.
    const double ratio = c3 / amplitude;
    if (!(std::abs(ratio) <= 1.0 + 1e-9)) return false;
    const double phase = std::atan2(k1, k2);
    const double base = std::asin(std::max(-1.0, std::min(1.0, ratio)));
    bool any = false;
    for (int branch = 0; branch < 2; ++branch) {
        const double root = (branch == 0 ? base : kPi - base) - phase;
        // The crossings repeat a full turn apart, and a body never turns a whole turn
        // in one tick, so only the nearest repeats can fall inside the span.
        for (int period = -1; period <= 1; ++period) {
            const double t = root + 2.0 * kPi * double(period);
            if (!(t > -1e-15 && t <= span)) continue;
            if (!any || t < found) found = t;
            any = true;
        }
    }
    return any;
}

}  // namespace

// The first angle of a turn that meets something, as the fraction of the turn the
// body is allowed to take.
//
// This is the hitscan, and it is solved rather than sampled. A turn is travel: the far
// end of a plank sweeps an arc, and an arc that ends above the floor can still have
// passed through it on the way. Every corner of the body sweeps a circle about the
// pin, so every meeting there can be where that circle meets a line -- the floor, or a
// face of another shape -- or another circle, the ball. Those are equations with exact
// answers, so all of a turn's crossings are written down at once instead of being
// stepped towards.
//
// A crossing on its own is not a meeting. A shape resting on the plank has its corners
// *on* the plank's face, so every turn of that face crosses it: turning away is a
// crossing and is nothing at all, and turning into it is a crossing and a contact. The
// two are told apart by the one thing that distinguishes them -- whether the body is
// deeper in a micrometre later than it is at the crossing. Turning away, the depth
// stays where it was; turning in, it grows. Nothing is asked of the current depth, so a
// body that is already a hair inside something can always turn out of it, and a body
// resting against something is never held still by the arithmetic.
double PlaygroundRoom::pivotTurnAllowed(const Body& body, double turn) const {
    if (turn == 0.0) return 1.0;
    // A turn the other way is the same turn measured the other way round: it is solved
    // in u, where the body always turns forwards, and put back at the end.
    const double span = std::abs(turn);
    const double way = turn > 0.0 ? 1.0 : -1.0;
    const Vec3 pin = body.pivotPoint;
    double furthest = 0.0;
    for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
    // A micrometre of the body's own travel, which is the room's resolution: far
    // enough past a crossing to tell an entry from a leaving, and no more.
    const double nudge = furthest > 0.0 ? playground_tick::kQuantum / furthest : playground_tick::kQuantum;
    const auto cornerAt = [&](Vec3 local, double angle) {
        return Vec3{pin.x + local.x * std::cos(angle) - local.y * std::sin(angle),
                    pin.y + local.x * std::sin(angle) + local.y * std::cos(angle), 0.0};
    };
    const auto cornersAt = [&](double angle, std::array<Vec3, kMaxCorners>& into) {
        for (std::size_t i = 0; i < body.count; ++i) into[i] = cornerAt(body.local[i], angle);
    };
    std::array<Vec3, kMaxCorners> here{}, next{};
    // How deep the body is in the floor at an angle, and how deep it is in another
    // shape -- each measure its own, because "deeper" is asked of each meeting apart.
    const auto floorDepth = [&](double angle) {
        double down = 0.0;
        for (std::size_t i = 0; i < body.count; ++i)
            down = std::max(down, playground_tick::kSurfaceY - cornerAt(body.local[i], angle).y);
        return down;
    };
    const auto objectDepth = [&](double angle, std::size_t other) {
        cornersAt(angle, here);
        return outlineInside(here.data(), body.count, bodies_[other].position.data(), bodies_[other].count);
    };
    const auto ballDepth = [&](double angle) {
        cornersAt(angle, here);
        const double gap = outlineGap(ball_.position, here.data(), here.data(), body.count, 1.0).gap;
        return std::max(0.0, ball_.radius - gap);
    };
    double first = span + 1.0;  // no meeting at all
    const auto deeper = [&](double at, double past, const auto& depth) {
        return depth(past) > depth(at) + 1e-12;
    };
    // A body that is already inside something and turning further in is not allowed to
    // begin. It is only the beginning: turning *out* is free, which is what lets a shape
    // that is a hair inside something leave it instead of being frozen there with the
    // contact still feeding the axle a torque -- the pump the King saw.
    //
    // "Inside" is the room's own word for it, and it means deeper than a micrometre.
    // This used to be asked at a picometre, which caught every body that was merely
    // *touching*: a ball resting on the plank is at exactly one radius from it, and the
    // arithmetic that keeps it there lands a nanometre either side -- so a turn that
    // pushed the surface a nanometre into the ball it was carrying was called a turn
    // into something, and the plank was refused. The plank then said one thing with its
    // spin and did another with its angle, which is a seesaw that will not swing under a
    // load: sluggish, and nothing like a plank on an axle. A body that is only touching
    // is not inside anything, and a plank may turn under what is resting on it -- the
    // contact is what carries the load, and it is answered after the turn, not instead
    // of it.
    {
        const double mine = floorDepth(body.angle);
        const double past = floorDepth(body.angle + way * nudge);
        if (mine > playground_tick::kQuantum && past > mine + 1e-12) return 0.0;
        for (std::size_t other = 0; other < kBodyCount; ++other) {
            if (other == std::size_t(&body - &bodies_[0]) || bodies_[other].count == 0) continue;
            const double a0 = objectDepth(body.angle, other);
            const double a1 = objectDepth(body.angle + way * nudge, other);
            if (a0 > playground_tick::kQuantum && a1 > a0 + 1e-12) return 0.0;
        }
        const double b0 = ballDepth(body.angle);
        const double b1 = ballDepth(body.angle + way * nudge);
        if (b0 > playground_tick::kQuantum && b1 > b0 + 1e-12) return 0.0;
    }
    const auto take = [&](double u, double past, const auto& depth) {
        if (!(u > -nudge && u <= span)) return;
        const double at = std::max(0.0, u);
        if (!deeper(at, past, depth)) return;
        if (at < first) first = at;
    };
    for (std::size_t i = 0; i < body.count; ++i) {
        const Vec3 local = body.local[i];
        const double radius = length(local);
        if (!(radius > 0.0)) continue;
        const double phase = std::atan2(local.y, local.x);
        // The floor. The corner's height is pin.y + local.x sin a + local.y cos a, so
        // the crossings are where that is nought.
        {
            double u = 0.0;
            if (firstSinusoidCrossing(local.y, local.x, -pin.y, body.angle, way, span, u))
                take(u, u + nudge, [&](double t) { return floorDepth(body.angle + way * t); });
        }
        for (std::size_t other = 0; other < kBodyCount; ++other) {
            if (other == std::size_t(&body - &bodies_[0]) || bodies_[other].count == 0) continue;
            const Body& solid = bodies_[other];
            // This body's corners against that shape's faces. The corner's arc meets a
            // face's line where the line is a radius away from the pin at the right
            // angle, which is a cosine to solve.
            {
                Vec3 middle{};
                for (std::size_t k = 0; k < solid.count; ++k) middle = middle + solid.position[k];
                middle = middle * (1.0 / static_cast<double>(solid.count));
                for (std::size_t e = 0; e < solid.count; ++e) {
                    const Vec3 p = solid.position[e];
                    const Vec3 side = solid.position[(e + 1) % solid.count] - p;
                    const double sideSpan = length(side);
                    if (!(sideSpan > 1e-12)) continue;
                    Vec3 inward{side.y / sideSpan, -side.x / sideSpan, 0.0};
                    if (dot(middle - p, inward) < 0.0) inward = inward * -1.0;
                    // dot(corner - p, inward) = 0, with the corner on its circle.
                    const double reach = dot(p, inward) - dot(pin, inward);
                    if (std::abs(reach) > radius) continue;
                    const double normalPhase = std::atan2(inward.y, inward.x);
                    const double spread = std::acos(std::max(-1.0, std::min(1.0, reach / radius)));
                    for (const double which : {1.0, -1.0}) {
                        double u = way * (normalPhase + which * spread - phase - body.angle);
                        while (u > span) u -= 2.0 * kPi;
                        while (u <= -span) u += 2.0 * kPi;
                        take(u, u + nudge, [&](double t) { return objectDepth(body.angle + way * t, other); });
                    }
                }
            }
            // And the other way round: a corner of that shape against this body's own
            // faces, which are the ones turning. The distance from a fixed point to a
            // turning face is a sinusoid in the angle, so that is solved the same way.
            {
                Vec3 localMiddle{};
                for (std::size_t k = 0; k < body.count; ++k) localMiddle = localMiddle + body.local[k];
                localMiddle = localMiddle * (1.0 / static_cast<double>(body.count));
                for (std::size_t e = 0; e < body.count; ++e) {
                    const Vec3 a0 = body.local[e];
                    const Vec3 localSide = body.local[(e + 1) % body.count] - a0;
                    const double localSpan = length(localSide);
                    if (!(localSpan > 1e-12)) continue;
                    Vec3 outward{localSide.y / localSpan, -localSide.x / localSpan, 0.0};
                    if (dot(localMiddle - a0, outward) > 0.0) outward = outward * -1.0;
                    for (std::size_t c = 0; c < solid.count; ++c) {
                        const Vec3 arm = solid.position[c] - pin;
                        const double k1 = dot(arm, outward);
                        const double k2 = arm.y * outward.x - arm.x * outward.y;
                        double u = 0.0;
                        if (!firstSinusoidCrossing(k1, k2, dot(a0, outward), body.angle, way, span, u)) continue;
                        take(u, u + nudge, [&](double t) { return objectDepth(body.angle + way * t, other); });
                    }
                }
            }
        }
        // And the ball, which is a circle rather than a face: the corner's own circle
        // against it, and the ball against each of this body's faces.
        {
            const Vec3 away = ball_.position - pin;
            const double reach = length(away);
            if (reach > 0.0 && std::abs(radius - ball_.radius) <= reach && reach <= radius + ball_.radius) {
                const double cosAt =
                    (radius * radius + reach * reach - ball_.radius * ball_.radius) / (2.0 * radius * reach);
                const double spread = std::acos(std::max(-1.0, std::min(1.0, cosAt)));
                const double middle = std::atan2(away.y, away.x);
                for (const double which : {1.0, -1.0}) {
                    double u = way * (middle + which * spread - phase - body.angle);
                    while (u > span) u -= 2.0 * kPi;
                    while (u <= -span) u += 2.0 * kPi;
                    take(u, u + nudge, [&](double t) { return ballDepth(body.angle + way * t); });
                }
            }
            Vec3 localMiddle{};
            for (std::size_t k = 0; k < body.count; ++k) localMiddle = localMiddle + body.local[k];
            localMiddle = localMiddle * (1.0 / static_cast<double>(body.count));
            for (std::size_t e = 0; e < body.count; ++e) {
                const Vec3 a0 = body.local[e];
                const Vec3 localSide = body.local[(e + 1) % body.count] - a0;
                const double localSpan = length(localSide);
                if (!(localSpan > 1e-12)) continue;
                Vec3 outward{localSide.y / localSpan, -localSide.x / localSpan, 0.0};
                if (dot(localMiddle - a0, outward) > 0.0) outward = outward * -1.0;
                const double k1 = dot(away, outward);
                const double k2 = away.y * outward.x - away.x * outward.y;
                double u = 0.0;
                if (!firstSinusoidCrossing(k1, k2, dot(a0, outward) + ball_.radius, body.angle, way, span, u))
                    continue;
                take(u, u + nudge, [&](double t) { return ballDepth(body.angle + way * t); });
            }
        }
    }
    if (!(first <= span)) return 1.0;
    return std::max(0.0, std::min(1.0, first / span));
}

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
        {
            const OutlineGap gap = ball_shape::outlineGap(ball_.position, here.data(), here.data(), body.count, 1.0);
            Approach row;
            row.gap = gap.gap - ball_.radius;
            row.normal = gap.normal;
            row.closingSpeed = pointSpeed(body, ball_.position) - ball_.velocity;
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

    // The soonest any part of the body could be `limit` deep into anything: how much
    // distance is left, over how fast that distance is closing. Written as a time, not
    // as a fraction of the step and not as a distance to walk, so the answer is the
    // meeting itself -- no step size, nothing to tune, and no speed or spin, however
    // large, that can carry the outline through a plank between two looks. A part that
    // is not closing at all is not going to close anything, however long the step is.
    // The face it was stopped by, kept for the caller: a step that was refused is a
    // contact, and a contact has a direction. Which way the body was going into the
    // thing that stopped it is the whole of what is left to answer.
    Vec3 stopping{0, 1, 0};
    double fastest = 0.0;
    const auto soonestMeeting = [&](double t) {
        poseAt(t);
        survey();
        double soonest = 1e30;
        for (const Approach& row : approaches) {
            const double need = row.gap - limit;
            if (!(need > 0.0)) {
                stopping = row.normal;
                return 0.0;
            }
            const double closing = -dot(row.closingSpeed, row.normal);
            if (!(closing > 0.0)) continue;
            const double answer = need / closing;
            if (answer < soonest) {
                soonest = answer;
                stopping = row.normal;
                fastest = closing;
            }
        }
        return soonest;
    };

    double t = 0.0;
    for (int pass = 0; pass < 96; ++pass) {
        const double soonest = soonestMeeting(t);
        if (!(soonest > playground_tick::kQuantum)) {
            if (blockedBy != nullptr) *blockedBy = stopping;
            return t;
        }
        const double next = t + soonest;
        if (!(next < 1.0)) return 1.0;
        t = next;
    }
    (void)fastest;
    if (blockedBy != nullptr) *blockedBy = stopping;
    return t;
}

// How much of its own step a body may keep and still be standing above the floor.
//
// The pose the room draws is a body's middle and its angle, and the corners follow
// from the two: between the pose it began the tick in and the pose it ended it in,
// every pose in between is a pose the body was in. A body turning as it travels
// passes through poses its two ends say nothing about -- the corner of a turning
// square swings on an arc, and the straight line between where it started and where
// it finished is a chord that bows *inside* the arc, which for a corner lying on the
// floor is under it.
//
// So a shape that is stopped part-way through its own step -- the ball's sweep stops
// it where it met the ball -- is stopped at a pose that may not be one the room
// allows. It is put back to the last pose along that step which is above the floor,
// by bisection, and no further. Nothing is moved further than it had already
// travelled, so nothing is added to it: it is the same stop, taken a moment earlier,
// because a corner cannot be under the ground at any moment of the tick.
double PlaygroundRoom::keepAboveFloor(std::size_t index, Vec3 fromCentre, double fromAngle, Vec3 toCentre,
                                      double toAngle, double along) {
    Body& body = bodies_[index];
    if (body.count == 0) return along;
    const auto lowest = [&](double fraction) {
        const Vec3 centre = fromCentre + (toCentre - fromCentre) * fraction;
        const double angle = fromAngle + (toAngle - fromAngle) * fraction;
        const double c = std::cos(angle), s = std::sin(angle);
        double down = 1e30;
        for (std::size_t i = 0; i < body.count; ++i)
            down = std::min(down, centre.y + body.local[i].x * s + body.local[i].y * c);
        return down;
    };
    if (lowest(along) >= playground_tick::kSurfaceY - playground_tick::kQuantum) return along;
    double low = 0.0, high = along;
    for (int bisect = 0; bisect < 60; ++bisect) {
        const double mid = 0.5 * (low + high);
        if (lowest(mid) >= playground_tick::kSurfaceY - playground_tick::kQuantum)
            low = mid;
        else
            high = mid;
    }
    return low;
}

void PlaygroundRoom::separateBodies() {
    for (std::size_t one = 0; one < kBodyCount; ++one) {
        Body& body = bodies_[one];
        if (body.count == 0 || body.frozen || bodyHeld(one)) continue;
        // A pinned body is not separated from anything by moving it: it is where its
        // axle says it is, and a shape that has come to be inside a seesaw's plank is
        // answered by the contact, not by shifting the plank out of the way.
        //
        // It can still be inside something, and what gets it out is its turn.
        //
        // A pinned body has exactly one kind of place -- its angle -- so being
        // inside the floor, inside another shape or inside the ball is answered the
        // only way this body can answer anything: turn it back. That is not a
        // separate rule from every other body's, it is the same one. A free shape
        // that has ended up inside something is moved out along its position, and
        // the only position a pinned body has is which way it is turned.
        //
        // Without this the plank was exempt from the room. Its turn was integrated
        // with nothing asked about what it turned through, and nothing here could
        // give any of it back, so a plank coming down across a cube cut seven
        // centimetres into it and stayed there -- the contact underneath then spent
        // every tick pushing the cube sideways out of the way, which is the drift the
        // King saw. The plank stops where it met the cube and the axle carries the
        // rest, exactly as it would in the yard.
        if (body.pivoted) {
            // The floor is never negotiable. Whatever else is in the way, a plank may
            // not be through the ground, so if there is no angle that clears
            // everything, the one that clears the floor is taken: the floor is the
            // room's own surface and the axle, not the shape standing on it, is what
            // holds the plank up.
            const auto throughFloorOnly = [&](double angle) {
                std::array<Vec3, kMaxCorners> corners{};
                pivotCornersAt(body, angle, corners);
                double deepest = 0.0;
                for (std::size_t i = 0; i < body.count; ++i)
                    deepest = std::max(deepest, playground_tick::kSurfaceY - corners[i].y);
                return deepest;
            };
            const auto clear = [&](double angle) {
                return pivotDepthAt(body, angle) <= playground_tick::kQuantum;
            };
            if (pivotDepthAt(body, body.angle) > playground_tick::kQuantum) {
                // How far a turn of this body carries its furthest corner, which is
                // how the answer is measured: a step that moves the tip four
                // millimetres cannot pass over anything in a room whose thinnest thing
                // is a quarter of a metre.
                double furthest = 0.0;
                for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
                const double grain = furthest > 0.0 ? 0.004 / furthest : 0.01;
                // A pinned body gives way by turning and by nothing else, and it is
                // the axle that decides how much of a turn it is allowed: a plank that
                // swung a whole radian out of an overlap would be a plank that
                // teleported, which is not an answer this room gives. What it will not
                // give way about, the shape standing on it will.
                const double reachable = 0.05;
                const double start = body.angle;
                // Nearest first, both ways round, and only a turn that actually clears
                // is taken. Nothing is turned to an angle chosen in advance.
                for (int pass = 0; pass < 2 && body.angle == start; ++pass) {
                    for (double turned = grain; turned <= reachable + 1e-12; turned += grain) {
                        for (const double side : {1.0, -1.0}) {
                            const double where = start + side * turned;
                            const bool ok = pass == 0 ? clear(where) : throughFloorOnly(where) <= playground_tick::kQuantum;
                            if (!ok) continue;
                            body.angle = where;
                            placeCorners(body);
                            break;
                        }
                        if (body.angle != start) break;
                    }
                }
            }
            continue;
        }
        // Another shape first; the floor has the last word below.
        // Another shape.
        for (std::size_t two = 0; two < kBodyCount; ++two) {
            if (two == one || bodies_[two].count == 0) continue;
            Body& solid = bodies_[two];
            // How much of the move this shape has to make: by inverse mass, so the
            // pair share it the way two bodies share any push between them. A shape
            // asleep or in the mouse's hand is not going anywhere, and takes none.
            const bool solidMoves = !solid.frozen && !bodyHeld(two) && !solid.pivoted;
            const double mine = 1.0 / body.mass;
            const double theirs = solidMoves ? 1.0 / solid.mass : 0.0;
            const double share = mine / (mine + theirs);
            for (std::size_t i = 0; i < body.count; ++i) {
                const ball_shape::OutlineGap gap = ball_shape::outlineGap(body.position[i], solid.position.data(),
                                                                         solid.position.data(), solid.count, 1.0);
                if (!(gap.gap < 0.0)) continue;
                const Vec3 move = gap.normal * (-gap.gap);
                body.centre = body.centre + move * share;
                if (solidMoves) solid.centre = solid.centre - move * (1.0 - share);
                placeCorners(body);
                if (solidMoves) placeCorners(solid);
            }
            // And the same for two shapes meeting along their edges, where no corner
            // of either is inside the other: the pair is moved apart along the axis
            // it is least inside on.
            {
                pair_shape::PairManifold meeting;
                if (pair_shape::separate(body.position.data(), body.count, solid.position.data(), solid.count,
                                         meeting) &&
                    meeting.overlap > 0.0) {
                    const auto inside = [](const Vec3* from, std::size_t count, const Vec3* outline,
                                           std::size_t outlineCount) {
                        for (std::size_t i = 0; i < count; ++i)
                            if (ball_shape::outlineGap(from[i], outline, outline, outlineCount, 1.0).gap < 0.0)
                                return true;
                        return false;
                    };
                    const bool cornersInside =
                        inside(body.position.data(), body.count, solid.position.data(), solid.count) ||
                        inside(solid.position.data(), solid.count, body.position.data(), body.count);
                    if (!cornersInside) {
                        const Vec3 move = meeting.normal * meeting.overlap;
                        body.centre = body.centre + move * share;
                        if (solidMoves) solid.centre = solid.centre - move * (1.0 - share);
                        placeCorners(body);
                        if (solidMoves) placeCorners(solid);
                    }
                }
            }
            // And the other way round: a corner of the other shape inside this one
            // is the same overlap seen from the other side.
            for (std::size_t i = 0; i < solid.count; ++i) {
                const ball_shape::OutlineGap gap = ball_shape::outlineGap(solid.position[i], body.position.data(),
                                                                         body.position.data(), body.count, 1.0);
                if (!(gap.gap < 0.0)) continue;
                const Vec3 move = gap.normal * (-gap.gap);
                if (solidMoves) solid.centre = solid.centre + move * (1.0 - share);
                body.centre = body.centre - move * share;
                if (solidMoves) placeCorners(solid);
                placeCorners(body);
            }
        }
    }
    // And the floor, once, for everything in the room, after everything else has
    // had its say.
    //
    // It used to be answered first, and an overlap with another shape was then taken
    // out of the same body afterwards -- downwards, if the other shape was on top of
    // it -- with the ground it had just been put back on top of already spent. A cube
    // under a seesaw's end was pushed seventeen centimetres into the floor that way.
    // It cannot be answered body by body either: a shape that is put right in its own
    // turn can be pushed back under by the next one, because the shape it is sharing
    // an overlap with is moved after it. The floor is not a suggestion and it is not
    // one body's business: nothing in this room is below it, and that is decided here,
    // at the end, for all of them.
    for (std::size_t one = 0; one < kBodyCount; ++one) {
        Body& body = bodies_[one];
        if (body.count == 0 || body.frozen || bodyHeld(one) || body.pivoted) continue;
        double deepest = 0.0;
        for (std::size_t i = 0; i < body.count; ++i)
            deepest = std::max(deepest, playground_tick::kSurfaceY - body.position[i].y);
        if (deepest > 0.0) {
            body.centre.y += deepest;
            placeCorners(body);
        }
    }
}


// Nothing in this room ends a tick inside anything -- the ball included.
//
// A body is answered by the sweep before it steps, and by separateBodies() after,
// so a shape that arrives somewhere it may not be is either stopped or moved out.
// The ball has the same two halves and needs the same second one, because there is
// one thing its sweep cannot see: a shape that *turns* into it. The ball's sweep
// subtracts the shape's travel, which is exact for a shape that slides, and says
// nothing about a corner coming round -- a triangle spinning at a radian and a half
// a second walks its own corners through a ball while its middle has barely moved.
// A ball that a spinning corner has just swept through must not be left there.
//
// So it is answered where it is, exactly as a shape inside another shape is: moved
// out along the way out of the ball, by the depth it is inside, the pair sharing the
// move by inverse mass, and nothing added to anybody's speed. A ball against the
// floor cannot be given ground and the floor takes the whole of the move, for the
// same reason nothing in this room can be below it.
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
bool PlaygroundRoom::pushBodyOutOfBall(std::size_t index, Vec3 normal, double through) {
    if (index >= kBodyCount || !(through > 0.0)) return false;
    Body& body = bodies_[index];
    if (body.count == 0 || bodyHeld(index) || body.pivoted) {
        // A pinned body cannot give way -- it has nowhere to go -- so a ball that has
        // come to be inside one is answered by the ball moving, which is the honest
        // last resort and the only one left.
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
    for (std::size_t i = 0; i < body.count; ++i) {
        if (playground_tick::kSurfaceY - body.position[i].y > below + playground_tick::kQuantum) {
            body.centre = was;
            placeCorners(body);
            return false;
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
    const double closing = dot(body.velocity, normal);
    if (closing > 0.0) body.velocity = body.velocity - normal * closing;
    ++bodyStepClamped_;
    return true;
}

void PlaygroundRoom::separateBall() {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        Vec3 way{};
        Vec3 at{};
        const double gap = bodyBallGap(index, way, at);
        if (!(gap < 0.0)) continue;
        // The shape gives way, and it gives way *first*, because it is the shape that
        // is where it should not be. Splitting the overlap by mass share -- which is
        // what this used to do -- handed three quarters of it to the ball, because
        // the ball is the lighter of the two, and 75 % of a two-centimetre overlap
        // is a ball moved fifteen millimetres in a tick by nothing at all. That is
        // the teleport; this room does not do teleports.
        //
        // So `way`, which runs out of the ball and into the body, is the direction
        // the body leaves along, and pushBodyOutOfBall does the moving -- including
        // refusing when the body has nowhere to go, because it is on the floor.
        if (pushBodyOutOfBall(index, way * -1.0, -gap)) continue;
        // Nowhere to go: the ball gives the rest of it, and only what is left.
        if (ball_.frozen || dragTarget_ == DragTarget::Ball) continue;
        const Vec3 wanted = ball_.position - way * (-gap);
        if (wanted.y < ball_.radius - kSkin) continue;
        // Counted, because this is the room moving the ball by hand: the shape had
        // nowhere to go and the ball was the only thing left that could.
        ++ballFoundThrough_;
        ball_.position = wanted;
    }
    // And the floor, for the ball as well, after everything else -- the same rule in
    // the same place as the shapes above. The ball's own contact is what rests it on
    // the ground, and normally nothing gets it under: but a shape that has nowhere to
    // go hands the ball the whole of a separation, and a ball pushed through the
    // ground by a shape squeezed beside it is still a ball under the ground. Nothing
    // is below the floor at the end of a tick, which is this line.
    if (ball_.position.y < ball_.radius) ball_.position.y = ball_.radius;
}

}  // namespace gtc::native
