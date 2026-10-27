#include "core/projectile.h"

#include "core/world.h"

#include <algorithm>

namespace gtc {

std::vector<Projectile> World::projectiles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return projectiles_;
}

std::vector<Puddle> World::puddles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return puddles_;
}

double World::gameSeconds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return 8.0 * 60.0 * 60.0 + elapsed_ * kGameTimeScale;
}

void World::spawnPiss(Character& character) {
    Projectile projectile;
    projectile.id = nextProjectileId_++;
    projectile.owner = character.id;
    projectile.kind = ProjectileKind::Piss;
    projectile.stain = projectile.id % 6U == 0;
    projectile.toilet = character.toiletAction == ToiletAction::Piss;
    const float pitch = projectile.toilet ? -0.32F : character.pitch;
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const Vec2 forward{std::sin(character.yaw), std::cos(character.yaw)};
    projectile.position = character.position + forward * (0.10F + 0.06F * cp);
    projectile.height = character.elevation + 0.79F + 0.10F * sp;
    projectile.velocity = forward * (kPissSpeed * cp);
    projectile.verticalVelocity = kPissSpeed * sp;
    if (projectiles_.size() >= 768) projectiles_.erase(projectiles_.begin());
    projectiles_.push_back(projectile);
}

void World::spawnBullet(Character& attacker, const Character& target) {
    const Vec2 initial{std::sin(attacker.yaw), std::cos(attacker.yaw)};
    Projectile projectile;
    projectile.id = nextProjectileId_++;
    projectile.owner = attacker.id;
    projectile.kind = ProjectileKind::Bullet;
    projectile.position = attacker.position + initial * 0.16F;
    projectile.height = attacker.elevation + 0.79F;
    const Vec2 delta = target.position - projectile.position;
    const float yaw = std::atan2(delta.x, delta.y);
    const float targetHeight = target.elevation + (target.prone ? 0.24F : target.crouched ? 0.65F : 0.95F);
    const float pitch = ballisticPitch(length(delta), targetHeight - projectile.height, kBulletSpeed);
    attacker.yaw = yaw;
    attacker.pitch = pitch;
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const Vec2 forward{std::sin(yaw), std::cos(yaw)};
    projectile.velocity = forward * (kBulletSpeed * cp);
    projectile.verticalVelocity = kBulletSpeed * sp;
    if (projectiles_.size() >= 768) projectiles_.erase(projectiles_.begin());
    projectiles_.push_back(projectile);
}

void World::updatePissing(Character& character, float dt) {
    const bool ordinary = character.toiletAction == ToiletAction::None && character.wantsPiss &&
        (character.unzipped || character.ai);
    const bool active = character.toiletAction == ToiletAction::Piss || ordinary;
    character.pissing = active && !character.onToilet && character.needs.bladder > 0.0F;
    if (!active || character.needs.bladder <= 0.0F) {
        character.projectileTimer = 0.0F;
        return;
    }
    character.needs.bladder = std::max(0.0F, character.needs.bladder - kBladderDrainPerSecond * dt);
    if (character.needs.bladder < 0.001F) { character.needs.bladder = 0.0F; character.pissing = false; }
    if (!character.pissing) {
        character.projectileTimer = 0.0F;
        return;
    }
    character.projectileTimer -= dt;
    int emitted = 0;
    while (character.projectileTimer <= 0.0F && emitted++ < 5) {
        spawnPiss(character);
        character.projectileTimer += kPissEmissionInterval;
    }
}

void World::addPuddle(const Puddle& puddle) {
    Puddle placed = placePuddle(puddles_, puddle, nextPuddleId_++, obstacles_, kHalfWidth, kHalfDepth);
    if (!placed.id) return;
    if (puddles_.size() >= 4096) puddles_.erase(puddles_.begin());
    puddles_.push_back(placed);
}

void World::updateProjectiles(float dt) {
    for (Puddle& puddle : puddles_) puddle.age += dt;
    puddles_.erase(std::remove_if(puddles_.begin(), puddles_.end(), [](const Puddle& puddle) {
        return puddle.age >= kPuddleLifetimeSeconds;
    }), puddles_.end());
    const Vec2 wind = windAt(elapsed_);
    for (std::size_t index = 0; index < projectiles_.size();) {
        Projectile projectile = projectiles_[index];
        const bool bullet = projectile.kind == ProjectileKind::Bullet;
        const float drag = bullet ? 0.025F : 0.16F;
        const projectile_detail::Point old = projectile_detail::point(projectile);
        const Vec2 nextVelocity = projectile.velocity + (wind - projectile.velocity) * (drag * dt);
        const float nextVertical = projectile.verticalVelocity - kProjectileGravity * dt;
        projectile_detail::Point next;
        next.x = old.x + (projectile.velocity.x + nextVelocity.x) * (0.5F * dt);
        next.z = old.z + (projectile.velocity.y + nextVelocity.y) * (0.5F * dt);
        next.y = old.y + (projectile.verticalVelocity + nextVertical) * (0.5F * dt);
        projectile.age += dt;
        const float radius = bullet ? 0.015F : 0.022F;
        projectile_detail::SurfaceHit hit = projectile_detail::sweepSurfaces(old, next, radius, obstacles_);
        Character* victim = nullptr;
        float victimAmount = hit.hit ? hit.amount : 1.0F;
        if (bullet) {
            for (auto& entry : characters_) {
                Character& candidate = entry.second;
                if (candidate.id == projectile.owner) continue;
                float amount = 1.0F;
                if (projectile_detail::sweepCharacter(old, next, radius, candidate, amount) && amount <= victimAmount) {
                    victim = &candidate;
                    victimAmount = amount;
                }
            }
        }
        const auto erase = [&] { projectiles_.erase(projectiles_.begin() + static_cast<std::ptrdiff_t>(index)); };
        if (victim) {
            if (Character* attacker = find(projectile.owner)) damageCharacter(*attacker, *victim, 7.0F, true);
            erase();
            continue;
        }
        if (hit.hit) {
            if (!bullet && projectile.stain && !hit.absorb && !(projectile.toilet && hit.toilet)) addPuddle(hit.puddle);
            erase();
            continue;
        }
        const bool expired = projectile.age >= (bullet ? 3.0F : kPissLifetime) ||
            std::abs(next.x) > kHalfWidth + 1.0F || std::abs(next.z) > kHalfDepth + 1.0F ||
            next.y < -0.1F || (old.y <= 3.4F && next.y >= 3.4F);
        if (expired) {
            erase();
            continue;
        }
        projectile.position = {next.x, next.z};
        projectile.height = next.y;
        projectile.velocity = nextVelocity;
        projectile.verticalVelocity = nextVertical;
        projectiles_[index] = projectile;
        ++index;
    }
}

} // namespace gtc
