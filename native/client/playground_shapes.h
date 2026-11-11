#pragma once

// The drawing and the point-in-shape tests the playground room needs. They are
// here rather than in the room so that neither file grows past the size a file
// is allowed to be.

#include "client/playground_room.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gtc::native::playground_detail {

// The cube is 0.70 m on a side and the triangle is three times the cube.
// How big the dot is drawn, and therefore how big a thing the ball and the
// bodies see. One number, because the picture and the contact must agree.
inline constexpr double kPointRadius = 0.07;
// What counts as not moving. This is the room's existing rest level -- the dot
// freezes below it -- and it is a statement about the arithmetic, not about
// physics: a speed of a nanometre per second moves an object a fiftieth of a
// picometre in a tick, which no double here can tell from standing still.
inline constexpr double kRestSpeed = 1e-9;  // m/s
inline constexpr double kHalfSize = 0.35;
inline constexpr double kCubeSide = 2.0 * kHalfSize;
inline constexpr double kTriangleLeg = 3.0 * kCubeSide;
inline constexpr double kStartAngle = 0.30;  // radians, so a corner leads the landing

// Batches carry a model matrix that the renderer reads every frame, so the
// point and its markers move without rebuilding or re-uploading any geometry.
inline std::array<double, 16> translation(Vec3 point) {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, point.x, point.y, point.z, 1};
}

// A unit quad laid along local +x, so one matrix turns it into this tick's
// hitscan segment, or into one edge of a body.
inline std::array<double, 16> segment(Vec3 from, Vec3 to, double halfWidth) {
    const Vec3 delta = to - from;
    const double alongLength = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (!(alongLength > 1e-9)) return translation({0, -1000, 0});
    const Vec3 along{delta.x, delta.y, 0};
    const Vec3 across{-delta.y / alongLength * halfWidth, delta.x / alongLength * halfWidth, 0};
    return {along.x, along.y, 0, 0, across.x, across.y, 0, 0, 0, 0, 1, 0,
            from.x, from.y, from.z, 1};
}

// A body's rest shape, placed where the body is and turned how far round it has
// come. Its corners are drawn through this, so the face drawn between them is
// the face those corners make.
inline std::array<double, 16> pose(Vec3 centre, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, centre.x, centre.y, centre.z, 1};
}

// One flat colour, no shading: these are markers, not surfaces.
inline Material flat(const char* hex) {
    Material material;
    material.color = colorHex(hex);
    material.unlit = true;
    material.constantLighting = true;
    return material;
}

inline void quad(std::vector<Vec3>& points, std::vector<std::array<int, 3>>& faces,
          double x0, double y0, double x1, double y1, double z) {
    const int base = static_cast<int>(points.size());
    points.push_back({x0, y0, z});
    points.push_back({x1, y0, z});
    points.push_back({x1, y1, z});
    points.push_back({x0, y1, z});
    faces.push_back({base, base + 1, base + 2});
    faces.push_back({base, base + 2, base + 3});
}

inline void disc(std::vector<Vec3>& points, std::vector<std::array<int, 3>>& faces, double radius) {
    constexpr int kSegments = 32;
    points.clear();
    faces.clear();
    points.push_back({0, 0, 0});
    for (int i = 0; i < kSegments; ++i) {
        const double angle = 2.0 * 3.14159265358979323846 * double(i) / double(kSegments);
        points.push_back({radius * std::cos(angle), radius * std::sin(angle), 0});
    }
    for (int i = 0; i < kSegments; ++i) faces.push_back({0, 1 + i, 1 + (i + 1) % kSegments});
}

// A body's face: its corners in its own rest shape, fanned from the first one.
// Three corners make one triangle, four make the square.
inline void face(std::vector<Vec3>& points, std::vector<std::array<int, 3>>& faces, const Vec3* local,
          std::size_t count, double z) {
    points.clear();
    faces.clear();
    for (std::size_t i = 0; i < count; ++i) points.push_back({local[i].x, local[i].y, z});
    for (std::size_t i = 1; i + 1 < count; ++i) faces.push_back({0, int(i), int(i + 1)});
}

inline std::size_t findBatch(const GpuScene& scene, std::uint32_t objectId) {
    for (std::size_t index = 0; index < scene.batches.size(); ++index) {
        if (scene.batches[index].objectId == objectId) return index;
    }
    return 0;
}

// The corners of a body, in order round its outline, and the distances between
// them that are held: every edge, plus both diagonals so a square cannot fold
// into a diamond. A triangle is already rigid with its three edges, and its
// list simply has no diagonals to add.
constexpr std::array<std::array<int, 2>, 6> kSquarePairs{{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 2}, {1, 3}}};
constexpr std::array<std::array<int, 2>, 6> kTrianglePairs{{{0, 1}, {1, 2}, {2, 0}, {0, 1}, {0, 1}, {0, 1}}};


// What each body is when the room starts: its corners about its own middle, the
// distances it holds, and where that middle is and how far it is turned.
struct Shape {
    std::size_t count = 0, pairCount = 0;
    std::array<Vec3, 4> local{};
    std::array<std::array<int, 2>, 6> pairs{};
    Vec3 middle{};
    double angle = 0;
    // This body's own pair of coefficients.
    double staticFriction = 0, kineticFriction = 0;
};

inline constexpr double kThird = 1.0 / 3.0;
inline constexpr std::array<Shape, PlaygroundRoom::kBodyCount> kShapes{{
    // The square, turned so a corner leads: it lands on a corner and has to be
    // held up by the other three.
    {4,
     6,
     {{{-kHalfSize, -kHalfSize, 0}, {kHalfSize, -kHalfSize, 0}, {kHalfSize, kHalfSize, 0}, {-kHalfSize, kHalfSize, 0}}},
     kSquarePairs,
     {1.20, 2.05, 0},
     kStartAngle,
     0.70,
     0.55},
    // The right isosceles triangle: two legs three cubes long, the right angle
    // standing on the plane so it is held up by two corners and its mass sits
    // between them.
    {3,
     3,
     {{{-kTriangleLeg * kThird, -kTriangleLeg * kThird, 0},
       {kTriangleLeg * 2.0 * kThird, -kTriangleLeg * kThird, 0},
       {-kTriangleLeg * kThird, kTriangleLeg * 2.0 * kThird, 0}}},
     kTrianglePairs,
     {-1.90, kTriangleLeg * kThird, 0},
     0.0,
     0.70,
     0.55},
}};

// Which way round a body is, from the direction of its first edge.
inline double edgeAngle(const Vec3* corners) {
    const Vec3 side = corners[1] - corners[0];
    return std::atan2(side.y, side.x);
}

// The middle of a body's corners. A body is only its corners, so this is worked
// out from them rather than stored and kept in step.
inline Vec3 centroid(const Vec3* corners, std::size_t count) {
    Vec3 sum{};
    for (std::size_t i = 0; i < count; ++i) sum += corners[i];
    return sum / static_cast<double>(count);
}

// Where a segment first comes within radius of a circle, and the normal there.
// The ball is one point with a radius, so every test against it is this one.
inline bool segmentHitsCircle(Vec3 from, Vec3 to, Vec3 centre, double radius, double& along, Vec3& normal) {
    const Vec3 travel = to - from;
    const Vec3 offset = from - centre;
    const double a = dot(travel, travel);
    if (!(a > 1e-18)) return false;
    const double b = 2.0 * dot(offset, travel);
    const double c = dot(offset, offset) - radius * radius;
    if (c <= 0.0) {
        // Already overlapping: report it at once, so it is pushed back out.
        const double distance = std::sqrt(dot(offset, offset));
        if (!(distance > 1e-12)) return false;
        along = 0.0;
        normal = offset / distance;
        return true;
    }
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) return false;
    const double root = (-b - std::sqrt(discriminant)) / (2.0 * a);
    if (root < 0.0 || root > 1.0) return false;
    along = root;
    const Vec3 hit = from + travel * root;
    const Vec3 gap = hit - centre;
    const double distance = std::sqrt(dot(gap, gap));
    normal = distance > 1e-12 ? gap / distance : Vec3{0, 1, 0};
    return true;
}

// Where a segment first meets a convex body, and the outward normal of the edge
// it meets. The corners are in order, so each edge can be tested on its own and
// the earliest crossing wins. A point already inside is reported as a hit at
// once, with the normal of the nearest edge.
inline bool segmentHitsPolygon(Vec3 from, Vec3 to, const Vec3* corners, std::size_t count, double& hit,
                               Vec3& normal) {
    const auto cross2 = [](Vec3 a, Vec3 b) { return a.x * b.y - a.y * b.x; };
    const auto edgeNormal = [&](std::size_t edge) {
        const Vec3 a = corners[edge];
        const Vec3 b = corners[(edge + 1) % count];
        const Vec3 side = b - a;
        const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
        return sideLength > 1e-12 ? Vec3{side.y / sideLength, -side.x / sideLength, 0} : Vec3{};
    };
    // A segment walking from outside must cross an edge; the earliest crossing is
    // the hit. Crossing out of the body is not a hit.
    const Vec3 travel = to - from;
    double earliest = 2.0;
    std::size_t crossing = count;
    for (std::size_t edge = 0; edge < count; ++edge) {
        const Vec3 a = corners[edge];
        const Vec3 b = corners[(edge + 1) % count];
        const Vec3 side = b - a;
        const double denominator = cross2(travel, side);
        if (std::abs(denominator) < 1e-15) continue;
        const Vec3 gap = a - from;
        const double along = cross2(gap, side) / denominator;
        const double across = cross2(gap, travel) / denominator;
        if (along < 0.0 || along > 1.0 || across < 0.0 || across > 1.0) continue;
        // Only a crossing that goes from outside to inside counts.
        const Vec3 outward = edgeNormal(edge);
        if (!(dot(travel, outward) < 0.0)) continue;
        if (along < earliest) { earliest = along; crossing = edge; }
    }
    if (crossing < count) {
        hit = earliest;
        normal = edgeNormal(crossing);
        return true;
    }
    // Already inside: every edge must report it as behind itself. One edge
    // behind itself is not enough -- that is just a point somewhere outside.
    bool inside = true;
    std::size_t nearest = 0;
    double nearestDistance = -1e30;
    for (std::size_t edge = 0; edge < count; ++edge) {
        const Vec3 outward = edgeNormal(edge);
        if (length(outward) < 0.5) return false;
        const double distance = dot(from - corners[edge], outward);
        if (distance > 0.0) inside = false;
        if (distance > nearestDistance) { nearestDistance = distance; nearest = edge; }
    }
    if (!inside) return false;
    hit = 0.0;
    normal = edgeNormal(nearest);
    return true;
}

// A point found inside a body is put back onto the boundary, at the closest
// point of it. Nudging it out along an edge normal by a hair is not enough --
// a point that is a centimetre deep stays a centimetre deep, gets nudged again
// next tick, and creeps instead of sliding. Returns true when it had to move.
// How far two convex outlines are into each other, and the shortest way out.
//
// Separating axes: two convex outlines are apart if they are apart along any one of
// their own sides, and the smallest overlap of all of them is the shortest distance
// that puts them apart. `way` points from the first outline towards the second, the
// direction the second one has to move. False when they do not overlap at all.
//
// This is the only correction that keeps a pair of bodies out of each other by
// construction: pushing a corner out of an outline that it is deep inside takes the
// corner out along the nearest side, which for a body sitting on another body's
// point is a side that sends it *through* -- and a pair that is put back out the
// wrong way is a pair that was never separated at all.
inline bool leastOverlap(const Vec3* one, std::size_t oneCount, const Vec3* two, std::size_t twoCount, Vec3& way,
                         double& amount) {
    double least = 1e30;
    Vec3 axis{0, 1, 0};
    for (int side = 0; side < 2; ++side) {
        const Vec3* corners = side == 0 ? one : two;
        const std::size_t count = side == 0 ? oneCount : twoCount;
        for (std::size_t i = 0; i < count; ++i) {
            const Vec3 a = corners[i];
            const Vec3 b = corners[(i + 1) % count];
            const Vec3 edge = b - a;
            const double edgeLength = std::sqrt(edge.x * edge.x + edge.y * edge.y);
            if (!(edgeLength > 1e-12)) continue;
            const Vec3 outward{edge.y / edgeLength, -edge.x / edgeLength, 0};
            double oneLow = 1e30, oneHigh = -1e30, twoLow = 1e30, twoHigh = -1e30;
            for (std::size_t k = 0; k < oneCount; ++k) {
                const double at = dot(one[k], outward);
                oneLow = std::min(oneLow, at);
                oneHigh = std::max(oneHigh, at);
            }
            for (std::size_t k = 0; k < twoCount; ++k) {
                const double at = dot(two[k], outward);
                twoLow = std::min(twoLow, at);
                twoHigh = std::max(twoHigh, at);
            }
            const double overlap = std::min(oneHigh, twoHigh) - std::max(oneLow, twoLow);
            if (!(overlap > 0.0)) return false;  // apart along this axis: apart
            if (overlap < least) {
                least = overlap;
                // The way the second outline comes out: along this axis, away from
                // the first one's middle.
                axis = dot(outward, one[0] - two[0]) > 0.0 ? outward * -1.0 : outward;
            }
        }
    }
    if (!(least < 1e30)) return false;
    way = axis;
    amount = least;
    return true;
}

inline bool pushOutOfPolygon(Vec3& point, const Vec3* corners, std::size_t count) {
    bool inside = true;
    for (std::size_t edge = 0; edge < count; ++edge) {
        const Vec3 a = corners[edge];
        const Vec3 b = corners[(edge + 1) % count];
        const Vec3 side = b - a;
        const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
        if (!(sideLength > 1e-12)) return false;
        const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        if (dot(point - a, outward) > 0.0) { inside = false; break; }
    }
    if (!inside) return false;
    Vec3 closest = corners[0];
    double nearest = 1e30;
    for (std::size_t edge = 0; edge < count; ++edge) {
        const Vec3 a = corners[edge];
        const Vec3 b = corners[(edge + 1) % count];
        const Vec3 side = b - a;
        const double length2 = dot(side, side);
        const double along = length2 > 1e-12 ? std::clamp(dot(point - a, side) / length2, 0.0, 1.0) : 0.0;
        const Vec3 candidate = a + side * along;
        const double distance = length(candidate - point);
        if (distance < nearest) { nearest = distance; closest = candidate; }
    }
    point = closest;
    return true;
}

} // namespace gtc::native::playground_detail
