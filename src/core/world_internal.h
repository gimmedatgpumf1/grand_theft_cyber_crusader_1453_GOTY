// The measurements the world rules are written in: how tall a character is,
// what it has to fit under, where its feet land on an obstacle, and what it can
// reach. They were in an anonymous namespace in world.cpp; the character step
// and the toilet step were moved out of that file and still need them, so they
// are inline here and every step of the simulation sees one copy.

#ifndef GTC_CORE_WORLD_INTERNAL_H
#define GTC_CORE_WORLD_INTERNAL_H

#include "core/collision.h"
#include "core/world.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gtc::world_detail {
constexpr float kBodyRadius = 0.36F;
constexpr float kGravity = 9.81F;
constexpr float kArmReachWithoutHands = 1.95F;
constexpr float kClimbSpeed = 1.1F;

inline float bodyHeight(const Character& character) {
    return character.prone ? kProneHeight : character.crouched ? kCrouchingHeight : kStandingHeight;
}

inline float overheadClearance(const Character& character, const Vec2& point,
                        const std::vector<Obstacle>& obstacles) {
    float clearance = 100.0F;
    for (const Obstacle& obstacle : obstacles) {
        if (obstacle.kind == "boundary" || obstacle.base <= character.elevation + 0.02F ||
            obstacle.height <= character.elevation || !circleHitsObstacle(point, kBodyRadius, obstacle)) continue;
        clearance = std::min(clearance, obstacle.base - character.elevation);
    }
    return clearance;
}

inline void applyStance(Character& character, const Vec2& point, const std::vector<Obstacle>& obstacles) {
    const float clearance = overheadClearance(character, point, obstacles);
    // Low ceilings only preserve a stance the player already chose. Merely
    // walking near a tunnel must never initiate a crouch or lie-down action.
    const bool keepProne = character.prone && clearance < kStandingHeight - 0.02F;
    const bool keepCrouched = character.crouched && clearance < kStandingHeight - 0.02F;
    const bool advanced = character.pathAdvanced && !character.path.empty() && !character.grabbing;
    const bool advancedProne = advanced && clearance < kCrouchingHeight - 0.02F;
    const bool advancedCrouch = advanced && clearance < kStandingHeight - 0.02F;
    character.prone = keepProne || advancedProne ||
        (character.grounded && character.crouchInput && character.grabInput && !character.grabbing);
    character.crouched = !character.prone && (character.crouchInput || keepCrouched || advancedCrouch);
}

inline float supportHeight(const Character& character, const std::vector<Obstacle>& obstacles) {
    float support = 0.0F;
    for (const Obstacle& obstacle : obstacles) {
        const float top = obstacleHeightAt(obstacle, character.position);
        if (obstacle.kind != "boundary" && top <= character.elevation + 0.06F &&
            circleHitsObstacle(character.position, kBodyRadius, obstacle)) {
            support = std::max(support, top);
        }
    }
    return support;
}

inline int grabTarget(const Character& character, const std::vector<Obstacle>& obstacles) {
    const Vec2 forward{std::sin(character.yaw), std::cos(character.yaw)};
    int best = -1;
    float bestScore = 1000.0F;
    for (std::size_t i = 0; i < obstacles.size(); ++i) {
        const Obstacle& obstacle = obstacles[i];
        if (obstacle.kind == "boundary") continue;
        const Vec2 nearest{
            std::clamp(character.position.x, obstacle.center.x - obstacle.halfSize.x,
                       obstacle.center.x + obstacle.halfSize.x),
            std::clamp(character.position.y, obstacle.center.y - obstacle.halfSize.y,
                       obstacle.center.y + obstacle.halfSize.y)};
        const float top = obstacleHeightAt(obstacle, nearest);
        if (top <= character.elevation + kAutomaticStepHeight ||
            top > character.elevation + kArmReachWithoutHands + 0.05F) continue;
        const Vec2 delta = nearest - character.position;
        const float range = length(delta);
        if (range < 0.001F || range > kBodyRadius + 0.75F) continue;
        const float facing = (delta.x * forward.x + delta.y * forward.y) / range;
        if (facing < 0.45F) continue;
        const float score = range - facing * 0.2F;
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

inline const char* actionName(ToiletAction action) {
    switch (action) {
        case ToiletAction::Sit: return "sit";
        case ToiletAction::Shit: return "shit";
        case ToiletAction::Piss: return "piss";
        default: return "none";
    }
}

inline Appearance cleanAppearance(Appearance appearance) {
    appearance.skin = std::clamp(appearance.skin, 0, 5);
    appearance.hair = std::clamp(appearance.hair, 0, 15);
    appearance.hairColor = std::clamp(appearance.hairColor, 0, 8);
    appearance.shirt = std::clamp(appearance.shirt, 0, 6);
    appearance.pants = std::clamp(appearance.pants, 0, 3);
    appearance.beard = std::clamp(appearance.beard, 0, 2);
    appearance.eyeColor = std::clamp(appearance.eyeColor, 0, 5);
    appearance.makeup = std::clamp(appearance.makeup, 0, 4);
    appearance.lipColor = std::clamp(appearance.lipColor, 0, 4);
    appearance.tie = std::clamp(appearance.tie, 0, 5);
    for (std::size_t i = 0; i < appearance.morphs.size(); ++i) {
        appearance.morphs[i] = std::clamp(appearance.morphs[i], i < 3 ? 0 : -100, 100);
    }
    return appearance;
}

inline std::string cleanName(std::string name) {
    std::string cleaned;
    for (const unsigned char value : name) {
        if (value >= 0x20U && value != 0x7FU && cleaned.size() < 18) {
            cleaned.push_back(static_cast<char>(value));
        }
    }
    const std::size_t first = cleaned.find_first_not_of(" \t");
    if (first == std::string::npos) return "Unnamed Mistake";
    const std::size_t last = cleaned.find_last_not_of(" \t");
    return cleaned.substr(first, last - first + 1);
}

}  // namespace gtc::world_detail

#endif
