// The ball's geometry: where a circle and an outline first touch.
//
// This is a hitscan, not a search. Every meeting it reports is solved for outright,
// which is why the ball is never found inside a shape it crossed quickly: there is
// no step size here that could be too coarse to catch it.

#include "client/playground_ball_shape.h"

namespace gtc::native::ball_shape {

// Is this point inside the outline? The same test the point uses to decide it
// has been left inside a body: it is inside when it is on the inner side of every
// side. Nothing is assumed about the order the sides are wound in beyond that.
bool insideOutline(Vec3 point, const Vec3* corners, std::size_t count) {
    for (std::size_t edge = 0; edge < count; ++edge) {
        const Vec3 a = corners[edge];
        const Vec3 b = corners[(edge + 1) % count];
        const Vec3 side = b - a;
        const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
        if (!(sideLength > 1e-12)) return false;
        const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        if (dot(point - a, outward) > 0.0) return false;
    }
    return true;
}

OutlineTouch outlineTouch(Vec3 point, const Vec3* corners, std::size_t count) {
    OutlineTouch touch;
    double nearest = 1e30;
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 a = corners[i];
        const Vec3 b = corners[(i + 1) % count];
        const Vec3 side = b - a;
        const double length2 = dot(side, side);
        if (!(length2 > 1e-18)) continue;
        const double t = std::clamp(dot(point - a, side) / length2, 0.0, 1.0);
        const Vec3 closest = a + side * t;
        const double distance = length(point - closest);
        if (!(distance < nearest)) continue;
        nearest = distance;
        touch.point = closest;
        touch.a = i;
        touch.b = (i + 1) % count;
        touch.t = t;
    }
    return touch;
}

OutlineGap outlineGap(Vec3 point, const Vec3* previous, const Vec3* current, std::size_t count,
                      double fraction) {
    Vec3 corners[8];
    Vec3 middle{};
    for (std::size_t i = 0; i < count; ++i) {
        corners[i] = previous[i] + (current[i] - previous[i]) * fraction;
        middle = middle + corners[i];
    }
    middle = middle * (1.0 / static_cast<double>(count));
    double nearest = 1e30;
    Vec3 nearestPoint{};
    Vec3 way{0, 1, 0};
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 a = corners[i];
        const Vec3 b = corners[(i + 1) % count];
        const Vec3 side = b - a;
        const double length2 = dot(side, side);
        if (!(length2 > 1e-18)) continue;
        const double t = std::clamp(dot(point - a, side) / length2, 0.0, 1.0);
        const Vec3 closest = a + side * t;
        const double distance = length(point - closest);
        if (!(distance < nearest)) continue;
        nearest = distance;
        nearestPoint = closest;
        const double sideLength = std::sqrt(length2);
        // Which side of this edge is outside: the body's own middle is inside, so
        // the outward normal is the one the middle is not on.
        Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        if (dot(middle - a, outward) > 0.0) outward = outward * -1.0;
        way = outward;
    }
    OutlineGap result;
    const bool within = insideOutline(point, corners, count);
    result.gap = within ? -nearest : nearest;
    // Which way is out. Away from the nearest piece of the outline -- and where
    // that piece is a corner, away from the corner, not along the face it ends.
    // A corner is where a falling shape meets a ball that is beside it, and
    // answering that with the face's own normal slides the shape along the face
    // and through the ball instead of off it.
    if (!within && nearest > 1e-12)
        result.normal = (point - nearestPoint) * (1.0 / nearest);
    else
        result.normal = way;
    return result;
}

// The first fraction of the travel at which the ball's middle comes within
// `reach` of the outline, or 2 if it never does. A face is met where its line
// comes that close; a corner where the middle comes that close to the corner.
// Both are solved outright. Nothing is sampled, so no speed is too fast to see:
// a ball crossing the room in a tick meets whatever lies across its path.
double firstTouch(Vec3 from, Vec3 travel, const Vec3* corners, std::size_t count, double reach) {
    Vec3 middle{};
    for (std::size_t i = 0; i < count; ++i) middle = middle + corners[i];
    middle = middle * (1.0 / static_cast<double>(count));
    const double square = dot(travel, travel);
    double first = 2.0;
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 a = corners[i];
        const Vec3 b = corners[(i + 1) % count];
        const Vec3 side = b - a;
        const double sideLength = length(side);
        if (!(sideLength > 1e-18)) continue;
        Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        if (dot(middle - a, outward) > 0.0) outward = outward * -1.0;
        const double approach = dot(outward, travel);
        if (approach < 0.0) {
            const double t = (reach - dot(outward, from - a)) / approach;
            // Past either end of the face, that end is what the ball meets, and
            // the corners below are where that is worked out.
            if (t >= 0.0 && t < first &&
                dot(from + travel * t - a, side) >= 0.0 && dot(from + travel * t - a, side) <= sideLength * sideLength)
                first = t;
        }
        if (!(square > 0.0)) continue;
        for (const Vec3 corner : {a, b}) {
            const Vec3 away = from - corner;
            const double half = dot(away, travel);
            const double disc = half * half - square * (dot(away, away) - reach * reach);
            if (disc < 0.0) continue;
            const double t = (-half - std::sqrt(disc)) / square;
            if (t >= 0.0 && t < first) first = t;
        }
    }
    return first;
}

// The outline at one fraction of a tick, corner by corner, each corner on its own
// straight step from where it was to where it is. That is what the body actually
// did: the room carries a shape by moving its centre and turning it, and the
// corners' steps are what that motion left behind.
static void outlineAt(const Vec3* previous, const Vec3* current, std::size_t count, double fraction,
                      Vec3* corners) {
    for (std::size_t i = 0; i < count; ++i) corners[i] = previous[i] + (current[i] - previous[i]) * fraction;
}

// How fast the piece of outline nearest `point` is travelling, from the two corners
// that own it. The piece between two corners moves at the lever's interpolation of
// the two of them -- which is exactly what a rigid body's rotation does along an
// edge, and what a body turning on the spot does to the place it is touching.
static Vec3 outlineSpeed(const Vec3* previous, const Vec3* current, std::size_t count, Vec3 point,
                         double tick) {
    const OutlineTouch touch = outlineTouch(point, current, count);
    const Vec3 stepA = current[touch.a] - previous[touch.a];
    const Vec3 stepB = current[touch.b] - previous[touch.b];
    return (stepA * (1.0 - touch.t) + stepB * touch.t) * (1.0 / tick);
}

bool sweptBallAgainstMoving(const Vec3* previous, const Vec3* current, std::size_t count,
                            Vec3 ballPosition, Vec3 ballVelocity, double radius, double tick,
                            OutlineHit& hit) {
    Vec3 corners[kMaxOutlineCorners];
    // Where the ball is at the start: already against the surface or already
    // through it. That is not a meeting to find, it is the state the tick begins
    // in, and the caller has an answer for it (a contact for the whole tick).
    outlineAt(previous, current, count, 0.0, corners);
    const OutlineGap start = outlineGap(ballPosition, corners, corners, count, 1.0);
    if (start.gap <= radius + kTouch) {
        hit.resting = true;
        hit.along = 0.0;
        hit.normal = start.normal;
        hit.depth = std::max(0.0, radius - start.gap);
        hit.contact = ballPosition - start.normal * radius;
        return true;
    }
    hit.resting = false;
    // Conservative advancement. `safe` is the last fraction known to be clear, and
    // `unsafe` the first known to be touching; when a step lands inside, the two
    // bracket the meeting and it is closed in on -- so a body that swings quickly
    // through the ball cannot hide the moment it arrived between two steps.
    double safe = 0.0;
    double unsafe = 2.0;
    double fraction = 0.0;
    for (int step = 0; step < 64; ++step) {
        outlineAt(previous, current, count, fraction, corners);
        const Vec3 at = ballPosition + ballVelocity * (tick * fraction);
        const OutlineGap gap = outlineGap(at, corners, corners, count, 1.0);
        if (gap.gap <= radius) {
            unsafe = fraction;
            break;
        }
        // The distance to close, at the rate the two surfaces are closing at.
        const Vec3 surface = outlineSpeed(previous, current, count, at, tick);
        const double closing = -dot(ballVelocity - surface, gap.normal);
        // Not closing: whatever happens later in the tick, these two are moving
        // apart along the way they would meet, and there is no meeting to report.
        if (!(closing > 0.0)) return false;
        const double needed = (gap.gap - radius) / closing;  // seconds
        if (!(needed > 0.0)) {
            unsafe = fraction;
            break;
        }
        safe = fraction;
        fraction += needed / tick;
        if (fraction > 1.0) return false;
    }
    if (!(unsafe <= 1.0)) {
        if (!(fraction <= 1.0)) return false;
        unsafe = fraction;
    }
    // Close the bracket in on the meeting. This is a time found by solving for the
    // distance, not by sampling for it, so where it lands does not depend on how
    // fast anything is going.
    double low = safe, high = std::min(unsafe, 1.0);
    for (int bisect = 0; bisect < kBisections; ++bisect) {
        const double middle = 0.5 * (low + high);
        outlineAt(previous, current, count, middle, corners);
        const Vec3 at = ballPosition + ballVelocity * (tick * middle);
        if (outlineGap(at, corners, corners, count, 1.0).gap <= radius)
            high = middle;
        else
            low = middle;
    }
    outlineAt(previous, current, count, high, corners);
    const Vec3 at = ballPosition + ballVelocity * (tick * high);
    const OutlineGap met = outlineGap(at, corners, corners, count, 1.0);
    hit.along = high;
    hit.normal = met.normal;
    hit.contact = at - met.normal * radius;
    hit.depth = std::max(0.0, radius - met.gap);
    return true;
}

bool circleHitsBody(Vec3 from, Vec3 travel, const Vec3* corners, std::size_t count, double radius,
                    OutlineHit& hit) {
    const OutlineGap atStart = outlineGap(from, corners, corners, count, 1.0);
    hit.resting = atStart.gap <= radius + kTouch;
    hit.normal = atStart.normal;
    hit.contact = from - atStart.normal * radius;
    hit.depth = std::max(0.0, radius - atStart.gap);
    if (hit.resting) {
        // On the surface already: a contact for the whole tick, and the ball rolls
        // along it. What the rest of the tick still has to be swept for is going
        // deeper -- rounding the end of a face puts the ball inside the face it is
        // beside -- and a ball that only looked at where it started ended the tick
        // millimetres in. The margin is the rounding allowance itself, so a ball
        // simply lying on a surface spends its travel and one being pressed into
        // it is stopped where it touches it.
        hit.along = hit.depth > kSkin ? 0.0 : firstTouch(from, travel, corners, count,
                                                        std::min(radius, atStart.gap) - kTouch);
        return true;
    }
    const double first = firstTouch(from, travel, corners, count, radius);
    if (first > 1.0) return false;
    // Which way the surface faces where the ball meets it. A corner is answered by
    // the line from the corner, not by either face that ends there.
    const Vec3 at = from + travel * first;
    const OutlineGap touched = outlineGap(at, corners, corners, count, 1.0);
    hit.along = std::min(first, 1.0);
    hit.normal = touched.normal;
    hit.contact = at - touched.normal * radius;
    hit.depth = 0.0;
    return true;
}

}  // namespace gtc::native::ball_shape
