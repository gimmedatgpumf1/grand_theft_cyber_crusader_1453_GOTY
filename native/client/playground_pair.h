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

// The way out of `b` for `a`, and how far in they are.
//
// Separated on every face normal of both shapes, which for two convex outlines is
// every way there is to be apart: the axis of least overlap is the shortest way out,
// and if any axis shows a clear gap the two are not touching at all.
inline bool separate(const Vec3* a, std::size_t na, const Vec3* b, std::size_t nb, PairManifold& out,
                     double band = 0.0) {
    if (na < 3 || nb < 3) return false;
    double least = 1e30;
    Vec3 axis{0.0, 1.0, 0.0};
    bool found = false;
    // Every face of both shapes, as an outward normal and the line it lies on.
    const auto tryFace = [&](const Vec3* shape, std::size_t count, std::size_t edge, const Vec3* other,
                             std::size_t otherCount) {
        const Vec3 from = shape[edge], to = shape[(edge + 1) % count];
        const Vec3 side = to - from;
        const double span = std::hypot(side.x, side.y);
        if (!(span > 1e-12)) return;
        // The outline runs one way round, so this is out of the shape.
        const Vec3 normal{side.y / span, -side.x / span, 0.0};
        // How far the other shape reaches behind this face.
        double deepest = -1e30;
        for (std::size_t i = 0; i < otherCount; ++i) {
            const Vec3 p = other[i] - from;
            deepest = std::max(deepest, -(normal.x * p.x + normal.y * p.y));
        }
        if (!found || deepest < least) {
            least = deepest;
            axis = normal;
            found = true;
        }
    };
    for (std::size_t e = 0; e < na; ++e) tryFace(a, na, e, b, nb);
    for (std::size_t e = 0; e < nb; ++e) tryFace(b, nb, e, a, na);
    // A pair that is only just apart is still a pair. `least` is the distance by
    // which one shape is behind the other's face; when the two have not met yet it
    // is negative, and how negative is exactly how far apart they are. Asked for a
    // band, this answers with a meeting that has not happened yet -- a speculative
    // one -- so that a shape arriving at a surface this tick is answered by the
    // surface instead of being walked up to it and left there with its speed still
    // on. An overlap deeper than anything the band allows is a real meeting, and it
    // is answered as one whatever the band says.
    if (!found || least <= -band) return false;
    // The normal came out of whichever shape owned that face; the answer is wanted
    // out of `b` and into `a`, so point it that way.
    double centre = 0.0;
    for (std::size_t i = 0; i < na; ++i) centre += axis.x * a[i].x + axis.y * a[i].y;
    centre /= static_cast<double>(na);
    double otherCentre = 0.0;
    for (std::size_t i = 0; i < nb; ++i) otherCentre += axis.x * b[i].x + axis.y * b[i].y;
    otherCentre /= static_cast<double>(nb);
    if (centre < otherCentre) axis = axis * -1.0;
    out.normal = axis;
    out.overlap = least;
    // Where they are touching: their edges where they cross. For two shapes meeting
    // along a face that is the two ends of the overlap, which is the segment the load
    // between them sits on.
    out.ends = 0;
    for (std::size_t i = 0; i < na && out.ends < kMaxPoints; ++i) {
        const Vec3 a1 = a[i], a2 = a[(i + 1) % na];
        for (std::size_t j = 0; j < nb && out.ends < kMaxPoints; ++j) {
            const Vec3 b1 = b[j], b2 = b[(j + 1) % nb];
            Vec3 nearest;
            if (!(detail::segmentSegment(a1, a2, b1, b2, nearest) <= 1e-12)) continue;
            // One arrow per place, the way the patch solver expects.
            bool seen = false;
            for (std::size_t k = 0; k < out.ends; ++k) seen = seen || std::hypot(nearest.x - out.at[k].x,
                                                                                nearest.y - out.at[k].y) < 1e-9;
            if (seen) continue;
            out.at[out.ends] = nearest;
            out.depth[out.ends] = least;
            ++out.ends;
        }
    }
    // Two edges that cross nowhere can still be pressed face to face. Then the place
    // they touch is the deepest corner of the overlap, which is a crossing of the
    // near edges when there is one and otherwise the middle of the shorter side.
    if (out.ends == 0) {
        double deepest = 1e30;
        Vec3 where{};
        for (std::size_t i = 0; i < na; ++i) {
            const double d = out.normal.x * a[i].x + out.normal.y * a[i].y;
            if (d < deepest) { deepest = d; where = a[i]; }
        }
        out.at[0] = where;
        out.depth[0] = least;
        out.ends = 1;
    }
    return true;
}

// The least gap between two outlines, measured the way a walk needs it: corner to
// outline, and edge to edge, so a wide shape coming down on a narrow one is seen
// while it is still coming. Never negative -- an overlap is a gap of nothing.
inline double gap(const Vec3* a, std::size_t na, const Vec3* b, std::size_t nb) {
    if (na < 3 || nb < 3) return 1e30;
    PairManifold manifold;
    if (separate(a, na, b, nb, manifold)) return 0.0;
    double least = 1e30;
    for (std::size_t i = 0; i < na; ++i) {
        const Vec3 a1 = a[i], a2 = a[(i + 1) % na];
        for (std::size_t j = 0; j < nb; ++j) {
            Vec3 nearest;
            least = std::min(least, detail::segmentSegment(a1, a2, b[j], b[(j + 1) % nb], nearest));
        }
    }
    return least;
}

}  // namespace gtc::native::pair_shape

#endif  // GTC_NATIVE_CLIENT_PLAYGROUND_PAIR_H
