#include "core/projectile.h"

#include "core/world.h"

#include <algorithm>

namespace gtc {
namespace {
bool inWater(Vec2 point, Vec2 center) {
    const Vec2 p = point - center - Vec2{0.0F, kToiletBowlOffset};
    const float x = p.x / kToiletWater.x, z = p.y / kToiletWater.y;
    return x * x + z * z <= 1.0F;
}
}

std::vector<Projectile> World::projectiles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return projectiles_;
}

std::vector<Puddle> World::puddles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return puddles_;
}

std::vector<WaterRipple> World::waterRipples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return waterRipples_;
}

void World::rippleWater(Vec2 position, float strength) {
    if (waterRipples_.size() >= 48) waterRipples_.erase(waterRipples_.begin());
    waterRipples_.push_back({nextEventId_++, position, 0.0, std::clamp(strength, 0.2F, 1.5F)});
}

void World::attackAt(std::uint32_t id, Vec2 target, float height) {
    std::lock_guard<std::mutex> lock(mutex_);
    Character* attacker = find(id);
    if (!attacker || !attacker->appearance.male || !attacker->unzipped ||
        attacker->toiletAction != ToiletAction::None || attacker->attackCooldown > 0.0F ||
        attacker->needs.bladder < 4.0F || !std::isfinite(height) || height < 0.0F || height > 3.5F ||
        !std::isfinite(target.x) || !std::isfinite(target.y) || distance(attacker->position, target) > 20.0F) return;
    attacker->path.clear();
    attacker->pendingAttack = 0;
    attacker->yaw = std::atan2(target.x - attacker->position.x, target.y - attacker->position.y);
    attacker->attackAnim = attacker->attackCooldown = 0.48F;
    attacker->needs.bladder -= 4.0F;
    spawnBullet(*attacker, target, height, 0);
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
    projectile.toilet = character.toiletAction == ToiletAction::Piss;
    float pitch = projectile.toilet ? -0.32F : character.pitch;
    const bool targeted = projectile.toilet || character.hasAimTarget;
    Vec2 target = projectile.toilet ? toiletPosition() + Vec2{0.0F, 0.06F} : character.aimTarget;
    const float targetHeight = projectile.toilet ? kToiletWaterHeight : character.aimHeight;
    float speed = kPissSpeed;
    if (targeted) {
        pitch = pissLaunchPitch(character, target, targetHeight, pitch, &speed);
        projectile.target = target;
        projectile.targetHeight = targetHeight;
        projectile.toilet = projectile.toilet || pissWaterTarget(target, targetHeight);
    }
    if (targeted && !projectile.toilet) character.pitch = pitch;
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const Vec2 forward{std::sin(character.yaw), std::cos(character.yaw)};
    // The posed organ points the meatus along the aim line, so the droplet must
    // spawn where that hole actually is: pivot + shaft length * aim direction.
    projectile.position = character.position + forward * (kPissEmitterPivotForward + kPissEmitterShaft * cp);
    projectile.height = character.elevation + kPissEmitterPivotHeight + kPissEmitterShaft * sp;
    Vec2 direction = forward;
    if (targeted) {
        const Vec2 delta = target - projectile.position;
        if (length(delta) > 0.0001F) direction = normalized(delta);
    }
    character.pissYaw = std::atan2(direction.x, direction.y);
    character.pissPitch = pitch;
    // The renderer aims the organ by pissYaw, so re-anchor the emitter on that
    // axis; otherwise a locked target yanks the ribbon off the meatus.
    const Vec2 aim{std::sin(character.pissYaw), std::cos(character.pissYaw)};
    projectile.position = character.position + aim * (kPissEmitterPivotForward + kPissEmitterShaft * cp);
    projectile.velocity = direction * (speed * cp);
    projectile.verticalVelocity = speed * sp;
    if (projectiles_.size() >= 768) projectiles_.erase(projectiles_.begin());
    projectiles_.push_back(projectile);
}

void World::spawnBullet(Character& attacker, const Character& target) {
    const float height = target.elevation + (target.prone ? 0.24F : target.crouched ? 0.65F : 0.95F);
    spawnBullet(attacker, target.position, height, target.id);
}

void World::spawnBullet(Character& attacker, Vec2 target, float targetHeight, std::uint32_t) {
    if (inWater(target, toiletPosition())) target = bulletApproachTarget(attacker, target, targetHeight);
    const Vec2 initial{std::sin(attacker.yaw), std::cos(attacker.yaw)};
    Projectile projectile;
    projectile.id = nextProjectileId_++;
    projectile.owner = attacker.id;
    projectile.kind = ProjectileKind::Bullet;
    projectile.position = attacker.position + initial * 0.16F;
    projectile.height = attacker.elevation + 0.79F;
    const Vec2 delta = target - projectile.position;
    const float yaw = std::atan2(delta.x, delta.y);
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
    Puddle placed = placePuddle(puddles_, puddle, nextPuddleId_, obstacles_, kHalfWidth, kHalfDepth);
    if (placed.id == 0) return;
    placed.id = nextPuddleId_++;
    if (puddles_.size() >= 4096) puddles_.erase(puddles_.begin());
    puddles_.push_back(placed);
}

void World::updateProjectiles(float dt) {
    const double gameHours = dt * kGameTimeScale / 3600.0;
    const double areaPerMl = 0.000001 / kPissFilmDepth;
    const double volumeRetention = std::exp(-gameHours *
        kPissEvaporationMlPerSquareMeterGameHour * areaPerMl);
    for (Puddle& puddle : puddles_) {
        puddle.age += dt;
        const double evaporatedMl = puddle.volumeMl * (1.0 - volumeRetention);
        puddle.volumeMl = std::max(0.0, puddle.volumeMl - evaporatedMl);
        puddle.size = puddleRadiusForVolume(static_cast<float>(puddle.volumeMl));
    }
    for (WaterRipple& ripple : waterRipples_) ripple.age += dt;
    waterRipples_.erase(std::remove_if(waterRipples_.begin(), waterRipples_.end(),
        [](const WaterRipple& ripple) { return ripple.age >= 1.8; }), waterRipples_.end());
    puddles_.erase(std::remove_if(puddles_.begin(), puddles_.end(), [](const Puddle& puddle) {
        return puddle.volumeMl <= kPissMinimumVolumeMl;
    }), puddles_.end());
    const Vec2 wind = windAt(elapsed_);
    for (std::size_t index = 0; index < projectiles_.size();) {
        Projectile projectile = projectiles_[index];
        const bool bullet = projectile.kind == ProjectileKind::Bullet;
        const float drag = bullet ? 0.025F : projectile.toilet ? 0.0F : 0.16F;
        const projectile_detail::Point old = projectile_detail::point(projectile);
        const Vec2 nextVelocity = projectile.velocity + (wind - projectile.velocity) * (drag * dt);
        const float nextVertical = projectile.verticalVelocity - kProjectileGravity * dt;
        projectile_detail::Point next;
        next.x = old.x + (projectile.velocity.x + nextVelocity.x) * (0.5F * dt);
        next.z = old.z + (projectile.velocity.y + nextVelocity.y) * (0.5F * dt);
        next.y = old.y + (projectile.verticalVelocity + nextVertical) * (0.5F * dt);
        projectile.age += dt;
        const float radius = bullet ? 0.015F : 0.010F;
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
            if (victim->waterContact) rippleWater(victim->position, 1.25F);
            if (Character* attacker = find(projectile.owner)) damageCharacter(*attacker, *victim, 7.0F, true);
            erase();
            continue;
        }
        if (hit.hit) {
            const Vec2 impact{old.x + (next.x - old.x) * hit.amount, old.z + (next.z - old.z) * hit.amount};
            if (hit.toilet && inWater(impact, toiletPosition())) {
                rippleWater(impact, bullet ? 1.25F : 0.65F);
            }
            if (!bullet && !hit.absorb && !(projectile.toilet && hit.toilet)) addPuddle(hit.puddle);
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
