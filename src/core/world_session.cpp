// What the world tells a client: the one-off description of the room, and the
// snapshot of everything moving in it. Both are JSON written by hand here, next
// to the only code that reads the fields, so a field the snapshot forgets is a
// field this file does not have.

#include "core/world.h"

#include "core/json.h"
#include "core/projectile.h"
#include "core/world_internal.h"

#include <algorithm>
#include <sstream>

namespace gtc {
namespace {

using namespace world_detail;

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
        << ",\"pitch\":" << jsonNumber(character.pitch) << ",\"pissYaw\":" << jsonNumber(character.pissYaw) << ",\"pissPitch\":" << jsonNumber(character.pissPitch)
        << ",\"moveX\":" << jsonNumber(character.manualMove.x)
        << ",\"moveZ\":" << jsonNumber(character.manualMove.y)
        << ",\"speed\":" << jsonNumber(character.speed)
        << ",\"elevation\":" << jsonNumber(character.elevation)
        << ",\"verticalVelocity\":" << jsonNumber(character.verticalVelocity)
        << ",\"charge\":" << jsonNumber(character.charge)
        << ",\"fullChargeTime\":" << jsonNumber(character.fullChargeTime)
        << ",\"grabHeight\":" << jsonNumber(character.grabHeight)
        << ",\"health\":" << jsonNumber(character.health)
        << ",\"attackAnim\":" << jsonNumber(character.attackAnim)
        << ",\"grounded\":" << (character.grounded ? "true" : "false")
        << ",\"unzipped\":" << (character.unzipped ? "true" : "false")
        << ",\"blocking\":" << (character.blocking ? "true" : "false")
        << ",\"crouched\":" << (character.crouched ? "true" : "false")
        << ",\"prone\":" << (character.prone ? "true" : "false")
        << ",\"grabbing\":" << (character.grabbing ? "true" : "false")
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

} // namespace

void World::addEvent(const std::string& text) {
    events_.push_back({nextEventId_++, text});
    while (events_.size() > 7) events_.pop_front();
}

std::string World::description() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    out << "{\"width\":18,\"depth\":14,\"interactionRange\":" << jsonNumber(kInteractionRange)
        << ",\"toilet\":{\"x\":0,\"z\":-4.75,\"frontX\":0,\"frontZ\":-3.55}"
        << ",\"obstacles\":[";
    for (std::size_t i = 0; i < obstacles_.size(); ++i) {
        const Obstacle& obstacle = obstacles_[i];
        if (i != 0) out << ',';
        out << "{\"x\":" << jsonNumber(obstacle.center.x)
            << ",\"z\":" << jsonNumber(obstacle.center.y)
            << ",\"hx\":" << jsonNumber(obstacle.halfSize.x)
            << ",\"hz\":" << jsonNumber(obstacle.halfSize.y)
            << ",\"height\":" << jsonNumber(obstacle.height)
            << ",\"base\":" << jsonNumber(obstacle.base)
            << ",\"kind\":\"" << obstacle.kind << "\"}";
    }
    out << "]}";
    return out.str();
}

std::string World::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    const double game = 8.0 * 60.0 * 60.0 + elapsed_ * kGameTimeScale;
    const Vec2 wind = windAt(elapsed_);
    out << "{\"type\":\"state\",\"tick\":" << tick_ << ",\"time\":" << static_cast<std::uint64_t>(elapsed_)
        << ",\"gameTime\":" << jsonNumber(static_cast<float>(std::fmod(game, 86400.0)))
        << ",\"day\":" << (1 + static_cast<std::uint64_t>(game / 86400.0))
        << ",\"windX\":" << jsonNumber(wind.x) << ",\"windZ\":" << jsonNumber(wind.y)
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
            << ",\"z\":" << jsonNumber(puddles_[i].position.y) << ",\"size\":" << jsonNumber(puddles_[i].size)
            << ",\"ml\":" << jsonNumber(puddles_[i].volumeMl) << ",\"height\":" << jsonNumber(puddles_[i].height)
            << ",\"nx\":" << jsonNumber(puddles_[i].normal.x)
            << ",\"nz\":" << jsonNumber(puddles_[i].normal.y)
            << ",\"vertical\":" << (puddles_[i].vertical ? "true" : "false")
            << ",\"age\":" << jsonNumber(puddles_[i].age) << '}';
    }
    out << "],\"ripples\":[";
    for (std::size_t i = 0; i < waterRipples_.size(); ++i) {
        const WaterRipple& r = waterRipples_[i];
        if (i) out << ',';
        out << "{\"id\":" << r.id << ",\"x\":" << jsonNumber(r.position.x) << ",\"z\":" << jsonNumber(r.position.y)
            << ",\"age\":" << jsonNumber(r.age) << ",\"strength\":" << jsonNumber(r.strength) << '}';
    }
    out << "],\"projectiles\":[";
    for (std::size_t i = 0; i < projectiles_.size(); ++i) {
        const Projectile& projectile = projectiles_[i];
        if (i != 0) out << ',';
        out << "{\"id\":" << projectile.id << ",\"owner\":" << projectile.owner
            << ",\"kind\":" << (projectile.kind == ProjectileKind::Bullet ? 1 : 0)
            << ",\"x\":" << jsonNumber(projectile.position.x) << ",\"z\":" << jsonNumber(projectile.position.y)
            << ",\"y\":" << jsonNumber(projectile.height) << ",\"vx\":" << jsonNumber(projectile.velocity.x)
            << ",\"vz\":" << jsonNumber(projectile.velocity.y) << ",\"vy\":" << jsonNumber(projectile.verticalVelocity)
            << ",\"age\":" << jsonNumber(projectile.age) << ",\"toilet\":" << (projectile.toilet ? "true" : "false")
            << ",\"tx\":" << jsonNumber(projectile.target.x) << ",\"tz\":" << jsonNumber(projectile.target.y)
            << ",\"ty\":" << jsonNumber(projectile.targetHeight) << '}';
    }
    out << "],\"events\":[";
    for (std::size_t i = 0; i < events_.size(); ++i) {
        if (i != 0) out << ',';
        out << "{\"id\":" << events_[i].id << ",\"text\":\"" << jsonEscape(events_[i].text) << "\"}";
    }
    out << "]}";
    return out.str();
}

}  // namespace gtc
