#include "core/world.h"

#include "core/collision.h"
#include "core/projectile.h"
#include "core/json.h"
#include "core/world_internal.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <sstream>

namespace gtc {

using namespace world_detail;

World::World() : pathfinder_(obstacles_, kHalfWidth, kHalfDepth) {
    obstacles_ = {
        {{0.0F, -7.0F}, {9.0F, 0.16F}, 3.4F, "boundary"},
        {{0.0F, 7.0F}, {9.0F, 0.16F}, 0.55F, "boundary"},
        {{-9.0F, 0.0F}, {0.16F, 7.0F}, 3.4F, "boundary"},
        {{9.0F, 0.0F}, {0.16F, 7.0F}, 0.55F, "boundary"},
        {toiletPosition(), {0.2285F, 0.3525F}, kToiletTankHeight, "toilet"},
        {{5.2F, -4.95F}, {0.85F, 0.55F}, 1.1F, "sink"},
        {{-6.6F, -3.8F}, {0.75F, 0.75F}, 1.25F, "crate"},
        {{6.7F, 3.7F}, {0.65F, 0.65F}, 0.85F, "crate"},
        {{3.1F, -1.35F}, {0.8F, 0.45F}, 0.18F, "step"},
        {{3.1F, -2.45F}, {0.8F, 0.45F}, 0.45F, "step"},
        {{-5.0F, 0.0F}, {1.0F, 0.7F}, 1.55F, "tunnel", 0.60F}
    };
}

std::uint32_t World::addCharacter(const std::string& name, bool ai) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint32_t id = nextCharacterId_++;
    Character character;
    character.id = id;
    character.name = cleanName(name);
    character.ai = ai;
    character.position = spawnPoint(id);
    static const char* colors[] = {"#d7ff54", "#ff765f", "#6edbd2", "#c59aff", "#ffcc62"};
    character.color = colors[(id - 1U) % 5U];
    character.appearance.male = id % 3U != 0;
    character.appearance.skin = static_cast<int>(id % 6U);
    character.appearance.hair = static_cast<int>(id % 4U) + 1;
    character.appearance.hairColor = static_cast<int>(id % 6U);
    character.appearance.shirt = static_cast<int>(id % 7U);
    character.appearance.pants = static_cast<int>(id % 4U);
    character.appearance.beard = static_cast<int>(id % 3U);
    character.appearance.coat = id % 5U == 0;
    character.appearance.sunglasses = id % 4U == 0;
    if (ai) {
        character.appearance = {};
        character.appearance.beard = 2;
        character.appearance.coat = true;
        character.appearance.sunglasses = true;
    }
    character.needs.bladder = ai ? 86.0F : 58.0F + static_cast<float>((id * 7U) % 23U);
    character.needs.bowel = ai ? 67.0F : 34.0F + static_cast<float>((id * 11U) % 31U);
    character.aiTimer = ai ? 2.5F : 0.0F;
    characters_.emplace(id, std::move(character));
    addEvent(characters_.at(id).name + (ai ? " crawled out of the algorithm." : " entered the gene pool."));
    return id;
}

void World::removeCharacter(std::uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = characters_.find(id);
    if (found == characters_.end()) return;
    if (toiletUser_ == id) toiletUser_ = 0;
    addEvent(found->second.name + " returned to the void.");
    characters_.erase(found);
}

void World::renameCharacter(std::uint32_t id, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Character* character = find(id)) character->name = cleanName(name);
}

void World::customizeCharacter(std::uint32_t id, const Appearance& appearance) {
    std::lock_guard<std::mutex> lock(mutex_);
    Character* character = find(id);
    if (character == nullptr) return;
    const bool wasPiss = character->toiletAction == ToiletAction::Piss;
    const bool sexChanged = character->appearance.male != appearance.male;
    character->appearance = cleanAppearance(appearance);
    if (wasPiss && sexChanged) finishToilet(*character);
    if (!character->appearance.male) {
        character->unzipped = false;
        character->wantsPiss = false;
        character->pissing = false;
    }
    notify(*character, "Citizen mesh rebuilt. Dignity remains procedurally generated.");
}

void World::toggleZip(std::uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    Character* character = find(id);
    if (!character || !character->appearance.male || character->toiletAction != ToiletAction::None) return;
    character->unzipped = !character->unzipped;
    character->wantsPiss = false;
    character->pissing = false;
    character->blocking = false;
    notify(*character, character->unzipped ? "Trousers unzipped. Concealment has left the building." :
        "Trousers zipped. Loose civilian fabric restored.");
}

void World::damageCharacter(Character& attacker, Character& target, float damage, bool ranged) {
    if (target.blocking) damage *= 0.35F;
    target.health = std::max(0.0F, target.health - damage);
    notify(target, attacker.name + (ranged ? " scored a physical projectile hit." : " landed a melee bonk."));
    addEvent(attacker.name + (ranged ? " weaponized poor decisions against " : " bonked ") + target.name + ".");
    if (target.health > 0.0F) return;
    addEvent(target.name + " got flattened and respawned with fresh shame.");
    if (target.toiletAction != ToiletAction::None) finishToilet(target);
    target.health = 100.0F;
    target.position = spawnPoint(target.id);
    target.elevation = target.verticalVelocity = 0.0F;
    target.grounded = true;
    target.path.clear();
    target.pendingAttack = 0;
    target.wantsPiss = target.pissing = target.blocking = false;
    target.grabbing = false;
    target.grabbedObstacle = -1;
}

void World::performAttack(Character& attacker, Character& target) {
    const bool ranged = attacker.appearance.male && attacker.unzipped;
    const float targetDistance = distance(attacker.position, target.position);
    if (attacker.attackCooldown > 0.0F || (!ranged && (targetDistance > 1.15F ||
        std::abs(attacker.elevation - target.elevation) > 0.8F))) return;
    if (ranged && attacker.needs.bladder < 2.0F) {
        notify(attacker, "The ranged weapon is dry. Hydrate or embarrass yourself later.");
        return;
    }
    const Vec2 toTarget = target.position - attacker.position;
    attacker.yaw = std::atan2(toTarget.x, toTarget.y);
    attacker.attackAnim = ranged ? 0.32F : 0.26F;
    attacker.attackCooldown = ranged ? 0.55F : 0.45F;
    if (ranged) {
        attacker.needs.bladder = std::max(0.0F, attacker.needs.bladder - 4.0F);
        spawnBullet(attacker, target);
        notify(attacker, "Physical shot away. Wind and gravity have voting rights.");
        addEvent(attacker.name + " fired a physical round toward " + target.name + ".");
        return;
    }
    damageCharacter(attacker, target, 12.0F, false);
}

void World::updatePendingAttack(Character& character) {
    if (!character.pendingAttack) return;
    Character* target = find(character.pendingAttack);
    if (!target) { character.pendingAttack = 0; return; }
    const bool ranged = character.appearance.male && character.unzipped;
    const float range = ranged ? 3.0F : 1.15F;
    if (distance(character.position, target->position) <= range &&
        std::abs(character.elevation - target->elevation) <= (ranged ? 1.5F : 0.8F)) {
        if (character.attackCooldown > 0.0F) return;
        performAttack(character, *target);
        if (character.attackAnim > 0.0F) {
            character.pendingAttack = 0;
            character.path.clear();
        }
        return;
    }
    if (!ranged && character.path.empty()) {
        const std::uint32_t pending = character.pendingAttack;
        setDestinationLocked(character, target->position, false, character.pathAdvanced);
        if (!character.path.empty()) character.pendingAttack = pending;
    }
}

void World::attack(std::uint32_t id, std::uint32_t targetId, bool advanced) {
    std::lock_guard<std::mutex> lock(mutex_);
    Character* attacker = find(id);
    Character* target = find(targetId);
    if (!attacker || !target || attacker == target || attacker->toiletAction != ToiletAction::None) return;
    const bool ranged = attacker->appearance.male && attacker->unzipped;
    if (ranged) {
        performAttack(*attacker, *target);
        return;
    }
    if (distance(attacker->position, target->position) <= 1.15F &&
        std::abs(attacker->elevation - target->elevation) <= 0.8F) {
        performAttack(*attacker, *target);
        return;
    }
    setDestinationLocked(*attacker, target->position, false, advanced);
    if (!attacker->path.empty()) attacker->pendingAttack = targetId;
}

Character* World::find(std::uint32_t id) {
    const auto found = characters_.find(id);
    return found == characters_.end() ? nullptr : &found->second;
}

Character World::character(std::uint32_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = characters_.find(id);
    return found == characters_.end() ? Character{} : found->second;
}

std::size_t World::characterCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return characters_.size();
}

Vec2 World::spawnPoint(std::uint32_t id) const {
    static const Vec2 points[] = {{-2.0F, 2.3F}, {2.0F, 2.3F}, {-4.0F, 1.0F},
                                  {4.0F, 1.0F}, {-2.0F, 4.8F}, {2.0F, 4.8F}};
    return points[(id - 1U) % 6U];
}

void World::setInput(std::uint32_t id, const Vec2& move, bool sprint, bool piss,
                     float yaw, float pitch, bool slowWalk, bool crouch, bool grab, bool jump, bool block,
                     Vec2 aimTarget, float aimHeight, bool hasAimTarget) {
    std::lock_guard<std::mutex> lock(mutex_);
    Character* character = find(id);
    if (character == nullptr) return;
    const float magnitude = length(move);
    if (character->toiletAction != ToiletAction::None) {
        if (magnitude > 0.05F) {
            finishToilet(*character);
        } else {
            character->manualMove = {};
            character->sprint = false;
            character->wantsPiss = false;
            character->crouchInput = character->grabInput = character->jumpInput = false;
            character->blocking = false;
            character->jumpHeld = jump;
            return;
        }
    }
    character->manualMove = magnitude > 1.0F ? move * (1.0F / magnitude) : move;
    character->sprint = sprint;
    character->slowWalk = slowWalk;
    character->wantsPiss = piss;
    character->crouchInput = crouch;
    character->grabInput = grab;
    character->jumpInput = jump;
    character->blocking = block && !(character->appearance.male && character->unzipped);
    character->hasAimTarget = hasAimTarget && std::isfinite(aimTarget.x) && std::isfinite(aimTarget.y) &&
        std::isfinite(aimHeight) && aimHeight >= -0.1F && aimHeight <= 3.5F &&
        distance(character->position, aimTarget) <= 20.0F;
    if (character->hasAimTarget) {
        character->aimTarget = aimTarget;
        character->aimHeight = aimHeight;
    }
    if (std::isfinite(yaw)) character->yaw = yaw;
    if (std::isfinite(pitch)) character->pitch = std::clamp(pitch, -1.57079633F, 1.57079633F);
    if (piss && character->hasAimTarget)
        character->pitch = pissLaunchPitch(*character, character->aimTarget, character->aimHeight, character->pitch);
}

void World::setDestination(std::uint32_t id, const Vec2& destination, bool sprint, bool advanced) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Character* character = find(id)) setDestinationLocked(*character, destination, sprint, advanced);
}

void World::setDestinationLocked(Character& character, const Vec2& destination, bool sprint, bool advanced) {
    if (character.toiletAction != ToiletAction::None ||
        !std::isfinite(destination.x) || !std::isfinite(destination.y)) return;
    character.pendingToilet = false;
    character.pendingAttack = 0;
    character.queuedToiletAction = ToiletAction::None;
    character.wantsPiss = false;
    character.grabbing = false;
    character.grabbedObstacle = -1;
    character.grabHeight = 0.0F;
    character.charge = character.fullChargeTime = 0.0F;
    character.pathSprint = sprint;
    character.pathAdvanced = advanced;
    character.path = advanced ? std::vector<Vec2>{destination} : pathfinder_.findPath(character.position, destination);
    if (character.path.empty()) notify(character, "No route. Try being less geographically challenged.");
}

void World::requestToilet(std::uint32_t id, const std::string& action) {
    std::lock_guard<std::mutex> lock(mutex_);
    Character* character = find(id);
    if (character == nullptr) return;
    if (action == "stand") {
        finishToilet(*character);
        return;
    }
    ToiletAction requested = ToiletAction::None;
    if (action == "sit") requested = ToiletAction::Sit;
    if (action == "shit") requested = ToiletAction::Shit;
    if (action == "piss") requested = ToiletAction::Piss;
    if (requested != ToiletAction::None) requestToiletLocked(*character, requested);
}

void World::requestToiletLocked(Character& character, ToiletAction action) {
    if (character.toiletAction != ToiletAction::None) return;
    if (distance(character.position, toiletFront()) > kInteractionRange) {
        notify(character, "Out of range. Move closer to interact.");
        return;
    }
    if (toiletUser_ != 0 && toiletUser_ != character.id) {
        notify(character, "Throne occupied. Find another outlet.");
        return;
    }
    beginToilet(character, action);
}

void World::adjustNeed(std::uint32_t id, const std::string& need, float delta) {
    std::lock_guard<std::mutex> lock(mutex_);
    Character* character = find(id);
    if (character == nullptr || !std::isfinite(delta)) return;
    float* value = nullptr;
    if (need == "bladder") value = &character->needs.bladder;
    if (need == "bowel") value = &character->needs.bowel;
    if (value == nullptr) return;
    *value = std::clamp(*value + std::clamp(delta, -100.0F, 100.0F), 0.0F, 100.0F);
    notify(*character, "Cheat: " + need + " set to " + std::to_string(static_cast<int>(*value)) + "%.");
}

void World::notify(Character& character, const std::string& text) {
    character.notice = text;
    ++character.noticeSequence;
}

void World::beginToilet(Character& character, ToiletAction action) {
    if (action == ToiletAction::None || toiletUser_ != 0 ||
        distance(character.position, toiletFront()) > kInteractionRange) return;
    toiletUser_ = character.id;
    character.toiletAction = action;
    character.queuedToiletAction = ToiletAction::None;
    character.onToilet = action == ToiletAction::Sit || action == ToiletAction::Shit ||
                         !character.appearance.male;
    character.pendingToilet = false;
    character.pissing = action == ToiletAction::Piss && !character.onToilet;
    character.wantsPiss = false;
    character.manualMove = {};
    character.path.clear();
    character.elevation = character.verticalVelocity = character.charge = character.fullChargeTime = 0.0F;
    character.grounded = true;
    character.crouched = character.prone = character.grabbing = false;
    character.grabbedObstacle = -1;
    character.grabHeight = 0.0F;
    character.position = character.onToilet ? toiletSeat() : toiletFront();
    character.yaw = character.onToilet ? 0.0F : 3.14159265F;
    character.pitch = action == ToiletAction::Piss && !character.onToilet ? -0.32F : 0.0F;
    character.pissYaw = character.yaw; character.pissPitch = character.pitch;
    character.actionTime = 0.0F;
    character.speed = 0.0F;
    if (action == ToiletAction::Sit) addEvent(character.name + " parked their cheeks on the porcelain.");
    if (action == ToiletAction::Shit) addEvent(character.name + " began a tactical bowel download.");
    if (action == ToiletAction::Piss) addEvent(character.name + (character.onToilet ?
        " took a seated piss." : " aimed at the porcelain like a professional."));
}

void World::finishToilet(Character& character, const std::string& message) {
    if (character.toiletAction == ToiletAction::None) return;
    character.toiletAction = ToiletAction::None;
    character.queuedToiletAction = ToiletAction::None;
    character.pendingToilet = false;
    character.onToilet = false;
    character.pissing = false;
    character.wantsPiss = false;
    character.manualMove = {};
    character.path.clear();
    character.elevation = character.verticalVelocity = character.charge = character.fullChargeTime = 0.0F;
    character.grounded = true;
    character.crouched = character.prone = character.grabbing = false;
    character.grabbedObstacle = -1;
    character.grabHeight = 0.0F;
    character.position = toiletFront();
    character.yaw = character.pitch = 0.0F;
    character.actionTime = 0.0F;
    character.speed = 0.0F;
    if (toiletUser_ == character.id) toiletUser_ = 0;
    if (!message.empty()) notify(character, message);
}

void World::update(float deltaSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    const float dt = std::clamp(deltaSeconds, 0.0F, 0.1F);
    ++tick_;
    elapsed_ += dt;
    for (auto& entry : characters_) {
        Character& character = entry.second;
        if (character.ai) updateAI(character, dt);
        updateCharacter(character, dt);
    }
    for (auto first = characters_.begin(); first != characters_.end(); ++first) {
        for (auto second = std::next(first); second != characters_.end(); ++second) {
            Character& a = first->second;
            Character& b = second->second;
            if (a.toiletAction != ToiletAction::None || b.toiletAction != ToiletAction::None ||
                a.elevation >= b.elevation + bodyHeight(b) || b.elevation >= a.elevation + bodyHeight(a)) continue;
            const Vec2 difference = b.position - a.position;
            const float currentDistance = length(difference);
            if (currentDistance >= kBodyRadius * 2.0F) continue;
            const Vec2 normal = currentDistance > 0.001F ? difference * (1.0F / currentDistance) : Vec2{1.0F, 0.0F};
            const float push = (kBodyRadius * 2.0F - currentDistance) * 0.5F;
            a.position = moveWithCollisions(a.position, normal * -push, kBodyRadius, obstacles_, kHalfWidth, kHalfDepth);
            b.position = moveWithCollisions(b.position, normal * push, kBodyRadius, obstacles_, kHalfWidth, kHalfDepth);
        }
    }
    updateProjectiles(dt);
}

void World::updateAI(Character& character, float dt) {
    character.aiTimer -= dt;
    if (character.toiletAction != ToiletAction::None || character.aiTimer > 0.0F) return;
    if ((character.needs.bladder > 78.0F || character.needs.bowel > 78.0F) &&
        !character.pendingToilet && toiletUser_ == 0) {
        const ToiletAction action = character.needs.bowel > 78.0F ? ToiletAction::Shit : ToiletAction::Piss;
        if (distance(character.position, toiletFront()) <= kInteractionRange) {
            requestToiletLocked(character, action);
        } else {
            character.pendingToilet = true;
            character.queuedToiletAction = action;
            character.path = pathfinder_.findPath(character.position, toiletFront());
            if (character.path.empty()) {
                character.pendingToilet = false;
                character.queuedToiletAction = ToiletAction::None;
            }
        }
        character.aiTimer = 1.0F;
        return;
    }
    if (character.pendingToilet) {
        character.aiTimer = 0.5F;
        return;
    }
    if (character.needs.bladder > 94.0F && toiletUser_ != 0) {
        character.wantsPiss = true;
        character.yaw = 0.4F;
        character.aiTimer = 0.5F;
        return;
    }
    if (character.pissing) {
        character.wantsPiss = character.needs.bladder > 35.0F;
        character.aiTimer = 0.5F;
        return;
    }
    if (character.path.empty()) {
        std::uniform_real_distribution<float> x(-6.5F, 6.5F);
        std::uniform_real_distribution<float> z(-1.5F, 5.0F);
        setDestinationLocked(character, {x(random_), z(random_)});
    }
    character.aiTimer = 4.0F;
}

void World::moveBody(Character& character, const Vec2& delta) {
    const int steps = std::max(1, static_cast<int>(std::ceil(length(delta) / 0.18F)));
    const Vec2 step = delta * (1.0F / static_cast<float>(steps));
    for (int i = 0; i < steps; ++i) {
        for (int axis = 0; axis < 2; ++axis) {
            Vec2 candidate = character.position;
            if (axis == 0) candidate.x += step.x;
            else candidate.y += step.y;
            applyStance(character, candidate, obstacles_);
            if (!bodyHitsWorld(candidate, kBodyRadius, character.elevation, bodyHeight(character),
                               obstacles_, kHalfWidth, kHalfDepth)) {
                character.position = candidate;
                continue;
            }
            if (!character.grounded) continue;
            float stepTop = -1.0F;
            for (const Obstacle& obstacle : obstacles_) {
                const float top = obstacleHeightAt(obstacle, candidate);
                const float rise = top - character.elevation;
                if (obstacle.kind == "boundary" || obstacle.base > character.elevation + 0.02F ||
                    rise <= 0.005F || rise > kJumpHeight + 0.001F ||
                    !bodyHitsObstacle(candidate, kBodyRadius, character.elevation, bodyHeight(character), obstacle)) continue;
                stepTop = std::max(stepTop, top);
            }
            if (stepTop < 0.0F) {
                if (!character.pathAdvanced || character.path.empty()) continue;
                for (std::size_t index = 0; index < obstacles_.size(); ++index) {
                    const Obstacle& obstacle = obstacles_[index];
                    const float rise = obstacleHeightAt(obstacle, candidate) - character.elevation;
                    if (obstacle.kind == "boundary" || obstacle.base > character.elevation + 0.02F || rise <= 0.0F ||
                        !bodyHitsObstacle(candidate, kBodyRadius, character.elevation, bodyHeight(character), obstacle)) continue;
                    if (rise <= kMaximumJumpHeight) {
                        character.grounded = false;
                        character.verticalVelocity = std::sqrt(2.0F * kGravity * std::min(kMaximumJumpHeight, rise * 1.08F));
                        return;
                    }
                    if (rise <= kArmReachWithoutHands) {
                        character.grabbing = true;
                        character.grabbedObstacle = static_cast<int>(index);
                        character.grabHeight = obstacleHeightAt(obstacle, candidate);
                        character.grounded = false;
                        character.verticalVelocity = 0.0F;
                        return;
                    }
                }
                continue;
            }
            if (!bodyHitsWorld(candidate, kBodyRadius, stepTop, bodyHeight(character),
                               obstacles_, kHalfWidth, kHalfDepth)) {
                character.position = candidate; character.elevation = stepTop;
                character.verticalVelocity = 0.0F;
            }
        }
    }
}

void World::updateVertical(Character& character, float dt) {
    if (character.grabbing) return;
    const float oldElevation = character.elevation;
    if (character.grounded) {
        const float support = supportHeight(character, obstacles_);
        if (std::abs(oldElevation - support) <= 0.061F) {
            character.elevation = support;
            character.verticalVelocity = 0.0F;
            return;
        }
        character.grounded = false;
    }
    const float nextVelocity = character.verticalVelocity - kGravity * dt;
    float next = oldElevation + character.verticalVelocity * dt - 0.5F * kGravity * dt * dt;
    character.verticalVelocity = nextVelocity;
    const float height = bodyHeight(character);
    if (character.verticalVelocity > 0.0F) {
        float ceiling = 1000.0F;
        for (const Obstacle& obstacle : obstacles_) {
            if (obstacle.kind == "boundary" || !circleHitsObstacle(character.position, kBodyRadius, obstacle)) continue;
            if (oldElevation + height <= obstacle.base + 0.02F && next + height > obstacle.base) {
                ceiling = std::min(ceiling, obstacle.base);
            }
        }
        if (ceiling < 999.0F) {
            next = std::max(0.0F, ceiling - height);
            character.verticalVelocity = 0.0F;
        }
    }
    if (character.verticalVelocity <= 0.0F) {
        float landing = -1.0F;
        if (oldElevation >= 0.0F && next <= 0.0F) landing = 0.0F;
        for (const Obstacle& obstacle : obstacles_) {
            const float top = obstacleHeightAt(obstacle, character.position);
            if (obstacle.kind == "boundary" || oldElevation < top - 0.02F ||
                next > top || !circleHitsObstacle(character.position, kBodyRadius, obstacle)) continue;
            landing = std::max(landing, top);
        }
        if (landing >= 0.0F) {
            next = landing;
            character.verticalVelocity = 0.0F;
            character.grounded = true;
        }
    }
    character.elevation = std::max(0.0F, next);
}

} // namespace gtc
