#pragma once

#include "core/pathfinding.h"
#include "core/types.h"

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace gtc {

class World {
public:
    World();

    std::uint32_t addCharacter(const std::string& name, bool ai);
    void removeCharacter(std::uint32_t id);
    void renameCharacter(std::uint32_t id, const std::string& name);
    void setInput(std::uint32_t id, const Vec2& move, bool sprint, bool piss,
                  float yaw, float pitch);
    void setDestination(std::uint32_t id, const Vec2& destination);
    void requestToilet(std::uint32_t id);
    void update(float deltaSeconds);

    Character character(std::uint32_t id) const;
    std::size_t characterCount() const;
    std::string snapshot() const;
    std::string description() const;

    static constexpr float kHalfWidth = 9.0F;
    static constexpr float kHalfDepth = 7.0F;
    static constexpr float kInteractionRange = 1.8F;

private:
    mutable std::mutex mutex_;
    std::map<std::uint32_t, Character> characters_;
    std::vector<Obstacle> obstacles_;
    Pathfinder pathfinder_;
    std::vector<Puddle> puddles_;
    std::deque<GameEvent> events_;
    std::uint32_t nextCharacterId_ = 1;
    std::uint32_t nextPuddleId_ = 1;
    std::uint32_t nextEventId_ = 1;
    std::uint32_t toiletUser_ = 0;
    std::uint64_t tick_ = 0;
    double elapsed_ = 0.0;
    std::mt19937 random_{1453};

    Vec2 toiletPosition() const { return {0.0F, -4.75F}; }
    Vec2 toiletSeat() const { return toiletPosition() + Vec2{0.0F, 0.45F}; }
    Vec2 toiletFront() const { return {0.0F, -2.85F}; }
    Vec2 spawnPoint(std::uint32_t id) const;
    Character* find(std::uint32_t id);
    void setDestinationLocked(Character& character, const Vec2& destination);
    void requestToiletLocked(Character& character);
    void beginToilet(Character& character);
    void updateCharacter(Character& character, float dt);
    void updateAI(Character& character, float dt);
    void updatePissing(Character& character, float dt);
    void addPuddle(const Vec2& position);
    void addEvent(const std::string& text);
    void notify(Character& character, const std::string& text);
};

} // namespace gtc
