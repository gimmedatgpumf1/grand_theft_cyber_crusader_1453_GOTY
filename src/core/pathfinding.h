#pragma once

#include "core/types.h"

#include <vector>

namespace gtc {

class Pathfinder {
public:
    Pathfinder(const std::vector<Obstacle>& obstacles, float halfWidth, float halfDepth);

    std::vector<Vec2> findPath(const Vec2& start, const Vec2& goal) const;
    bool walkable(const Vec2& point, float radius = 0.38F) const;

private:
    const std::vector<Obstacle>& obstacles_;
    float halfWidth_;
    float halfDepth_;
};

} // namespace gtc
