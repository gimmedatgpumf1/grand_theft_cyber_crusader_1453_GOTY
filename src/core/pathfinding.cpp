#include "core/pathfinding.h"

#include "core/collision.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gtc {
namespace {

constexpr float kCellSize = 0.5F;
constexpr float kBodyRadius = 0.38F;

struct GridPoint {
    int x = 0;
    int y = 0;
};

} // namespace

Pathfinder::Pathfinder(const std::vector<Obstacle>& obstacles, float halfWidth, float halfDepth)
    : obstacles_(obstacles), halfWidth_(halfWidth), halfDepth_(halfDepth) {}

bool Pathfinder::walkable(const Vec2& point, float radius) const {
    return !circleHitsWorld(point, radius, obstacles_, halfWidth_, halfDepth_);
}

std::vector<Vec2> Pathfinder::findPath(const Vec2& start, const Vec2& goal) const {
    const int columns = static_cast<int>((halfWidth_ * 2.0F) / kCellSize);
    const int rows = static_cast<int>((halfDepth_ * 2.0F) / kCellSize);
    const int count = columns * rows;
    const auto indexOf = [columns](GridPoint point) { return point.y * columns + point.x; };
    const auto centerOf = [&](GridPoint point) {
        return Vec2{-halfWidth_ + (static_cast<float>(point.x) + 0.5F) * kCellSize,
                    -halfDepth_ + (static_cast<float>(point.y) + 0.5F) * kCellSize};
    };
    const auto nearestOpen = [&](const Vec2& position) {
        GridPoint base{
            std::clamp(static_cast<int>((position.x + halfWidth_) / kCellSize), 0, columns - 1),
            std::clamp(static_cast<int>((position.y + halfDepth_) / kCellSize), 0, rows - 1)};
        GridPoint best{-1, -1};
        float bestDistance = std::numeric_limits<float>::max();
        for (int radius = 0; radius <= 4 && best.x < 0; ++radius) {
            for (int y = base.y - radius; y <= base.y + radius; ++y) {
                for (int x = base.x - radius; x <= base.x + radius; ++x) {
                    if (x < 0 || y < 0 || x >= columns || y >= rows) {
                        continue;
                    }
                    const GridPoint candidate{x, y};
                    const Vec2 center = centerOf(candidate);
                    const float candidateDistance = distance(center, position);
                    if (candidateDistance < bestDistance && walkable(center, kBodyRadius)) {
                        best = candidate;
                        bestDistance = candidateDistance;
                    }
                }
            }
        }
        return best;
    };

    const GridPoint startNode = nearestOpen(start);
    const GridPoint goalNode = nearestOpen(goal);
    if (startNode.x < 0 || goalNode.x < 0) {
        return {};
    }

    const float infinity = std::numeric_limits<float>::infinity();
    std::vector<float> scores(static_cast<std::size_t>(count), infinity);
    std::vector<int> parents(static_cast<std::size_t>(count), -1);
    std::vector<bool> closed(static_cast<std::size_t>(count), false);
    const int startIndex = indexOf(startNode);
    const int goalIndex = indexOf(goalNode);
    scores[static_cast<std::size_t>(startIndex)] = 0.0F;

    const auto heuristic = [](GridPoint a, GridPoint b) {
        const float dx = static_cast<float>(std::abs(a.x - b.x));
        const float dy = static_cast<float>(std::abs(a.y - b.y));
        return std::max(dx, dy) + 0.41421356F * std::min(dx, dy);
    };

    for (int iteration = 0; iteration < count; ++iteration) {
        int current = -1;
        float bestScore = infinity;
        for (int i = 0; i < count; ++i) {
            if (closed[static_cast<std::size_t>(i)] || scores[static_cast<std::size_t>(i)] == infinity) {
                continue;
            }
            const GridPoint point{i % columns, i / columns};
            const float estimate = scores[static_cast<std::size_t>(i)] + heuristic(point, goalNode);
            if (estimate < bestScore) {
                bestScore = estimate;
                current = i;
            }
        }
        if (current < 0 || current == goalIndex) {
            break;
        }
        closed[static_cast<std::size_t>(current)] = true;
        const GridPoint currentPoint{current % columns, current / columns};
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oy == 0) {
                    continue;
                }
                const GridPoint next{currentPoint.x + ox, currentPoint.y + oy};
                if (next.x < 0 || next.y < 0 || next.x >= columns || next.y >= rows ||
                    !walkable(centerOf(next), kBodyRadius)) {
                    continue;
                }
                if (ox != 0 && oy != 0 &&
                    (!walkable(centerOf({currentPoint.x + ox, currentPoint.y}), kBodyRadius) ||
                     !walkable(centerOf({currentPoint.x, currentPoint.y + oy}), kBodyRadius))) {
                    continue;
                }
                const int nextIndex = indexOf(next);
                const float stepCost = (ox != 0 && oy != 0) ? 1.41421356F : 1.0F;
                const float newScore = scores[static_cast<std::size_t>(current)] + stepCost;
                if (newScore < scores[static_cast<std::size_t>(nextIndex)]) {
                    scores[static_cast<std::size_t>(nextIndex)] = newScore;
                    parents[static_cast<std::size_t>(nextIndex)] = current;
                }
            }
        }
    }

    if (goalIndex != startIndex && parents[static_cast<std::size_t>(goalIndex)] < 0) {
        return {};
    }
    std::vector<Vec2> reversed;
    for (int current = goalIndex; current >= 0 && current != startIndex;
         current = parents[static_cast<std::size_t>(current)]) {
        reversed.push_back(centerOf({current % columns, current / columns}));
    }
    std::reverse(reversed.begin(), reversed.end());
    if (walkable(goal, kBodyRadius)) {
        reversed.push_back(goal);
    }

    std::vector<Vec2> simplified;
    Vec2 anchor = start;
    std::size_t index = 0;
    while (index < reversed.size()) {
        std::size_t furthest = index;
        for (std::size_t candidate = reversed.size(); candidate > index + 1; --candidate) {
            if (lineIsWalkable(anchor, reversed[candidate - 1], kBodyRadius,
                               obstacles_, halfWidth_, halfDepth_)) {
                furthest = candidate - 1;
                break;
            }
        }
        simplified.push_back(reversed[furthest]);
        anchor = reversed[furthest];
        index = furthest + 1;
    }
    return simplified;
}

} // namespace gtc
