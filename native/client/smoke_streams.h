#pragma once

#include "client/client.h"
#include "client/human_renderer.h"
#include "core/projectile.h"

namespace gtc::native {

inline void seedAimedToiletStream(ClientState& state, Character& character, gtc::Vec2 target) {
    character.onToilet = false;
    character.toiletAction = ToiletAction::None;
    character.unzipped = character.pissing = character.hasAimTarget = true;
    character.aimTarget = target;
    character.aimHeight = kToiletWaterHeight;
    character.yaw = std::atan2(target.x - character.position.x, target.y - character.position.y);
    float speed = kPissSpeed;
    character.pitch = pissLaunchPitch(character, target, kToiletWaterHeight, -0.9F, &speed);
    const float cp = std::cos(character.pitch), sp = std::sin(character.pitch);
    const gtc::Vec2 forward{std::sin(character.yaw), std::cos(character.yaw)};
    const gtc::Vec2 origin = character.position + forward * (0.10F + 0.06F * cp);
    const float originHeight = character.elevation + 0.79F + 0.10F * sp;
    const gtc::Vec2 direction = normalized(target - origin);
    character.pissYaw = std::atan2(direction.x, direction.y);
    character.pissPitch = character.pitch;
    const float vertical = speed * sp;
    const float flight = (vertical + std::sqrt(vertical * vertical +
        2.0F * kProjectileGravity * (originHeight - kToiletWaterHeight))) / kProjectileGravity;
    const int count = std::clamp(static_cast<int>(std::floor((flight - 0.025F) * 30.0F)) + 1, 2, 20);
    const std::uint32_t base = 9000U + character.id * 100U;
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / 30.0F;
        Projectile drop;
        drop.id = base + static_cast<std::uint32_t>(count - i);
        drop.owner = character.id;
        drop.position = origin + direction * (speed * cp * t);
        drop.height = originHeight + vertical * t - 0.5F * kProjectileGravity * t * t;
        drop.velocity = direction * (speed * cp);
        drop.verticalVelocity = vertical - kProjectileGravity * t;
        drop.age = t;
        drop.toilet = true;
        drop.target = target;
        drop.targetHeight = kToiletWaterHeight;
        state.projectiles.push_back(drop);
    }
}

inline bool npcStreamOriginMatchesPose(const HumanModel& model, Character character, double tick,
                                       const std::string& root) {
    character.ai = character.appearance.male = true;
    character.unzipped = character.pissing = true;
    character.onToilet = false;
    character.toiletAction = ToiletAction::None;
    HumanRenderCache cache;
    prepareHuman(model, character, cache);
    Camera camera;
    camera.width = camera.height = 16;
    camera.worldSpaceGpu = true;
    Rasterizer rasterizer(16, 16, Color{}, 1.0, true);
    HairLibrary hair;
    drawHuman(rasterizer, camera, model, hair, character, cache, root, tick, true);
    const Vec3 launch{std::cos(character.pissPitch) * std::sin(character.pissYaw), std::sin(character.pissPitch),
                      std::cos(character.pissPitch) * std::cos(character.pissYaw)};
    const Vec3 rootDelta = cache.genitalRoot - Vec3{character.position.x, character.elevation, character.position.y};
    const double rootX = std::cos(character.yaw) * rootDelta.x - std::sin(character.yaw) * rootDelta.z;
    if (cache.genitalTip < 0 || std::abs(rootX) > 0.01 ||
        length(cache.genitalExit - cache.posed[cache.genitalTip]) < 0.001 ||
        dot(normalized(cache.genitalExit - cache.genitalRoot), launch) < 0.99999 ||
        length(humanStreamOrigin(character, &cache, 0) - cache.genitalExit) > 1e-6) return false;
    const Vec3 tip = cache.genitalExit;
    Character moved = character;
    moved.position += {0.3F, -0.2F};
    moved.elevation += 0.1F;
    moved.yaw += 0.35F;
    const double yaw = moved.yaw - cache.poseYaw;
    const double c = std::cos(yaw), s = std::sin(yaw);
    const Vec3 expected{moved.position.x + c * (tip.x - cache.poseX) + s * (tip.z - cache.poseZ),
        tip.y + moved.elevation - cache.poseElevation,
        moved.position.y - s * (tip.x - cache.poseX) + c * (tip.z - cache.poseZ)};
    return length(humanStreamOrigin(moved, &cache, 0) - expected) < 1e-6;
}

inline bool seedToiletStreamExhibit(ClientState& state, std::uint32_t localId,
                                    const std::string& root, double tick) {
    state.projectiles.clear();
    state.waterRipples = {{9100, {0, -4.69F}, 0.12, 1.2F}, {9101, {0.04F, -4.67F}, 0.38, 0.8F}};
    for (auto& citizen : state.characters) citizen.position = {7, 5};
    Character* player = nullptr;
    Character* npc = nullptr;
    for (auto& citizen : state.characters) {
        if (citizen.id == localId) player = &citizen;
        if (citizen.ai && citizen.appearance.male && !npc) npc = &citizen;
    }
    if (!player || !npc) return false;
    player->position = {-0.55F, -3.55F};
    npc->position = {0.55F, -3.55F};
    seedAimedToiletStream(state, *player, {0, -4.69F});
    seedAimedToiletStream(state, *npc, {0, -4.69F});
    HumanModel model;
    std::string error;
    return model.loadRuntime(root, true, error) && npcStreamOriginMatchesPose(model, *npc, tick, root);
}

} // namespace gtc::native
