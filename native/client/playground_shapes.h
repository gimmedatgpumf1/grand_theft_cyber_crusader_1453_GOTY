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

} // namespace gtc::native::playground_detail
