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
    void customizeCharacter(std::uint32_t id, const Appearance& appearance);
    void setInput(std::uint32_t id, const Vec2& move, bool sprint, bool piss,
                  float yaw, float pitch, bool slowWalk = false, bool crouch = false,
                  bool grab = false, bool jump = false, bool block = false,
                  Vec2 aimTarget = {}, float aimHeight = 0.0F, bool hasAimTarget = false);
    void setDestination(std::uint32_t id, const Vec2& destination, bool sprint = false, bool advanced = false);
    void toggleZip(std::uint32_t id);
    void attack(std::uint32_t id, std::uint32_t targetId, bool advanced = false);
    void attackAt(std::uint32_t id, Vec2 target, float height);
    void requestToilet(std::uint32_t id, const std::string& action);
    void adjustNeed(std::uint32_t id, const std::string& need, float delta);
    void update(float deltaSeconds);

    Character character(std::uint32_t id) const;
    std::size_t characterCount() const;
    std::vector<Projectile> projectiles() const;
    std::vector<Puddle> puddles() const;
    std::vector<WaterRipple> waterRipples() const;
    double gameSeconds() const;
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
    std::vector<Projectile> projectiles_;
    std::vector<WaterRipple> waterRipples_;
    std::deque<GameEvent> events_;
    std::uint32_t nextCharacterId_ = 1;
    std::uint32_t nextPuddleId_ = 1;
    std::uint32_t nextProjectileId_ = 1;
    std::uint32_t nextEventId_ = 1;
    std::uint32_t toiletUser_ = 0;
    std::uint64_t tick_ = 0;
    double elapsed_ = 0.0;
    std::mt19937 random_{1453};

    Vec2 toiletPosition() const { return {0.0F, -4.75F}; }
    Vec2 toiletSeat() const { return toiletPosition() + Vec2{0.0F, kToiletSeatForwardOffset}; }
    Vec2 toiletFront() const { return {0.0F, -3.55F}; }
    Vec2 spawnPoint(std::uint32_t id) const;
    Character* find(std::uint32_t id);
    void setDestinationLocked(Character& character, const Vec2& destination, bool sprint = false, bool advanced = false);
    void requestToiletLocked(Character& character, ToiletAction action);
    void performAttack(Character& attacker, Character& target);
    void damageCharacter(Character& attacker, Character& target, float damage, bool ranged);
    void spawnPiss(Character& character);
    void spawnBullet(Character& attacker, const Character& target);
    void spawnBullet(Character& attacker, Vec2 target, float height, std::uint32_t targetId);
    void rippleWater(Vec2 position, float strength, bool refresh = false);
    void updateProjectiles(float dt);
    void updatePendingAttack(Character& character);
    void beginToilet(Character& character, ToiletAction action);
    void finishToilet(Character& character, const std::string& message = "");
    void updateCharacter(Character& character, float dt);
    void moveBody(Character& character, const Vec2& delta);
    void updateVertical(Character& character, float dt);
    void updateAI(Character& character, float dt);
    void updatePissing(Character& character, float dt);
    void addPuddle(const Puddle& puddle);
    void addEvent(const std::string& text);
    void notify(Character& character, const std::string& text);
};

} // namespace gtc
