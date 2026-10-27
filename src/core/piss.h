#pragma once

#include "core/collision.h"

namespace gtc {

struct PissImpact {
    bool hit = false;
    float time = kPissDuration;
    Puddle puddle;
};

namespace piss_detail {
struct Point {
    float x = 0, y = 0, z = 0;
};
inline Point interpolate(Point a, Point b, float amount) {
    return {a.x + (b.x - a.x) * amount, a.y + (b.y - a.y) * amount, a.z + (b.z - a.z) * amount};
}
inline bool inside(Point point, const Obstacle& obstacle) {
    return point.x >= obstacle.center.x - obstacle.halfSize.x && point.x <= obstacle.center.x + obstacle.halfSize.x &&
        point.z >= obstacle.center.y - obstacle.halfSize.y && point.z <= obstacle.center.y + obstacle.halfSize.y &&
        point.y >= obstacle.base && point.y <= obstacle.height;
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
        if (!puddle.vertical) {
            if (std::abs(puddle.height - obstacle.height - 0.012F) < 0.05F &&
                puddle.position.x >= minX && puddle.position.x <= maxX &&
                puddle.position.y >= minZ && puddle.position.y <= maxZ) return true;
            continue;
        }
        if (puddle.height < obstacle.base || puddle.height > obstacle.height) continue;
        const bool xSide = std::abs(puddle.normal.x) > 0.5F;
        const float expectedX = puddle.normal.x > 0.5F ? maxX + 0.008F : minX - 0.008F;
        const float expectedZ = puddle.normal.y > 0.5F ? maxZ + 0.008F : minZ - 0.008F;
        if (xSide && std::abs(puddle.position.x - expectedX) < 0.035F &&
            puddle.position.y >= minZ && puddle.position.y <= maxZ) return true;
        if (!xSide && std::abs(puddle.position.y - expectedZ) < 0.035F &&
            puddle.position.x >= minX && puddle.position.x <= maxX) return true;
    }
    return false;
}
} // namespace piss_detail

inline PissImpact tracePiss(const Character& character, const std::vector<Obstacle>& obstacles,
                            float originHeight = 0.77F, Vec2 originOffset = {}) {
    using piss_detail::Point;
    PissImpact result;
    const float pitch = character.unzipped && character.toiletAction == ToiletAction::None ? character.pitch : 0.0F;
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float sy = std::sin(character.yaw), cy = std::cos(character.yaw);
    const Point origin{character.position.x + originOffset.x, originHeight, character.position.y + originOffset.y};
    auto at = [&](float time) {
        const float distance = kPissSpeed * time;
        return Point{origin.x + sy * cp * distance, origin.y + sp * distance - kPissGravity * time * time,
                     origin.z + cy * cp * distance};
    };
    Point previous = origin;
    for (int step = 1; step <= 45; ++step) {
        const float time = kPissDuration * static_cast<float>(step) / 45.0F;
        const Point current = at(time);
        for (const Obstacle& obstacle : obstacles) {
            if (!piss_detail::inside(current, obstacle)) continue;
            result.hit = true;
            result.time = time;
            result.puddle.size = 0.11F;
            if (previous.y > obstacle.height) {
                result.puddle.position = {current.x, current.z};
                result.puddle.height = obstacle.height + 0.012F;
                return result;
            }
            const float minX = obstacle.center.x - obstacle.halfSize.x, maxX = obstacle.center.x + obstacle.halfSize.x;
            const float minZ = obstacle.center.y - obstacle.halfSize.y, maxZ = obstacle.center.y + obstacle.halfSize.y;
            result.puddle.position = {current.x, current.z};
            result.puddle.height = current.y;
            result.puddle.vertical = true;
            if (previous.x < minX) { result.puddle.position.x = minX - 0.008F; result.puddle.normal = {-1, 0}; }
            else if (previous.x > maxX) { result.puddle.position.x = maxX + 0.008F; result.puddle.normal = {1, 0}; }
            else if (previous.z < minZ) { result.puddle.position.y = minZ - 0.008F; result.puddle.normal = {0, -1}; }
            else if (previous.z > maxZ) { result.puddle.position.y = maxZ + 0.008F; result.puddle.normal = {0, 1}; }
            else {
                const float dx = std::min(std::abs(current.x - minX), std::abs(maxX - current.x));
                const float dz = std::min(std::abs(current.z - minZ), std::abs(maxZ - current.z));
                if (dx < dz) {
                    const bool left = std::abs(current.x - minX) < std::abs(maxX - current.x);
                    result.puddle.position.x = left ? minX - 0.008F : maxX + 0.008F;
                    result.puddle.normal = {left ? -1.0F : 1.0F, 0};
                } else {
                    const bool back = std::abs(current.z - minZ) < std::abs(maxZ - current.z);
                    result.puddle.position.y = back ? minZ - 0.008F : maxZ + 0.008F;
                    result.puddle.normal = {0, back ? -1.0F : 1.0F};
                }
            }
            return result;
        }
        if (current.y <= 0.018F && previous.y > 0.018F) {
            const float amount = (previous.y - 0.018F) / (previous.y - current.y);
            const Point impact = piss_detail::interpolate(previous, current, amount);
            result.hit = true;
            result.time = time;
            result.puddle.position = {impact.x, impact.z};
            result.puddle.height = 0.018F;
            result.puddle.size = 0.11F;
            return result;
        }
        previous = current;
    }
    return result;
}

inline Puddle placePuddle(const std::vector<Puddle>& existing, Puddle candidate, std::uint32_t id,
                          const std::vector<Obstacle>& obstacles, float halfWidth, float halfDepth) {
    candidate.id = id;
    candidate.size = 0.09F + static_cast<float>(id % 4U) * 0.015F;
    for (int attempt = 0; attempt < 18; ++attempt) {
        const Puddle* overlap = nullptr;
        for (const Puddle& other : existing) if (piss_detail::overlaps(candidate, other)) { overlap = &other; break; }
        if (!overlap && piss_detail::supported(candidate, obstacles, halfWidth, halfDepth)) return candidate;
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
