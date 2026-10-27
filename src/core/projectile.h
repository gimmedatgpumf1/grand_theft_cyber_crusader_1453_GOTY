#pragma once

#include "core/collision.h"

#include <algorithm>
#include <cmath>

namespace gtc {
namespace projectile_detail {

struct Point {
    float x = 0, y = 0, z = 0;
};
inline Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Point operator*(Point a, float scale) { return {a.x * scale, a.y * scale, a.z * scale}; }
inline Point point(const Projectile& projectile) {
    return {projectile.position.x, projectile.height, projectile.position.y};
}
inline Vec2 normalized2(Vec2 value) {
    const float magnitude = length(value);
    return magnitude > 0.00001F ? value * (1.0F / magnitude) : Vec2{0, 1};
}

struct SurfaceHit {
    bool hit = false;
    bool absorb = false;
    bool toilet = false;
    float amount = 1.0F;
    Puddle puddle;
};
inline void consider(SurfaceHit& best, const SurfaceHit& candidate) {
    if (candidate.hit && (!best.hit || candidate.amount < best.amount)) best = candidate;
}
inline SurfaceHit floorHit(Point a, Point b) {
    SurfaceHit result;
    if (a.y <= 0.018F || b.y > 0.018F) return result;
    const float amount = (a.y - 0.018F) / (a.y - b.y);
    const Point at = a + (b - a) * amount;
    result.hit = true;
    result.amount = amount;
    result.puddle.position = {at.x, at.z};
    result.puddle.height = 0.018F;
    return result;
}
inline SurfaceHit aabbHit(Point a, Point b, float radius, const Obstacle& obstacle) {
    SurfaceHit result;
    const Point minimum{obstacle.center.x - obstacle.halfSize.x - radius, obstacle.base - radius,
                        obstacle.center.y - obstacle.halfSize.y - radius};
    const Point maximum{obstacle.center.x + obstacle.halfSize.x + radius, obstacle.height + radius,
                        obstacle.center.y + obstacle.halfSize.y + radius};
    const auto inside = [&](Point point) {
        return point.x >= minimum.x && point.x <= maximum.x && point.y >= minimum.y && point.y <= maximum.y &&
            point.z >= minimum.z && point.z <= maximum.z;
    };
    if (inside(a)) return result;
    const Point delta = b - a;
    const float starts[3] = {a.x, a.y, a.z}, deltas[3] = {delta.x, delta.y, delta.z};
    const float lows[3] = {minimum.x, minimum.y, minimum.z}, highs[3] = {maximum.x, maximum.y, maximum.z};
    float enter = 0, leave = 1, sign = 0;
    int axis = -1;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(deltas[i]) < 0.000001F) {
            if (starts[i] < lows[i] || starts[i] > highs[i]) return result;
            continue;
        }
        float near = (lows[i] - starts[i]) / deltas[i], far = (highs[i] - starts[i]) / deltas[i];
        float nearSign = -1.0F;
        if (near > far) { std::swap(near, far); nearSign = 1.0F; }
        if (near > enter) { enter = near; axis = i; sign = nearSign; }
        leave = std::min(leave, far);
        if (enter > leave) return result;
    }
    if (axis < 0 || enter < 0 || enter > 1) return result;
    const Point at = a + delta * enter;
    result.hit = true;
    result.amount = enter;
    result.puddle.position = {at.x, at.z};
    result.puddle.height = at.y;
    if (axis == 1) {
        result.absorb = sign < 0;
        if (!result.absorb) {
            result.puddle.height = obstacle.height + 0.008F;
            result.puddle.position = {at.x, at.z};
        }
        return result;
    }
    result.puddle.vertical = true;
    if (axis == 0) {
        result.puddle.normal = {sign, 0};
        result.puddle.position.x = (sign > 0 ? obstacle.center.x + obstacle.halfSize.x :
            obstacle.center.x - obstacle.halfSize.x) + sign * 0.008F;
    } else {
        result.puddle.normal = {0, sign};
        result.puddle.position.y = (sign > 0 ? obstacle.center.y + obstacle.halfSize.y :
            obstacle.center.y - obstacle.halfSize.y) + sign * 0.008F;
    }
    return result;
}
inline SurfaceHit planeHit(Point a, Point b, float height, const Obstacle& toilet, bool water) {
    SurfaceHit result;
    if (a.y <= height || b.y > height) return result;
    const float amount = (a.y - height) / (a.y - b.y);
    const Point at = a + (b - a) * amount;
    const float x = at.x - toilet.center.x, z = at.z - toilet.center.y - 0.06F;
    const float outer = x * x / (0.1905F * 0.1905F) + z * z / (0.270F * 0.270F);
    const float inner = x * x / (0.145F * 0.145F) + z * z / (0.205F * 0.205F);
    if (outer > 1.0F || (!water && inner < 1.0F)) return result;
    result.hit = result.absorb = water;
    result.amount = amount;
    if (!water) {
        result.puddle.position = {at.x, at.z};
        result.puddle.height = height + 0.008F;
    }
    return result;
}
inline SurfaceHit toiletHit(Point a, Point b, float radius, const Obstacle& toilet) {
    SurfaceHit best;
    const Obstacle tank{{toilet.center.x, toilet.center.y - 0.2445F}, {0.2285F, 0.108F},
                        kToiletTankHeight, "toilet-tank", 0.32F};
    const Obstacle pedestal{{toilet.center.x, toilet.center.y + 0.02F}, {0.12F, 0.15F}, 0.20F, "toilet-base"};
    consider(best, aabbHit(a, b, radius, tank));
    consider(best, aabbHit(a, b, radius, pedestal));
    consider(best, planeHit(a, b, kToiletSeatHeight, toilet, false));
    consider(best, planeHit(a, b, kToiletWaterHeight, toilet, true));
    const float cx = toilet.center.x, cz = toilet.center.y + 0.06F;
    const float rx = 0.1905F, rz = 0.270F;
    const float ox = a.x - cx, oz = a.z - cz, dx = b.x - a.x, dz = b.z - a.z;
    const float qa = dx * dx / (rx * rx) + dz * dz / (rz * rz);
    const float qb = 2.0F * (ox * dx / (rx * rx) + oz * dz / (rz * rz));
    const float qc = ox * ox / (rx * rx) + oz * oz / (rz * rz) - 1.0F;
    const float discriminant = qb * qb - 4.0F * qa * qc;
    if (qc > 0.0F && qa > 0.0000001F && discriminant >= 0.0F) {
        const float amount = (-qb - std::sqrt(discriminant)) / (2.0F * qa);
        const float y = a.y + (b.y - a.y) * amount;
        if (amount >= 0.0F && amount <= 1.0F && y >= 0.08F && y <= kToiletSeatHeight) {
            const Point at = a + (b - a) * amount;
            const Vec2 normal = normalized2({(at.x - cx) / (rx * rx), (at.z - cz) / (rz * rz)});
            SurfaceHit side;
            side.hit = true;
            side.amount = amount;
            side.puddle.position = {at.x + normal.x * 0.008F, at.z + normal.y * 0.008F};
            side.puddle.height = y;
            side.puddle.normal = normal;
            side.puddle.vertical = true;
            consider(best, side);
        }
    }
    if (best.hit) best.toilet = true;
    return best;
}
inline SurfaceHit sweepSurfaces(Point a, Point b, float radius, const std::vector<Obstacle>& obstacles) {
    SurfaceHit best = floorHit(a, b);
    for (const Obstacle& obstacle : obstacles) {
        consider(best, obstacle.kind == "toilet" ? toiletHit(a, b, radius, obstacle) :
            aabbHit(a, b, radius, obstacle));
    }
    return best;
}
inline bool sweepCharacter(Point a, Point b, float radius, const Character& character, float& amount) {
    const float cx = character.position.x, cz = character.position.y;
    const float combined = 0.32F + radius;
    const float ox = a.x - cx, oz = a.z - cz, dx = b.x - a.x, dz = b.z - a.z;
    const float qa = dx * dx + dz * dz, qb = 2.0F * (ox * dx + oz * dz);
    const float qc = ox * ox + oz * oz - combined * combined;
    const float low = character.elevation;
    const float high = low + (character.prone ? kProneHeight : character.crouched ? kCrouchingHeight : kStandingHeight);
    auto valid = [&](float time) {
        if (time < 0.0F || time > 1.0F) return false;
        const float y = a.y + (b.y - a.y) * time;
        return y >= low && y <= high;
    };
    if (qc <= 0.0F && valid(0.0F)) { amount = 0.0F; return true; }
    const float discriminant = qb * qb - 4.0F * qa * qc;
    if (qa > 0.0000001F && discriminant >= 0.0F) {
        const float root = std::sqrt(discriminant);
        const float enter = (-qb - root) / (2.0F * qa);
        if (valid(enter)) { amount = enter; return true; }
    }
    if (std::abs(b.y - a.y) > 0.000001F) {
        for (float y : {low, high}) {
            const float time = (y - a.y) / (b.y - a.y);
            const float x = ox + dx * time, z = oz + dz * time;
            if (valid(time) && x * x + z * z <= combined * combined) { amount = time; return true; }
        }
    }
    return false;
}

inline bool overlaps(const Puddle& a, const Puddle& b) {
    if (a.vertical != b.vertical) return false;
    const float radius = a.size + b.size + 0.025F;
    if (a.vertical) {
        if (length(a.normal - b.normal) > 0.1F) return false;
        const float planar = distance(a.position, b.position), dy = a.height - b.height;
        return planar * planar + dy * dy < radius * radius;
    }
    return std::abs(a.height - b.height) < 0.05F && distance(a.position, b.position) < radius;
}
inline bool supported(const Puddle& puddle, const std::vector<Obstacle>& obstacles, float halfWidth, float halfDepth) {
    if (std::abs(puddle.position.x) > halfWidth - 0.02F || std::abs(puddle.position.y) > halfDepth - 0.02F) return false;
    if (!puddle.vertical && puddle.height < 0.05F) {
        return !circleHitsWorld(puddle.position, 0.015F, obstacles, halfWidth, halfDepth);
    }
    for (const Obstacle& obstacle : obstacles) {
        const float minX = obstacle.center.x - obstacle.halfSize.x, maxX = obstacle.center.x + obstacle.halfSize.x;
        const float minZ = obstacle.center.y - obstacle.halfSize.y, maxZ = obstacle.center.y + obstacle.halfSize.y;
        if (obstacle.kind == "toilet") {
            if (puddle.position.x < minX - 0.06F || puddle.position.x > maxX + 0.06F ||
                puddle.position.y < minZ - 0.06F || puddle.position.y > maxZ + 0.06F) continue;
            if (puddle.vertical && puddle.height >= 0.0F && puddle.height <= kToiletTankHeight + 0.02F) return true;
            if (!puddle.vertical && (std::abs(puddle.height - kToiletSeatHeight - 0.008F) < 0.04F ||
                std::abs(puddle.height - kToiletTankHeight - 0.008F) < 0.04F)) return true;
            continue;
        }
        if (!puddle.vertical) {
            if (std::abs(puddle.height - obstacleHeightAt(obstacle, puddle.position) - 0.008F) < 0.05F &&
                puddle.position.x >= minX && puddle.position.x <= maxX &&
                puddle.position.y >= minZ && puddle.position.y <= maxZ) return true;
            continue;
        }
        if (puddle.height < obstacle.base || puddle.height > obstacle.height) continue;
        const bool xSide = std::abs(puddle.normal.x) > 0.5F;
        const float expectedX = puddle.normal.x > 0.0F ? maxX + 0.008F : minX - 0.008F;
        const float expectedZ = puddle.normal.y > 0.0F ? maxZ + 0.008F : minZ - 0.008F;
        if (xSide && std::abs(puddle.position.x - expectedX) < 0.035F &&
            puddle.position.y >= minZ && puddle.position.y <= maxZ) return true;
        if (!xSide && std::abs(puddle.position.y - expectedZ) < 0.035F &&
            puddle.position.x >= minX && puddle.position.x <= maxX) return true;
    }
    return false;
}
} // namespace projectile_detail

inline Vec2 windAt(double elapsed) {
    return {static_cast<float>(0.65 + std::sin(elapsed * 0.09) * 0.35),
            static_cast<float>(-0.20 + std::cos(elapsed * 0.07) * 0.25)};
}
inline float ballisticPitch(float horizontal, float rise, float speed) {
    if (horizontal < 0.01F) return rise >= 0.0F ? 1.57079633F : -1.57079633F;
    const float v2 = speed * speed, gravity = kProjectileGravity;
    const float discriminant = v2 * v2 - gravity * (gravity * horizontal * horizontal + 2.0F * rise * v2);
    if (discriminant <= 0.0F) return 0.785398163F;
    return std::atan((v2 - std::sqrt(discriminant)) / (gravity * horizontal));
}

inline Puddle placePuddle(const std::vector<Puddle>& existing, Puddle candidate, std::uint32_t id,
                          const std::vector<Obstacle>& obstacles, float halfWidth, float halfDepth) {
    candidate.id = id;
    candidate.size = 0.09F + static_cast<float>(id % 4U) * 0.015F;
    for (int attempt = 0; attempt < 18; ++attempt) {
        const Puddle* overlap = nullptr;
        for (const Puddle& other : existing) if (projectile_detail::overlaps(candidate, other)) { overlap = &other; break; }
        if (!overlap && projectile_detail::supported(candidate, obstacles, halfWidth, halfDepth)) return candidate;
        if (!overlap) { candidate.id = 0; return candidate; }
        const float spacing = overlap->size + candidate.size + 0.035F;
        const float angle = static_cast<float>(id + attempt) * 2.39996323F;
        if (candidate.vertical) {
            const float side = ((id + static_cast<std::uint32_t>(attempt)) & 1U) ? 1.0F : -1.0F;
            const Vec2 tangent{-candidate.normal.y * side, candidate.normal.x * side};
            candidate.position = overlap->position + tangent * spacing;
            candidate.normal = overlap->normal;
        } else {
            candidate.position = overlap->position + Vec2{std::cos(angle), std::sin(angle)} * spacing;
            candidate.height = overlap->height;
        }
    }
    candidate.id = 0;
    return candidate;
}

} // namespace gtc
