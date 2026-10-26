#pragma once

#include "core/types.h"

#include <vector>

namespace gtc {

bool circleHitsObstacle(const Vec2& point, float radius, const Obstacle& obstacle);
bool circleHitsWorld(const Vec2& point, float radius,
                     const std::vector<Obstacle>& obstacles,
                     float halfWidth, float halfDepth);
Vec2 moveWithCollisions(const Vec2& start, const Vec2& delta, float radius,
                        const std::vector<Obstacle>& obstacles,
                        float halfWidth, float halfDepth);
bool lineIsWalkable(const Vec2& start, const Vec2& end, float radius,
                    const std::vector<Obstacle>& obstacles,
                    float halfWidth, float halfDepth);

} // namespace gtc
