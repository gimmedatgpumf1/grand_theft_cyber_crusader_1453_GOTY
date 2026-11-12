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

bool circleHitsBody(Vec3 from, Vec3 travel, const Vec3* corners, std::size_t count, double radius,
                    OutlineHit& hit) {
    const OutlineGap atStart = outlineGap(from, corners, corners, count, 1.0);
    hit.resting = atStart.gap <= radius + kTouch;
    hit.normal = atStart.normal;
    hit.contact = from - atStart.normal * radius;
    hit.depth = std::max(0.0, radius - atStart.gap);
    if (hit.resting) {
        // On the surface already. What the rest of the tick has to be swept for is
        // going deeper -- rounding the end of a face puts the ball inside the face
        // it is beside -- and a ball that only looked at where it started ended the
        // tick millimetres in. A ball that is already that far in is stopped where
        // it touches.
        if (hit.depth > kSkin) {
            hit.along = 0.0;
            return true;
        }
        // And a ball lying on a face and running along it is not arriving at that
        // face at all: its distance to the face is the same at the end of the tick
        // as at the start, to the last bit of the arithmetic. Asking whether the
        // swept circle ever comes `radius - kTouch` from the outline still finds a
        // time, because "the same to the last bit" is not "the same", and that time
        // is a contact that costs none of the tick: the walk answers it, finds it
        // again a hair further on, answers it again, and spends the whole tick on
        // a surface it is sliding along without moving along it at all. That is the
        // ball that runs down a slope and stops dead with all its speed left. So
        // the question is asked of the geometry: does the travel end up closer to
        // the outline than the thickness of a surface? If it does not, there is
        // nothing to meet -- only something to slide along, which is what the
        // resting contact at the end of the tick is for.
        const OutlineGap atEnd = outlineGap(from + travel, corners, corners, count, 1.0);
        if (!(atEnd.gap < radius - kSkin) && !(atEnd.gap < atStart.gap - kSkin)) return false;
        hit.along = firstTouch(from, travel, corners, count, radius - kTouch);
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
