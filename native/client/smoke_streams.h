#pragma once

#include "client/client.h"
#include "client/human_renderer.h"
#include "client/view_controls.h"
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
    const gtc::Vec2 origin = character.position + forward * (kPissEmitterPivotForward + kPissEmitterShaft * cp);
    const float originHeight = character.elevation + kPissEmitterPivotHeight + kPissEmitterShaft * sp;
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

// Seed a visible stream that starts at the same meatus the physics uses.
inline void seedMeatusStream(ClientState& state, Character& character) {
    state.projectiles.clear();
    const float cp = std::cos(character.pitch), sp = std::sin(character.pitch);
    const gtc::Vec2 forward{std::sin(character.yaw), std::cos(character.yaw)};
    const gtc::Vec2 origin = character.position + forward * (kPissEmitterPivotForward + kPissEmitterShaft * cp);
    for (int i = 0; i < 18; ++i) {
        const float t = static_cast<float>(i) / 30.0F;
        Projectile drop;
        drop.id = 9000U + static_cast<std::uint32_t>(i);
        drop.owner = character.id;
        drop.position = origin + gtc::Vec2{0.4F * t * t, kPissSpeed * cp * t};
        drop.height = character.elevation + kPissEmitterPivotHeight + kPissEmitterShaft * sp +
            kPissSpeed * sp * t - 0.5F * kProjectileGravity * t * t;
        drop.velocity = {0, kPissSpeed * cp};
        drop.verticalVelocity = kPissSpeed * sp - kProjectileGravity * t;
        state.projectiles.push_back(drop);
    }
}

// Put the exhibit NPC at the real porcelain, exactly where beginToilet() places
// him: facing the bowl with the body pitch of a standing toilet piss.
inline void seedNpcToiletStream(ClientState& state, Character& npc) {
    state.projectiles.clear();
    seedAimedToiletStream(state, npc, gtc::Vec2{0.0F, -4.69F});
    npc.toiletAction = ToiletAction::Piss;
    npc.onToilet = false;
    npc.unzipped = false;
    npc.pitch = -0.32F;
    npc.pissPitch = pissLaunchPitch(npc, gtc::Vec2{0.0F, -4.69F}, kToiletWaterHeight, -0.32F);
}

// Stand the exhibit NPC at the porcelain and prove the ribbon begins on the
// glans there too, not only in the open floor.
inline bool seedNpcToiletExhibit(ClientState& state, const std::string& root, double tick) {
    Character* npc = nullptr;
    for (auto& citizen : state.characters)
        if (citizen.ai && citizen.appearance.male && !npc) npc = &citizen;
    if (!npc) return false;
    npc->position = {0.0F, -3.55F};
    npc->elevation = 0.0F;
    npc->yaw = 3.14159265F;
    npc->crouched = npc->prone = false;
    seedNpcToiletStream(state, *npc);
    HumanModel model;
    std::string error;
    if (!model.loadRuntime(root, true, error)) return false;
    HumanRenderCache cache;
    Camera camera;
    camera.width = camera.height = 16;
    camera.worldSpaceGpu = true;
    Rasterizer rasterizer(16, 16, Color{}, 1.0, true);
    HairLibrary hair;
    drawHuman(rasterizer, camera, model, hair, *npc, cache, root, tick, true);
    const float cp = std::cos(npc->pissPitch), sp = std::sin(npc->pissPitch);
    const gtc::Vec2 forward{std::sin(npc->pissYaw), std::cos(npc->pissYaw)};
    const gtc::Vec2 origin = npc->position + forward * (kPissEmitterPivotForward + kPissEmitterShaft * cp);
    const Vec3 physical{origin.x, npc->elevation + kPissEmitterPivotHeight + kPissEmitterShaft * sp, origin.y};
    return cache.genitalTip >= 0 && humanStreamAttached(*npc) &&
        length(humanStreamOrigin(*npc, &cache, 0) - physical) < 0.006;
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
    const bool active = character.pissing || character.toiletAction == ToiletAction::Piss;
    const double aimYaw = active ? character.pissYaw : character.yaw;
    const double aimPitch = active ? character.pissPitch : character.pitch;
    const Vec3 launch{std::cos(aimPitch) * std::sin(aimYaw), std::sin(aimPitch), std::cos(aimPitch) * std::cos(aimYaw)};
    const Vec3 rootDelta = cache.genitalRoot - Vec3{character.position.x, character.elevation, character.position.y};
    const double rootX = std::cos(character.yaw) * rootDelta.x - std::sin(character.yaw) * rootDelta.z;
    const double capOffset = length(cache.genitalExit - cache.posed[cache.genitalTip]);
    if (cache.genitalTip < 0 || std::abs(rootX) > 0.01 || capOffset < 0.001 || capOffset > 0.003 ||
        !humanStreamAttached(character) || length(cache.genitalExit - cache.genitalRoot) < 0.03 ||
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

// The physical droplet spawn and the visible meatus must be the same point at
// every aim angle; otherwise the stream appears to start in mid-air.
inline bool streamSpawnMatchesMeatus(const HumanModel& model, Character character, double tick,
                                     const std::string& root) {
    character.ai = character.appearance.male = true;
    character.onToilet = false;
    character.unzipped = character.pissing = true;
    character.toiletAction = ToiletAction::None;
    HumanRenderCache cache;
    Camera camera;
    camera.width = camera.height = 16;
    camera.worldSpaceGpu = true;
    Rasterizer rasterizer(16, 16, Color{}, 1.0, true);
    HairLibrary hair;
    for (float pitch : {-0.9F, -0.32F, 0.0F, 0.45F, 1.3F}) {
        character.pissPitch = pitch;
        character.pissYaw = character.yaw;
        drawHuman(rasterizer, camera, model, hair, character, cache, root, tick, true);
        const float cp = std::cos(pitch), sp = std::sin(pitch);
        const gtc::Vec2 forward{std::sin(character.yaw), std::cos(character.yaw)};
        const gtc::Vec2 spawn = character.position + forward * (kPissEmitterPivotForward + kPissEmitterShaft * cp);
        const Vec3 physical{spawn.x, character.elevation + kPissEmitterPivotHeight + kPissEmitterShaft * sp, spawn.y};
        // The mesh apex sits 4 mm off the sagittal midline, so allow that much
        // sideways error; the droplet radius is 10 mm. The old hip offset was
        // 50 mm off, which is what made the stream start in mid-air.
        if (length(humanStreamOrigin(character, &cache, 0) - physical) > 0.006) return false;
    }
    return true;
}

inline bool orbitKeepsControlsFrozen() {
    constexpr float east = 1.57079633F, north = 0.0F;
    ViewState view;
    view.mode = ViewMode::ThirdPerson;
    view.yaw = east;
    view.pitch = -0.2F;
    OrbitPreview orbit;
    const bool capture = orbit.begin(view, false);
    view.yaw = north;
    view.pitch = -0.8F;
    const ViewState control = orbit.controls(view);
    const gtc::Vec2 walk{std::sin(control.yaw), std::cos(control.yaw)};
    const bool frozenWalk = std::abs(walk.x - 1.0F) < 1e-5F && std::abs(walk.y) < 1e-5F;
    const bool frozenAim = std::abs(control.yaw - east) < 1e-5F && std::abs(control.pitch + 0.2F) < 1e-5F;
    const bool previewsCamera = std::abs(view.yaw - north) < 1e-5F && !control.orbitPreview;
    const bool released = orbit.end(view);
    const bool restored = released && std::abs(view.yaw - east) < 1e-6F && std::abs(view.pitch + 0.2F) < 1e-6F;
    ViewState firstPerson;
    firstPerson.mode = ViewMode::FirstPerson;
    OrbitPreview rejected;
    return capture && frozenWalk && frozenAim && previewsCamera && restored && !rejected.begin(firstPerson, false);
}

// A male NPC can start pissing without ever unzipping. If the pose cache does
// not include that flag the genitals are never articulated and the renderer
// silently falls back to the floating hip offset instead of the meatus.
inline bool npcPissingReposesGenitals(const HumanModel& model, Character character, double tick,
                                      const std::string& root) {
    character.ai = character.appearance.male = true;
    character.onToilet = false;
    character.toiletAction = ToiletAction::None;
    character.unzipped = character.pissing = false;
    HumanRenderCache cache;
    Camera camera;
    camera.width = camera.height = 16;
    camera.worldSpaceGpu = true;
    Rasterizer rasterizer(16, 16, Color{}, 1.0, true);
    HairLibrary hair;
    drawHuman(rasterizer, camera, model, hair, character, cache, root, tick, true);
    if (cache.genitalTip >= 0) return false;
    character.pissing = true;
    character.pissYaw = character.yaw;
    character.pissPitch = -0.35F;
    drawHuman(rasterizer, camera, model, hair, character, cache, root, tick, true);
    const Vec3 fallback = humanStreamOrigin(character, nullptr, character.pissPitch);
    return cache.genitalTip >= 0 && length(humanStreamOrigin(character, &cache, 0) - cache.genitalExit) < 1e-6 &&
        length(cache.genitalExit - fallback) < 0.01;
}

// The reported NPC case: ranged urine attack. The attacker is unzipped but the
// continuous `pissing` flag stays false, so attachment must not depend on it.
inline bool npcRangedAttackKeepsStreamAttached(const HumanModel& model, Character character, double tick,
                                               const std::string& root) {
    character.ai = character.appearance.male = true;
    character.onToilet = false;
    character.toiletAction = ToiletAction::None;
    character.pissing = false;
    character.unzipped = true;
    character.attackAnim = 0.32F;
    character.pitch = -0.45F;
    HumanRenderCache cache;
    Camera camera;
    camera.width = camera.height = 16;
    camera.worldSpaceGpu = true;
    Rasterizer rasterizer(16, 16, Color{}, 1.0, true);
    HairLibrary hair;
    drawHuman(rasterizer, camera, model, hair, character, cache, root, tick, true);
    Character zipped = character;
    zipped.unzipped = zipped.attackAnim = false;
    return humanStreamAttached(character) && !humanStreamAttached(zipped) && cache.genitalTip >= 0 &&
        length(humanStreamOrigin(character, &cache, character.pitch) - cache.genitalExit) < 1e-6;
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
    // Reproduce a ranged NPC attack in the exhibit, not only a toilet stream.
    npc->pissing = false;
    return model.loadRuntime(root, true, error) && npcPissingReposesGenitals(model, *npc, tick, root) &&
        npcRangedAttackKeepsStreamAttached(model, *npc, tick, root) &&
        streamSpawnMatchesMeatus(model, *npc, tick, root) &&
        npcStreamOriginMatchesPose(model, *npc, tick, root);
}

} // namespace gtc::native
