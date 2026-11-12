// Two convex shapes meeting, when neither one's corner is inside the other.
//
// Corner-against-outline finds a shape standing on another one, which is what most
// meetings are -- but it is not all of them, and the ones it misses are the ones
// that hurt. A wide shape lying across a narrow one touches along two edges and no
// corner at all: its base edge crosses the other shape's two upright edges and
// everything else is outside. Found that way, a triangle resting across a cube is
// not touching anything. Nothing gathers it, nothing answers its weight, and it
// gathers speed with nowhere to go for as long as it is left there.
//
// So the pair is separated on the axis it is least apart on -- the one a convex pair
// is always separated on, if it is separated at all -- and the places it is touching
// are where their edges cross. For the triangle across the cube those are exactly
// the two ends of the cube's top face, which is where the load really is.

#ifndef GTC_NATIVE_CLIENT_PLAYGROUND_PAIR_H
#define GTC_NATIVE_CLIENT_PLAYGROUND_PAIR_H

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "native.h"

namespace gtc::native::pair_shape {

// How many places one meeting can be touched at. Two, for the same reason a face on
// a floor has two ends: a load that can sit anywhere between two points is what lets
// a shape stand on a surface at all.
constexpr std::size_t kMaxPoints = 2;

struct PairManifold {
    // Out of `b` and into `a`, which is the way a corner of `a` inside `b` has always
    // been reported: everything downstream already reads it that way.
    Vec3 normal{0.0, 1.0, 0.0};
    Vec3 at[kMaxPoints]{};
    // How far into `b` each place is, in metres. Positive is overlap; a pair that is
    // only just touching comes out at zero.
    double depth[kMaxPoints]{};
    // The least of the depths: how far the pair overlap at all. Negative or zero
    // means they do not.
    double overlap = 0.0;
    std::size_t ends = 0;
};

namespace detail {

inline double wedge(Vec3 u, Vec3 v) { return u.x * v.y - u.y * v.x; }

// The distance from a point to a segment, and the nearest point on it.
inline double pointSegment(Vec3 p, Vec3 a, Vec3 b, Vec3& nearest) {
    const Vec3 side = b - a;
    const double lengthSquared = side.x * side.x + side.y * side.y;
    if (!(lengthSquared > 1e-18)) {
        nearest = a;
        return std::hypot(p.x - a.x, p.y - a.y);
    }
    double t = ((p.x - a.x) * side.x + (p.y - a.y) * side.y) / lengthSquared;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    nearest = a + side * t;
    return std::hypot(p.x - nearest.x, p.y - nearest.y);
}

// The distance between two segments, and the nearest point on the first. A crossing
// pair is at distance nothing, and the crossing itself is returned.
inline double segmentSegment(Vec3 a1, Vec3 a2, Vec3 b1, Vec3 b2, Vec3& nearest) {
    const Vec3 u = a2 - a1, v = b2 - b1;
    const double denominator = wedge(u, v);
    if (std::abs(denominator) > 1e-18) {
        const Vec3 offset = b1 - a1;
        const double t = wedge(offset, v) / denominator;
        const double s = wedge(offset, u) / denominator;
        if (t >= 0.0 && t <= 1.0 && s >= 0.0 && s <= 1.0) {
            nearest = a1 + u * t;
            return 0.0;
        }
    }
    // Not crossing: the nearest of the four ends to the other segment.
    double best = 1e30;
    Vec3 onOther;
    double d = pointSegment(a1, b1, b2, onOther);
    if (d < best) { best = d; nearest = a1; }
    d = pointSegment(a2, b1, b2, onOther);
    if (d < best) { best = d; nearest = a2; }
    d = pointSegment(b1, a1, a2, onOther);
    if (d < best) { best = d; nearest = onOther; }
    d = pointSegment(b2, a1, a2, onOther);
    if (d < best) { best = d; nearest = onOther; }
    return best;
}

}  // namespace detail

// Two convex outlines meeting: how far apart they are, and where.
//
// This is the separating-axis test, and it is the whole of shape against shape: every
// edge of both outlines is a candidate axis, the pair's separation is the *largest*
// of the clearances those axes measure, and if that is bigger than the band the two
// are simply not touching. The axis that wins is the one they are closest on, which
// for two convex outlines is the one the closest faces share -- the way the load
// between them can go.
//
// The places they touch are the part of the other shape's near face that lies within
// the winner's face, clipped out exactly. That is not a corner picked out of the two
// outlines: a wide shape lying across a narrow one touches along the whole of the
// narrow one's face and no corner of either, and a corner picked instead is a lever
// the load does not act at. One point per end of the clipped span, each with its own
// signed separation, is a load that can sit anywhere between them -- which is what
// lets a shape stand on a surface instead of rocking on a corner.
//
// The separations are signed and exact. Positive is how much further that end has to
// travel to arrive; negative is how deep it already is. The solver needs the first to
// know how much of the tick an end may spend closing, and the second to know it has
// to give way. Nothing here is a tolerance on the physics.
inline bool collide(const Vec3* a, std::size_t na, const Vec3* b, std::size_t nb, double band,
                    PairManifold& out) {
    if (na < 3 || nb < 3) return false;
    // The face of either shape that the other one is furthest outside of.
    double least = -1e30;
    Vec3 faceNormal{};
    bool faceIsA = true;
    std::size_t faceEdge = 0;
    const auto tryFaces = [&](const Vec3* shape, std::size_t count, const Vec3* other, std::size_t otherCount,
                              bool isA) {
        for (std::size_t e = 0; e < count; ++e) {
            const Vec3 from = shape[e], to = shape[(e + 1) % count];
            const Vec3 side = to - from;
            const double span = std::hypot(side.x, side.y);
            if (!(span > 1e-12)) continue;
            // The outline runs one way round, so this is out of the shape.
            const Vec3 normal{side.y / span, -side.x / span, 0.0};
            // How far the other shape's nearest corner is outside this face. If every
            // corner is in front of it, the two are apart along this axis by that much.
            double nearest = 1e30;
            for (std::size_t i = 0; i < otherCount; ++i) {
                const Vec3 p = other[i] - from;
                nearest = std::min(nearest, normal.x * p.x + normal.y * p.y);
            }
            if (nearest > least) {
                least = nearest;
                faceNormal = normal;
                faceIsA = isA;
                faceEdge = e;
            }
        }
    };
    tryFaces(a, na, b, nb, true);
    tryFaces(b, nb, a, na, false);
    // Further apart than the band on the closest axis: not touching. The band is how
    // far anything can travel before this tick is over.
    if (!(least <= band)) return false;
    // Out of the other shape and into this one, the way every patch in the room is
    // written: against the outward normal when the face is this shape's own, along it
    // when the face belongs to the other one.
    out.normal = faceIsA ? faceNormal * -1.0 : faceNormal;
    const Vec3* owner = faceIsA ? a : b;
    const std::size_t ownerCount = faceIsA ? na : nb;
    const Vec3* other = faceIsA ? b : a;
    const std::size_t otherCount = faceIsA ? nb : na;
    const Vec3 faceFrom = owner[faceEdge];
    const Vec3 faceTo = owner[(faceEdge + 1) % ownerCount];
    // The near face of the other shape: the one whose outward normal is most against
    // the face the load goes through. That is the one lying on it.
    std::size_t incident = 0;
    double mostAgainst = 1e30;
    for (std::size_t e = 0; e < otherCount; ++e) {
        const Vec3 from = other[e], to = other[(e + 1) % otherCount];
        const Vec3 side = to - from;
        const double span = std::hypot(side.x, side.y);
        if (!(span > 1e-12)) continue;
        const Vec3 normal{side.y / span, -side.x / span, 0.0};
        const double against = faceNormal.x * normal.x + faceNormal.y * normal.y;
        if (against < mostAgainst) {
            mostAgainst = against;
            incident = e;
        }
    }
    // Clip that face to the span of the face it is lying on. A point of the incident
    // face past the end of the surface is beyond the surface, not on it.
    Vec3 low = other[incident], high = other[(incident + 1) % otherCount];
    const Vec3 along = faceTo - faceFrom;
    const double lowBound = along.x * faceFrom.x + along.y * faceFrom.y;
    const double highBound = along.x * faceTo.x + along.y * faceTo.y;
    const double boundLow = std::min(lowBound, highBound), boundHigh = std::max(lowBound, highBound);
    for (int side = 0; side < 2; ++side) {
        const double bound = side == 0 ? boundLow : boundHigh;
        const double atLow = along.x * low.x + along.y * low.y - bound;
        const double atHigh = along.x * high.x + along.y * high.y - bound;
        const bool lowIn = side == 0 ? atLow >= 0.0 : atLow <= 0.0;
        const bool highIn = side == 0 ? atHigh >= 0.0 : atHigh <= 0.0;
        if (lowIn && highIn) continue;
        if (!lowIn && !highIn) return false;
        const double t = atLow / (atLow - atHigh);
        const Vec3 crossing = low + (high - low) * t;
        if (lowIn) high = crossing;
        else low = crossing;
    }
    out.ends = 0;
    out.overlap = 0.0;
    for (int which = 0; which < 2; ++which) {
        const Vec3 at = which == 0 ? low : high;
        if (which == 1) {
            const Vec3 apart = at - low;
            if (std::hypot(apart.x, apart.y) < 1e-9) break;
        }
        const Vec3 from = at - faceFrom;
        const double separation = faceNormal.x * from.x + faceNormal.y * from.y;
        out.at[out.ends] = at;
        out.depth[out.ends] = -separation;  // positive is overlap, as it always was
        out.overlap = std::max(out.overlap, -separation);
        ++out.ends;
    }
    return out.ends > 0;
}

// Kept for the position pass, which only ever asks how far inside something is.
inline bool separate(const Vec3* a, std::size_t na, const Vec3* b, std::size_t nb, PairManifold& out) {
    if (!collide(a, na, b, nb, 0.0, out)) return false;
    return out.overlap > 0.0;
}

}  // namespace gtc::native::pair_shape

#endif  // GTC_NATIVE_CLIENT_PLAYGROUND_PAIR_H
