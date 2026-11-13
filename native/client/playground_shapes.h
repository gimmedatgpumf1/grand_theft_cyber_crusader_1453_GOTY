#pragma once

// The drawing and the point-in-shape tests the playground room needs. They are
// here rather than in the room so that neither file grows past the size a file
// is allowed to be.

#include "client/playground_ball_shape.h"
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
// What counts as not moving: the room's resolution, a micrometre of travel in a
// tick. It is the same number everywhere else in the room (playground_room_tick.h,
// kQuantumSpeed) -- anything slower covers less than the room can resolve, so its
// speed is nothing.
inline constexpr double kRestSpeed = 2e-5;  // m/s = 1 micrometre per 20 Hz tick
// Air, for the solids and the dot. The same air the ball already falls through:
// half its density, the drag coefficient of a flat plate square to the flow, and
// the area the shape presents. A body is a plate one metre deep, so the piece of
// outline a corner owns is the area it presents.
inline constexpr double kPlateDragCoefficient = 1.28;
inline constexpr double kRoomDepth = 1.0;  // m, into the screen
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

// An arrow's head: a triangle whose base is at `base` and whose point is at `tip`,
// so the head lies along the force exactly as the shaft does. Its own geometry is
// a unit triangle -- base spanning y from -1 to 1 at x = 0, point at (1, 0) -- and
// this is the matrix that puts it there, the same way `segment` places a bar.
inline std::array<double, 16> arrowHead(Vec3 base, Vec3 tip, double halfWidth) {
    const Vec3 along = tip - base;
    const double span = std::sqrt(along.x * along.x + along.y * along.y);
    if (!(span > 1e-9)) return translation({0, -1000, 0});
    const Vec3 across{-along.y / span * halfWidth, along.x / span * halfWidth, 0};
    return {along.x, along.y, 0, 0, across.x, across.y, 0, 0, 0, 0, 1, 0,
            base.x, base.y, base.z, 1};
}

// The unit triangle that arrowHead places: base at local x = 0, point at x = 1.
inline void triangle(std::vector<Vec3>& points, std::vector<std::array<int, 3>>& faces, double z) {
    points.clear();
    faces.clear();
    points.push_back({0, -1, z});
    points.push_back({0, 1, z});
    points.push_back({1, 0, z});
    faces.push_back({0, 1, 2});
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


// What each body is when the room starts: its corners about its own centre of
// mass, where that centre is, and how far it is turned. There are no distances to
// keep here, because a body is rigid by construction.
struct Shape {
    std::size_t count = 0;
    std::array<Vec3, 4> local{};
    Vec3 middle{};
    double angle = 0;
    // This body's own pair of coefficients.
    double staticFriction = 0, kineticFriction = 0;
    // Whether this body is pinned at its middle and can only turn. A seesaw is.
    bool pivoted = false;
};

inline constexpr double kThird = 1.0 / 3.0;
// The seesaw's plank: half its length and half its thickness, where its axle stands,
// and how high the axle is. Its middle is the axle, so the pin is its centre of mass
// and its own weight gives it no turn at all.
inline constexpr double kSeesawHalf = 1.25;
inline constexpr double kSeesawHalfThick = 0.12;
inline constexpr double kSeesawX = -4.60;
inline constexpr double kSeesawPivotY = 0.85;
// The axle is a bearing and not a mathematical point. A plank on a pivot has nothing
// to slow it down -- its weight acts at the pin, so it makes no turn, and a turn it
// was given once would last for ever -- and a real one comes to rest because the pin
// it turns on is a steel pin in a bronze bush rubbing under the weight it carries.
// The couple is mu * N * r, the same law as the ball's rolling resistance and the
// same reason: a real contact, a real coefficient, and a force that opposes the turn
// and can only take it away. mu is dry bronze on steel; r is the pin's own radius.
inline constexpr double kAxleBearingFriction = 0.12;
inline constexpr double kAxleRadius = 0.02;
inline constexpr std::array<Shape, PlaygroundRoom::kBodyCount> kShapes{{
    // The square, turned so a corner leads: it lands on a corner and has to be
    // held up by the other three.
    {4,
     {{{-kHalfSize, -kHalfSize, 0}, {kHalfSize, -kHalfSize, 0}, {kHalfSize, kHalfSize, 0}, {-kHalfSize, kHalfSize, 0}}},
     {1.20, 2.05, 0},
     kStartAngle,
     0.70,
     0.55},
    // The right isosceles triangle: two legs three cubes long, the right angle
    // standing on the plane so it is held up by two corners and its mass sits
    // between them.
    {3,
     {{{-kTriangleLeg * kThird, -kTriangleLeg * kThird, 0},
       {kTriangleLeg * 2.0 * kThird, -kTriangleLeg * kThird, 0},
       {-kTriangleLeg * kThird, kTriangleLeg * 2.0 * kThird, 0}}},
     {-1.90, kTriangleLeg * kThird, 0},
     0.0,
     0.70,
     0.55},
    // The seesaw: a plank on an axle. Its corners are the plank about its own middle,
    // and the pin is at that middle, so it balances wherever it is left and only a
    // load on one end can turn it.
    //
    // Two and a half metres of plank, a quarter of a metre thick, four corners, so it
    // weighs what the square weighs and carries a moment worked out from its own
    // outline like everything else. It stands on an axle a little above the floor, on
    // the far side of the room from the shapes, where a ball rolled at it can be sent
    // up one end and come back down the other.
    {4,
     {{{-kSeesawHalf, -kSeesawHalfThick, 0},
       {kSeesawHalf, -kSeesawHalfThick, 0},
       {kSeesawHalf, kSeesawHalfThick, 0},
       {-kSeesawHalf, kSeesawHalfThick, 0}}},
     {kSeesawX, kSeesawPivotY, 0},
     0.0,
     0.70,
     0.55,
     true},
}};

// The moment of inertia of a uniform plate about its centre of mass, from its own
// outline. For a polygon with corners p_0..p_{n-1}, wound either way,
//
//     I = (rho/12) * sum |cross(p_i, p_i+1)| * (|p_i|^2 + dot(p_i, p_{i+1}) + |p_i+1|^2)
//
// with rho the mass per unit area. That is the exact value for the shape drawn on
// screen -- a square plate and a triangular one, not a ring of points -- and it is
// where the body's sluggishness or liveliness comes from, with nothing tuned.
inline double polygonMoment(const std::array<Vec3, 4>& local, std::size_t count, double mass) {
    double area = 0.0, integral = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 a = local[i];
        const Vec3 b = local[(i + 1) % count];
        const double cross = a.x * b.y - a.y * b.x;
        area += 0.5 * cross;
        integral += cross * (dot(a, a) + dot(a, b) + dot(b, b));
    }
    const double flat = std::abs(area);
    if (!(flat > 1e-12)) return mass;
    const double density = mass / flat;  // kg per square metre
    return std::max(1e-9, std::abs(density * integral / 12.0));
}

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
// Two convex outlines are apart if they are apart along any one of their own sides,
// and the smallest of those overlaps is the shortest distance that puts them apart.
// `way` points from the first outline towards the second: the direction the second
// one has to move. False when they are not overlapping at all.
//
// This is the only way to put a pair apart that holds for a pair that is deep in
// each other. Taking a corner out along the nearest side of the outline it is inside
// of is right only while the corner is near that side; for a body that has been
// carried into another one, the nearest side is a side that sends it *through*, and
// a pair put back out the wrong way was never separated at all -- which is a square
// that has gone into a triangle and stayed there.
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
            if (!(overlap > 0.0)) return false;  // apart along this side: apart
            if (overlap < least) {
                least = overlap;
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

// How far apart two convex outlines are, which way the first has to move to be
// further from the second, and the closest point of each.
//
// This is the one piece of geometry the bodies' walk needs that a single point
// does not: two shapes meeting is two outlines, and what decides when they meet is
// the distance between them and how fast that distance is closing. Both outlines
// are convex, so the distance is either a corner against a side or a corner
// against a corner, and the shortest one is the distance.
struct OutlinePairGap {
    double gap = 0.0;
    Vec3 normal{0, 1, 0};  // out of the second outline, towards the first
    Vec3 one{}, two{};     // the closest point of each, where they would touch
};

// The same question as an overlap: how deep, and which way out. False when they are
// apart along any side of either, which is what being apart means for two convex
// shapes.
inline bool outlineOverlap(const Vec3* one, std::size_t oneCount, const Vec3* two, std::size_t twoCount,
                           Vec3& outOfTwo, double& depth) {
    Vec3 oneMiddle{}, twoMiddle{};
    for (std::size_t i = 0; i < oneCount; ++i) oneMiddle = oneMiddle + one[i];
    for (std::size_t i = 0; i < twoCount; ++i) twoMiddle = twoMiddle + two[i];
    oneMiddle = oneMiddle * (1.0 / static_cast<double>(oneCount));
    twoMiddle = twoMiddle * (1.0 / static_cast<double>(twoCount));
    double least = 1e30;
    Vec3 best{0, 1, 0};
    for (int side = 0; side < 2; ++side) {
        const Vec3* corners = side == 0 ? one : two;
        const std::size_t count = side == 0 ? oneCount : twoCount;
        for (std::size_t i = 0; i < count; ++i) {
            const Vec3 a = corners[i];
            const Vec3 b = corners[(i + 1) % count];
            const Vec3 edge = b - a;
            const double edgeLength = std::sqrt(dot(edge, edge));
            if (!(edgeLength > 1e-12)) continue;
            const Vec3 axis{edge.y / edgeLength, -edge.x / edgeLength, 0};
            double oneLow = 1e30, oneHigh = -1e30, twoLow = 1e30, twoHigh = -1e30;
            for (std::size_t k = 0; k < oneCount; ++k) {
                const double at = dot(one[k], axis);
                oneLow = std::min(oneLow, at);
                oneHigh = std::max(oneHigh, at);
            }
            for (std::size_t k = 0; k < twoCount; ++k) {
                const double at = dot(two[k], axis);
                twoLow = std::min(twoLow, at);
                twoHigh = std::max(twoHigh, at);
            }
            const double overlap = std::min(oneHigh, twoHigh) - std::max(oneLow, twoLow);
            if (!(overlap > 0.0)) return false;  // apart along this side: apart
            if (overlap < least) {
                least = overlap;
                best = dot(oneMiddle - twoMiddle, axis) > 0.0 ? axis : axis * -1.0;
            }
        }
    }
    if (!(least < 1e30)) return false;
    outOfTwo = best;
    depth = least;
    return true;
}

inline OutlinePairGap pairGap(const Vec3* one, std::size_t oneCount, const Vec3* two, std::size_t twoCount) {
    OutlinePairGap result;
    Vec3 outOfTwo{0, 1, 0};
    double depth = 0.0;
    if (outlineOverlap(one, oneCount, two, twoCount, outOfTwo, depth)) {
        // Into each other. It cannot happen through the walk -- the step that would
        // have gone in is not taken -- but the answer is here for the arithmetic's
        // own sake: the shortest way out, and how far it is.
        result.gap = -depth;
        result.normal = outOfTwo;
        Vec3 oneMiddle{}, twoMiddle{};
        for (std::size_t i = 0; i < oneCount; ++i) oneMiddle = oneMiddle + one[i];
        for (std::size_t i = 0; i < twoCount; ++i) twoMiddle = twoMiddle + two[i];
        result.one = oneMiddle * (1.0 / static_cast<double>(oneCount));
        result.two = twoMiddle * (1.0 / static_cast<double>(twoCount));
        return result;
    }
    // Apart: the closest pair of pieces is either a corner of one against a side of
    // the other, or two corners, and the closest of them all is the distance
    // between the outlines.
    double nearest = 1e30;
    const auto consider = [&](Vec3 from, Vec3 to) {
        const double distance = length(from - to);
        if (!(distance < nearest)) return;
        nearest = distance;
        result.gap = distance;
        result.one = from;
        result.two = to;
        result.normal = distance > 1e-12 ? (from - to) * (1.0 / distance) : Vec3{0, 1, 0};
    };
    for (std::size_t i = 0; i < oneCount; ++i) {
        const ball_shape::OutlineTouch touch = ball_shape::outlineTouch(one[i], two, twoCount);
        consider(one[i], touch.point);
    }
    for (std::size_t i = 0; i < twoCount; ++i) {
        const ball_shape::OutlineTouch touch = ball_shape::outlineTouch(two[i], one, oneCount);
        consider(touch.point, two[i]);
    }
    if (!(nearest < 1e30)) {
        result.gap = 0.0;
        result.one = one[0];
        result.two = two[0];
    }
    return result;
}

} // namespace gtc::native::playground_detail
