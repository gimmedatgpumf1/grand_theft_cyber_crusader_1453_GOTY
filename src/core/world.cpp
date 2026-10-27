#include "core/world.h"

#include "core/collision.h"
#include "core/json.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <sstream>

namespace gtc {
namespace {

constexpr float kBodyRadius = 0.36F;

const char* actionName(ToiletAction action) {
    switch (action) {
        case ToiletAction::Sit: return "sit";
        case ToiletAction::Shit: return "shit";
        case ToiletAction::Piss: return "piss";
        default: return "none";
    }
}

Appearance cleanAppearance(Appearance appearance) {
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

void appendVec(std::ostringstream& out, const Vec2& value) {
    out << '[' << jsonNumber(value.x) << ',' << jsonNumber(value.y) << ']';
}

void appendCharacter(std::ostringstream& out, const Character& character) {
    out << "{\"id\":" << character.id
        << ",\"name\":\"" << jsonEscape(character.name) << "\""
        << ",\"color\":\"" << character.color << "\""
        << ",\"ai\":" << (character.ai ? "true" : "false")
        << ",\"x\":" << jsonNumber(character.position.x)
        << ",\"z\":" << jsonNumber(character.position.y)
        << ",\"yaw\":" << jsonNumber(character.yaw)
        << ",\"pitch\":" << jsonNumber(character.pitch)
        << ",\"moveX\":" << jsonNumber(character.manualMove.x)
        << ",\"moveZ\":" << jsonNumber(character.manualMove.y)
        << ",\"speed\":" << jsonNumber(character.speed)
        << ",\"bladder\":" << jsonNumber(character.needs.bladder)
        << ",\"bowel\":" << jsonNumber(character.needs.bowel)
        << ",\"pissing\":" << (character.pissing ? "true" : "false")
        << ",\"onToilet\":" << (character.onToilet ? "true" : "false")
        << ",\"toiletAction\":\"" << actionName(character.toiletAction) << "\""
        << ",\"male\":" << (character.appearance.male ? "true" : "false")
        << ",\"skin\":" << character.appearance.skin
        << ",\"hair\":" << character.appearance.hair
        << ",\"hairColor\":" << character.appearance.hairColor
        << ",\"shirt\":" << character.appearance.shirt
        << ",\"pants\":" << character.appearance.pants
        << ",\"beard\":" << character.appearance.beard
        << ",\"coat\":" << (character.appearance.coat ? "true" : "false")
        << ",\"sunglasses\":" << (character.appearance.sunglasses ? "true" : "false")
        << ",\"eyeColor\":" << character.appearance.eyeColor
        << ",\"makeup\":" << character.appearance.makeup
        << ",\"lipColor\":" << character.appearance.lipColor
        << ",\"suit\":" << (character.appearance.suit ? "true" : "false")
        << ",\"tie\":" << character.appearance.tie
        << ",\"dress\":" << (character.appearance.dress ? "true" : "false")
        << ",\"beret\":" << (character.appearance.beret ? "true" : "false")
        << ",\"earrings\":" << (character.appearance.earrings ? "true" : "false")
        << ",\"necklace\":" << (character.appearance.necklace ? "true" : "false")
        << ",\"fur\":" << (character.appearance.fur ? "true" : "false")
        << ",\"morphs\":[";
    for (std::size_t i = 0; i < character.appearance.morphs.size(); ++i) {
        if (i != 0) out << ',';
        out << character.appearance.morphs[i];
    }
    out << ']'
        << ",\"notice\":" << character.noticeSequence
        << ",\"noticeText\":\"" << jsonEscape(character.notice) << "\""
        << ",\"path\":[";
    for (std::size_t i = 0; i < character.path.size(); ++i) {
        if (i != 0) out << ',';
        appendVec(out, character.path[i]);
    }
    out << "]}";
}

std::string cleanName(std::string name) {
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

} // namespace

World::World() : pathfinder_(obstacles_, kHalfWidth, kHalfDepth) {
    obstacles_ = {
        {{0.0F, -7.0F}, {9.0F, 0.16F}, 3.4F, "boundary"},
        {{0.0F, 7.0F}, {9.0F, 0.16F}, 0.55F, "boundary"},
        {{-9.0F, 0.0F}, {0.16F, 7.0F}, 3.4F, "boundary"},
        {{9.0F, 0.0F}, {0.16F, 7.0F}, 0.55F, "boundary"},
        {toiletPosition(), {0.58F, 0.72F}, 1.4F, "toilet"},
        {{5.2F, -4.95F}, {0.85F, 0.55F}, 1.1F, "sink"},
        {{-6.6F, -3.8F}, {0.75F, 0.75F}, 1.25F, "crate"},
        {{6.7F, 3.7F}, {0.65F, 0.65F}, 0.85F, "crate"}
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
    notify(*character, "Citizen mesh rebuilt. Dignity remains procedurally generated.");
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
                     float yaw, float pitch, bool slowWalk) {
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
            return;
        }
    }
    character->manualMove = magnitude > 1.0F ? move * (1.0F / magnitude) : move;
    character->sprint = sprint;
    character->slowWalk = slowWalk;
    character->wantsPiss = piss;
    if (std::isfinite(yaw)) character->yaw = yaw;
    if (std::isfinite(pitch)) character->pitch = std::clamp(pitch, -1.5F, 1.35F);
}

void World::setDestination(std::uint32_t id, const Vec2& destination) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Character* character = find(id)) setDestinationLocked(*character, destination);
}

void World::setDestinationLocked(Character& character, const Vec2& destination) {
    if (character.toiletAction != ToiletAction::None ||
        !std::isfinite(destination.x) || !std::isfinite(destination.y)) return;
    character.pendingToilet = false;
    character.queuedToiletAction = ToiletAction::None;
    character.wantsPiss = false;
    character.path = pathfinder_.findPath(character.position, destination);
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
    character.position = character.onToilet ? toiletSeat() : toiletFront();
    character.yaw = character.onToilet ? 0.0F : 3.14159265F;
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
    character.position = toiletFront();
    character.yaw = 0.0F;
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
            if (a.toiletAction != ToiletAction::None || b.toiletAction != ToiletAction::None) continue;
            const Vec2 difference = b.position - a.position;
            const float currentDistance = length(difference);
            if (currentDistance >= kBodyRadius * 2.0F) continue;
            const Vec2 normal = currentDistance > 0.001F ? difference * (1.0F / currentDistance) : Vec2{1.0F, 0.0F};
            const float push = (kBodyRadius * 2.0F - currentDistance) * 0.5F;
            a.position = moveWithCollisions(a.position, normal * -push, kBodyRadius, obstacles_, kHalfWidth, kHalfDepth);
            b.position = moveWithCollisions(b.position, normal * push, kBodyRadius, obstacles_, kHalfWidth, kHalfDepth);
        }
    }
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

void World::updateCharacter(Character& character, float dt) {
    if (character.toiletAction != ToiletAction::None) {
        character.actionTime += dt;
        character.speed = 0.0F;
        character.wantsPiss = false;
        character.position = character.onToilet ? toiletSeat() : toiletFront();
        if (character.toiletAction == ToiletAction::Sit) {
            character.needs.bladder = std::min(100.0F, character.needs.bladder + 0.065F * dt);
            character.needs.bowel = std::min(100.0F, character.needs.bowel + 0.042F * dt);
            return;
        }
        if (character.toiletAction == ToiletAction::Shit) {
            character.pissing = false;
            character.needs.bowel = std::max(0.0F, character.needs.bowel - 25.0F * dt);
            if (character.needs.bowel < 0.1F || character.actionTime > 6.0F) {
                character.needs.bowel = 0.0F;
                addEvent(character.name + " flushed two generations of evidence.");
                finishToilet(character, "Bowel emptied. Bloodline unfortunately intact.");
            }
            return;
        }
        character.needs.bladder = std::max(0.0F, character.needs.bladder - 22.0F * dt);
        character.pissing = !character.onToilet && character.needs.bladder > 0.05F;
        if (character.needs.bladder < 0.1F || character.actionTime > 6.0F) {
            character.needs.bladder = 0.0F;
            addEvent(character.name + " finished irrigation without painting the floor.");
            finishToilet(character, "Bladder emptied. Floor remains miraculously dry.");
        }
        return;
    }

    character.needs.bladder = std::min(100.0F, character.needs.bladder + 0.065F * dt);
    character.needs.bowel = std::min(100.0F, character.needs.bowel + 0.042F * dt);
    const bool manualMovement = length(character.manualMove) > 0.05F;
    Vec2 direction = character.manualMove;
    if (manualMovement) {
        character.path.clear();
        character.pendingToilet = false;
    } else if (!character.path.empty()) {
        const Vec2 difference = character.path.front() - character.position;
        if (length(difference) < 0.18F) {
            character.path.erase(character.path.begin());
        }
        if (!character.path.empty()) direction = normalized(character.path.front() - character.position);
    }

    const bool moving = length(direction) > 0.05F;
    const float walkSpeed = character.ai ? (character.pendingToilet ? kFastWalkSpeed : kSlowWalkSpeed) :
        (character.slowWalk ? kSlowWalkSpeed : kFastWalkSpeed);
    character.speed = moving ? (character.sprint ? kSprintSpeed : walkSpeed) : 0.0F;
    if (moving) {
        character.position = moveWithCollisions(character.position, normalized(direction) * (character.speed * dt),
                                                kBodyRadius, obstacles_, kHalfWidth, kHalfDepth);
        if (!character.wantsPiss && !manualMovement) {
            character.yaw = std::atan2(direction.x, direction.y);
        }
    }
    updatePissing(character, dt);

    if (character.pendingToilet && character.path.empty()) {
        if (distance(character.position, toiletFront()) <= kInteractionRange && toiletUser_ == 0) {
            beginToilet(character, character.queuedToiletAction);
        } else if (toiletUser_ != 0) {
            character.speed = 0.0F;
        } else {
            character.pendingToilet = false;
            character.queuedToiletAction = ToiletAction::None;
            notify(character, "Stopped just outside throning range. Click it again.");
        }
    }
}

void World::updatePissing(Character& character, float dt) {
    character.pissing = character.wantsPiss && character.needs.bladder > 0.05F;
    if (!character.pissing) {
        character.puddleTimer = 0.0F;
        return;
    }
    character.needs.bladder = std::max(0.0F, character.needs.bladder - 19.0F * dt);
    character.puddleTimer -= dt;
    if (character.puddleTimer > 0.0F) return;
    character.puddleTimer = 0.32F;
    const Vec2 forward{std::sin(character.yaw), std::cos(character.yaw)};
    const Vec2 landing = character.position + forward * 1.25F;
    const Vec2 toToilet = normalized(toiletPosition() - character.position);
    const float aimAtToilet = forward.x * toToilet.x + forward.y * toToilet.y;
    if (distance(character.position, toiletPosition()) < 2.4F && aimAtToilet > 0.65F) return;
    if (!circleHitsWorld(landing, 0.05F, obstacles_, kHalfWidth, kHalfDepth)) addPuddle(landing);
}

void World::addPuddle(const Vec2& position) {
    for (Puddle& puddle : puddles_) {
        if (distance(puddle.position, position) < 0.75F) {
            puddle.size = std::min(1.3F, puddle.size + 0.07F);
            puddle.position = (puddle.position + position) * 0.5F;
            return;
        }
    }
    if (puddles_.size() >= 40) puddles_.erase(puddles_.begin());
    puddles_.push_back({nextPuddleId_++, position, 0.28F});
}

void World::addEvent(const std::string& text) {
    events_.push_back({nextEventId_++, text});
    while (events_.size() > 7) events_.pop_front();
}

std::string World::description() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    out << "{\"width\":18,\"depth\":14,\"interactionRange\":" << jsonNumber(kInteractionRange)
        << ",\"toilet\":{\"x\":0,\"z\":-4.75,\"frontX\":0,\"frontZ\":-2.85}"
        << ",\"obstacles\":[";
    for (std::size_t i = 0; i < obstacles_.size(); ++i) {
        const Obstacle& obstacle = obstacles_[i];
        if (i != 0) out << ',';
        out << "{\"x\":" << jsonNumber(obstacle.center.x)
            << ",\"z\":" << jsonNumber(obstacle.center.y)
            << ",\"hx\":" << jsonNumber(obstacle.halfSize.x)
            << ",\"hz\":" << jsonNumber(obstacle.halfSize.y)
            << ",\"height\":" << jsonNumber(obstacle.height)
            << ",\"kind\":\"" << obstacle.kind << "\"}";
    }
    out << "]}";
    return out.str();
}

std::string World::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    out << "{\"type\":\"state\",\"tick\":" << tick_ << ",\"time\":" << static_cast<std::uint64_t>(elapsed_)
        << ",\"toiletUser\":" << toiletUser_ << ",\"characters\":[";
    std::size_t index = 0;
    for (const auto& entry : characters_) {
        if (index++ != 0) out << ',';
        appendCharacter(out, entry.second);
    }
    out << "],\"puddles\":[";
    for (std::size_t i = 0; i < puddles_.size(); ++i) {
        if (i != 0) out << ',';
        out << "{\"id\":" << puddles_[i].id << ",\"x\":" << jsonNumber(puddles_[i].position.x)
            << ",\"z\":" << jsonNumber(puddles_[i].position.y) << ",\"size\":" << jsonNumber(puddles_[i].size) << '}';
    }
    out << "],\"events\":[";
    for (std::size_t i = 0; i < events_.size(); ++i) {
        if (i != 0) out << ',';
        out << "{\"id\":" << events_[i].id << ",\"text\":\"" << jsonEscape(events_[i].text) << "\"}";
    }
    out << "]}";
    return out.str();
}

} // namespace gtc
