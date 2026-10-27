#include "core/collision.h"

#include <algorithm>
#include <cmath>

namespace gtc {

bool circleHitsObstacle(const Vec2& point, float radius, const Obstacle& obstacle) {
    const float nearestX = std::max(obstacle.center.x - obstacle.halfSize.x,
                                    std::min(point.x, obstacle.center.x + obstacle.halfSize.x));
    const float nearestZ = std::max(obstacle.center.y - obstacle.halfSize.y,
                                    std::min(point.y, obstacle.center.y + obstacle.halfSize.y));
    const float dx = point.x - nearestX;
    const float dz = point.y - nearestZ;
    return dx * dx + dz * dz < radius * radius;
}

bool bodyHitsObstacle(const Vec2& point, float radius, float feet, float bodyHeight,
                      const Obstacle& obstacle) {
    if (feet >= obstacle.height - 0.0001F || feet + bodyHeight <= obstacle.base + 0.0001F) return false;
    return circleHitsObstacle(point, radius, obstacle);
}

bool bodyHitsWorld(const Vec2& point, float radius, float feet, float bodyHeight,
                   const std::vector<Obstacle>& obstacles, float halfWidth, float halfDepth) {
    if (point.x - radius < -halfWidth || point.x + radius > halfWidth ||
        point.y - radius < -halfDepth || point.y + radius > halfDepth) return true;
    for (const Obstacle& obstacle : obstacles) {
        if (obstacle.kind != "boundary" && bodyHitsObstacle(point, radius, feet, bodyHeight, obstacle)) return true;
    }
    return false;
}

bool circleHitsWorld(const Vec2& point, float radius,
                     const std::vector<Obstacle>& obstacles,
                     float halfWidth, float halfDepth) {
    if (point.x - radius < -halfWidth || point.x + radius > halfWidth ||
        point.y - radius < -halfDepth || point.y + radius > halfDepth) {
        return true;
    }
    for (const Obstacle& obstacle : obstacles) {
        if (obstacle.kind != "boundary" && circleHitsObstacle(point, radius, obstacle)) {
            return true;
        }
    }
    return false;
}

Vec2 moveWithCollisions(const Vec2& start, const Vec2& delta, float radius,
                        const std::vector<Obstacle>& obstacles,
                        float halfWidth, float halfDepth) {
    Vec2 result = start;
    const float magnitude = length(delta);
    const int steps = std::max(1, static_cast<int>(std::ceil(magnitude / 0.18F)));
    const Vec2 step = delta * (1.0F / static_cast<float>(steps));
    for (int i = 0; i < steps; ++i) {
        Vec2 horizontal = result;
        horizontal.x += step.x;
        if (!circleHitsWorld(horizontal, radius, obstacles, halfWidth, halfDepth)) {
            result.x = horizontal.x;
        }
        Vec2 vertical = result;
        vertical.y += step.y;
        if (!circleHitsWorld(vertical, radius, obstacles, halfWidth, halfDepth)) {
            result.y = vertical.y;
        }
    }
    return result;
}

bool lineIsWalkable(const Vec2& start, const Vec2& end, float radius,
                    const std::vector<Obstacle>& obstacles,
                    float halfWidth, float halfDepth) {
    const float lengthToCheck = distance(start, end);
    const int steps = std::max(1, static_cast<int>(std::ceil(lengthToCheck / 0.2F)));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Vec2 point = start + (end - start) * t;
        if (circleHitsWorld(point, radius, obstacles, halfWidth, halfDepth)) {
            return false;
        }
    }
    return true;
}

} // namespace gtc
