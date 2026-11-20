#include "client/renderer.h"

#include "client/blockbody.h"
#include "client/ui_layer.h"
#include "client/human_renderer.h"
#include "client/world_renderer_draw.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

namespace gtc::native {

// The room and the effects are drawn by their own files; the names read here as
// they always did.
using namespace world_draw;

namespace {

Vec3 unobstructedCamera(const ClientWorld& world, Vec3 target, Vec3 desired) {
    const Vec3 direction = desired - target;
    const double length = gtc::native::length(direction);
    if (length < 0.01) return desired;
    double nearest = 1.0;
    auto test = [&](Vec3 center, Vec3 half) {
        double enter = 0.0, exit = 1.0;
        const double origins[3] = {target.x, target.y, target.z};
        const double deltas[3] = {direction.x, direction.y, direction.z};
        const double centers[3] = {center.x, center.y, center.z};
        const double halves[3] = {half.x, half.y, half.z};
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(deltas[axis]) < 1e-9) {
                if (std::abs(origins[axis] - centers[axis]) > halves[axis]) return;
                continue;
            }
            double a = (centers[axis] - halves[axis] - origins[axis]) / deltas[axis];
            double b = (centers[axis] + halves[axis] - origins[axis]) / deltas[axis];
            if (a > b) std::swap(a, b);
            enter = std::max(enter, a);
            exit = std::min(exit, b);
            if (enter > exit) return;
        }
        if (enter > 0.02 && enter < nearest) nearest = enter;
    };
    for (const Obstacle& obstacle : world.obstacles) {
        if (obstacle.kind == "toilet") continue;
        test({obstacle.center.x, obstacle.height * 0.5, obstacle.center.y},
             {obstacle.halfSize.x, obstacle.height * 0.5, obstacle.halfSize.y});
    }
    test({world.toilet.x, 0.5425, world.toilet.y - 0.2445}, {0.2285, 0.20, 0.108});
    const double adjusted = std::max(0.12, nearest - 0.15 / length);
    return target + direction * adjusted;
}

Camera makeExternalCamera(const ClientWorld& world, const ClientState& state, std::uint32_t localId,
                          const ViewState& view) {
    Camera camera;
    camera.width = std::max(1, view.width);
    camera.height = std::max(1, view.height);
    camera.fovDegrees = view.aiming ? 52 : 62;
    const Character* local = state.character(localId);
    const Vec3 citizen = local ? Vec3{local->position.x, local->elevation, local->position.y} : Vec3{};
    const float yaw = local ? view.yaw : 0.0F;
    if (view.mode == ViewMode::God) {
        camera.position = {view.godX + std::sin(view.godYaw) * view.godDistance * 0.72,
                           view.godDistance * 0.82,
                           view.godZ + std::cos(view.godYaw) * view.godDistance * 0.72};
        camera.target = {view.godX, 0.0, view.godZ};
        return camera;
    }
    const double cp = std::cos(view.pitch), sp = std::sin(view.pitch);
    const Vec3 forward{std::sin(yaw) * cp, sp, std::cos(yaw) * cp};
    const Vec3 right{-std::cos(yaw), 0, std::sin(yaw)};
    const double targetHeight = local && local->prone ? 0.35 : local && local->crouched ? 0.82 : 1.15;
    camera.target = citizen + Vec3{0, targetHeight, 0} + right * 0.20;
    camera.position = camera.target - forward * view.distance;
    camera.position.x = std::clamp(camera.position.x, -world.width * 0.5 + 0.35, world.width * 0.5 - 0.35);
    camera.position.z = std::clamp(camera.position.z, -world.depth * 0.5 + 0.35, world.depth * 0.5 - 0.35);
    const Vec3 direct = unobstructedCamera(world, camera.target, camera.position);
    camera.position = direct;
    if (length(direct - camera.target) < 1.8) {
        Vec3 lifted = camera.target - forward * view.distance + Vec3{0, 1.4, 0};
        lifted.x = std::clamp(lifted.x, -world.width * 0.5 + 0.35, world.width * 0.5 - 0.35);
        lifted.z = std::clamp(lifted.z, -world.depth * 0.5 + 0.35, world.depth * 0.5 - 0.35);
        lifted = unobstructedCamera(world, camera.target, lifted);
        if (length(lifted - camera.target) > length(direct - camera.target)) camera.position = lifted;
    }
    return camera;
}


bool sameAppearance(const Appearance& a, const Appearance& b) {
    return a.male == b.male && a.skin == b.skin && a.hair == b.hair && a.hairColor == b.hairColor &&
        a.shirt == b.shirt && a.pants == b.pants && a.beard == b.beard && a.coat == b.coat &&
        a.sunglasses == b.sunglasses && a.eyeColor == b.eyeColor && a.makeup == b.makeup &&
        a.lipColor == b.lipColor && a.suit == b.suit && a.tie == b.tie && a.dress == b.dress &&
        a.beret == b.beret && a.earrings == b.earrings && a.necklace == b.necklace && a.fur == b.fur &&
        a.morphs == b.morphs;
}

bool animatedPose(const ClientState& state) {
    for (const Character& character : state.characters) {
        if (!character.onToilet && character.speed > 0.01F) return true;
    }
    return false;
}

bool samePuddles(const std::vector<Puddle>& a, const std::vector<Puddle>& b) {
    if (a.size() != b.size()) return false;
    const auto withinMillimeter = [](float x, float y) { return std::abs(static_cast<double>(x) - y) <= 0.001; };
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id || !withinMillimeter(a[i].position.x, b[i].position.x) ||
            !withinMillimeter(a[i].position.y, b[i].position.y) || !withinMillimeter(a[i].size, b[i].size) ||
            !withinMillimeter(a[i].height, b[i].height) || a[i].normal.x != b[i].normal.x ||
            a[i].normal.y != b[i].normal.y || a[i].vertical != b[i].vertical) return false;
    }
    return true;
}

bool sameGeometry(const ClientState& a, const ClientState& b) {
    if (a.characters.size() != b.characters.size()) return false;
    for (std::size_t i = 0; i < a.characters.size(); ++i) {
        const Character& left = a.characters[i];
        const Character& right = b.characters[i];
        if (left.id != right.id || !sameAppearance(left.appearance, right.appearance) ||
            left.speed != right.speed || left.elevation != right.elevation || left.charge != right.charge ||
            left.grabHeight != right.grabHeight || left.grounded != right.grounded ||
            left.pitch != right.pitch || left.pissYaw != right.pissYaw || left.pissPitch != right.pissPitch || left.attackAnim != right.attackAnim ||
            left.unzipped != right.unzipped || left.blocking != right.blocking ||
            left.crouched != right.crouched || left.prone != right.prone ||
            left.grabbing != right.grabbing || left.pissing != right.pissing || left.onToilet != right.onToilet ||
            left.toiletAction != right.toiletAction || left.path.size() != right.path.size()) return false;
        for (std::size_t p = 0; p < left.path.size(); ++p) {
            if (left.path[p].x != right.path[p].x || left.path[p].y != right.path[p].y) return false;
        }
    }
    return !(animatedPose(a) || animatedPose(b)) || a.tick == b.tick;
}

void updateObjectTransforms(GpuScene& scene, const ClientState& state) {
    for (GpuBatch& batch : scene.batches) {
        if (!batch.objectId) continue;
        const Character* character = state.character(batch.objectId);
        if (!character) continue;
        const double delta = std::atan2(std::sin(character->yaw - batch.yaw), std::cos(character->yaw - batch.yaw));
        const double c = std::cos(delta), s = std::sin(delta);
        const double rotatedX = c * batch.pivotX + s * batch.pivotZ;
        const double rotatedZ = -s * batch.pivotX + c * batch.pivotZ;
        batch.modelMatrix = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0,
            character->position.x - rotatedX, 0, character->position.y - rotatedZ, 1};
    }
}

} // namespace

bool WorldRenderer::load(const std::string& root, std::string& error) {
    root_ = root;
    // The authored block figure is the body of record. GTC_SCANBODY brings
    // back the imported scan, which the harness uses for the anatomical rig
    // checks that are written against its topology.
    if (!importedBody_ && !std::getenv("GTC_SCANBODY")) {
        male_ = blockBody(true);
        female_ = blockBody(false);
        male_.loadMorphSpecs(root);
        female_.loadMorphSpecs(root);
    } else {
        if (!male_.loadRuntime(root, true, error) || !female_.loadRuntime(root, false, error)) return false;
        if (!maleHair_.load(root, male_, error) || !femaleHair_.load(root, female_, error)) return false;
    }
    humanCache_.clear();
    gpuPuddleGeometry_.clear();
    gpuPuddleBatchCount_ = 0;
    gpuValid_ = false;
    ready_ = true;
    return true;
}

const std::vector<MorphSpec>& WorldRenderer::morphSpecs(bool male) const {
    return male ? male_.specs : female_.specs;
}

double headAngleForGaze(double gazeAngle) {
    // Small gaze shifts are ocular; beyond a comfortable 30 degrees the head
    // supplies the remainder instead of rotating the skull by the full gaze.
    return gazeAngle - std::clamp(gazeAngle, -kEyeInHeadRange, kEyeInHeadRange);
}

double clampedBodyRelativeYaw(float viewYaw, float bodyYaw) {
    constexpr double limit = 100.0 * 3.141592653589793 / 180.0;
    constexpr double fullTurn = 2.0 * 3.141592653589793;
    // Reduce only complete turns. Wrapping through atan2 first would change
    // +180 degrees into -180 degrees and snap the clamped head to the other side.
    const double continuous = std::fmod(static_cast<double>(viewYaw) - bodyYaw, fullTurn);
    return std::clamp(continuous, -limit, limit);
}

void clampViewToBody(ViewState& view, float bodyYaw, bool normalize) {
    constexpr double limit = 100.0 * 3.141592653589793 / 180.0;
    double relative = static_cast<double>(view.yaw) - bodyYaw;
    if (normalize) relative = std::atan2(std::sin(relative), std::cos(relative));
    view.yaw = bodyYaw + static_cast<float>(std::clamp(relative, -limit, limit));
}

Camera WorldRenderer::viewCamera(const ClientWorld& world, const ClientState& state,
                                 std::uint32_t localId, const ViewState& view) const {
    const Character* local = state.character(localId);
    if (view.mode != ViewMode::FirstPerson || !local) return makeExternalCamera(world, state, localId, view);
    const HumanModel& model = local->appearance.male ? male_ : female_;
    HumanRenderCache& cache = humanCache_[localId];
    prepareHuman(model, *local, cache);
    Camera camera;
    camera.width = std::max(1, view.width);
    camera.height = std::max(1, view.height);
    camera.fovDegrees = view.aiming ? 52 : 62;
    const double gazeYaw = clampedBodyRelativeYaw(view.yaw, local->yaw);
    constexpr double gazeYawLimit = 100.0 * 3.141592653589793 / 180.0;
    // Cervical motion is coupled rather than two independent maximum angles.
    // Keep the combined downward gaze clear of the posed shoulder and arm.
    constexpr double maximumDowngaze = 3.141592653589793 * 0.5;
    const double downwardLimit = maximumDowngaze - (maximumDowngaze - 0.5) *
        std::abs(gazeYaw) / gazeYawLimit;
    const double gazePitch = std::clamp(static_cast<double>(view.pitch), -downwardLimit, 1.57079633);
    const double headYaw = headAngleForGaze(gazeYaw);
    // The anatomical pose applies eye-in-head motion and solves downward
    // chin/manubrium contact from the total requested gaze angle.
    camera.position = humanEyePosition(model, cache.rest, *local, state.tick, headYaw, gazePitch);
    const double viewYaw = local->yaw + gazeYaw;
    const double cp = std::cos(gazePitch), sp = std::sin(gazePitch);
    const Vec3 forward{std::sin(viewYaw) * cp, sp, std::cos(viewYaw) * cp};
    camera.target = camera.position + forward;
    return camera;
}

void turnViewWithBody(ViewState& view, float previousBodyYaw, float currentBodyYaw) {
    const double delta = std::atan2(std::sin(currentBodyYaw - previousBodyYaw),
                                    std::cos(currentBodyYaw - previousBodyYaw));
    const double yaw = view.yaw + delta;
    view.yaw = static_cast<float>(std::atan2(std::sin(yaw), std::cos(yaw)));
}

void rotateView(ViewState& view, double mouseX, double mouseY) {
    if (view.mode == ViewMode::God) return;
    view.yaw -= static_cast<float>(mouseX * 0.0022);
    view.pitch -= static_cast<float>(mouseY * 0.0022);
    view.pitch = std::clamp(view.pitch, view.mode == ViewMode::FirstPerson ? -1.57079633F : -1.05F,
                            view.mode == ViewMode::FirstPerson ? 1.57079633F : -0.06F);
}

bool WorldRenderer::render(const ClientWorld& world, const ClientState& state, std::uint32_t localId,
                           const ViewState& view, NativeFrame& frame, std::string& error) const {
    if (!ready_) {
        error = "Native world renderer is not loaded";
        return false;
    }
    const auto profileStart = std::chrono::steady_clock::now();
    Camera camera = viewCamera(world, state, localId, view);
    camera.worldSpaceGpu = view.gpu;
    const bool rebuild = !view.gpu || !gpuValid_ || !sameGeometry(gpuGeometryState_, state) || gpuLocalId_ != localId ||
        gpuMode_ != static_cast<int>(view.mode) || gpuHighlight_ != view.highlightToilet;
    auto humanStart = profileStart;
    auto finishStart = profileStart;
    if (rebuild) {
        if (view.gpu) gpuScene_.batches.resize(gpuStaticBatchCount_);
        Rasterizer rasterizer(std::max(1, view.width), std::max(1, view.height), colorHex("#111913"), camera.focal(), view.gpu,
                              view.gpu ? std::move(gpuScene_) : GpuScene{}, &buffer_);
        rasterizer.setCamera(camera);
        static const bool subProfile = std::getenv("GTC_PROFILE2") != nullptr;
        const auto t0 = std::chrono::steady_clock::now();
        drawFloor(rasterizer, camera, world);
        const auto t1 = std::chrono::steady_clock::now();
        if (view.mode != ViewMode::God) {
            Material ceiling;
            ceiling.color = colorHex("#1b2720");
            drawBox(rasterizer, camera, {0, 3.41, 0}, {world.width, 0.04, world.depth}, ceiling);
        }
        const auto t2 = std::chrono::steady_clock::now();
        drawDressing(rasterizer, camera, world, state, view);
        const auto t3 = std::chrono::steady_clock::now();
        if (subProfile) {
            const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            std::cerr << "  floor=" << ms(t0, t1) << " ceiling=" << ms(t1, t2) << " dressing=" << ms(t2, t3) << "\n";
        }
        if (view.highlightToilet) {
            // A pool of accent colour under the thing the reticle is on, and a ring
            // round its base. The old one was a darker green painted on a green
            // floor: a highlight nobody could see is not a highlight.
            Material highlight;
            highlight.color = colorHex("#c8ff5e");
            highlight.unlit = true;
            drawEllipsoid(rasterizer, camera, Vec3{world.toilet.x, 0.025, world.toilet.y},
                          Vec3{0.82, 0.018, 0.94}, highlight, 24, 5);
            drawRing(rasterizer, camera, {world.toilet.x, 0.03, world.toilet.y}, 0.72, 0.78, highlight);
        }
        drawObstacles(rasterizer, camera, world);
        drawPaths(rasterizer, camera, state, localId);
        humanStart = std::chrono::steady_clock::now();
        for (const Character& character : state.characters) {
            rasterizer.setObject(character.id, {character.position.x, character.position.y}, character.yaw);
            const HumanModel& model = character.appearance.male ? male_ : female_;
            const HairLibrary& hair = character.appearance.male ? maleHair_ : femaleHair_;
            const bool hideHead = view.mode == ViewMode::FirstPerson && character.id == localId;
            if (character.id == localId && view.mode != ViewMode::FirstPerson && character.toiletAction == ToiletAction::None) {
                Material ring;
                ring.color = colorHex("#d7ff54");
                ring.unlit = true;
                drawRing(rasterizer, camera, {character.position.x, character.elevation + 0.028, character.position.y},
                         0.36, 0.41, ring, 24);
            }
            drawHuman(rasterizer, camera, model, hair, character, humanCache_[character.id], root_, state.tick, hideHead);
        }
        rasterizer.setObject(0, {}, 0);
        if (!view.gpu) {
            const double time = std::chrono::duration<double>(profileStart.time_since_epoch()).count();
            drawWater(rasterizer, camera, world, state, time);
            drawPuddles(rasterizer, camera, state);
            drawProjectiles(rasterizer, camera, state, humanCache_);
        }
        finishStart = std::chrono::steady_clock::now();
        if (view.gpu) {
            gpuScene_ = rasterizer.takeGpuScene();
            gpuStaticBatchCount_ = gpuScene_.batches.size();
            gpuPuddleBatchCount_ = 0;
            gpuPuddleGeometry_.clear();
            gpuScene_.revision = ++gpuRevision_;
            gpuGeometryState_ = state;
            gpuLocalId_ = localId;
            gpuMode_ = static_cast<int>(view.mode);
            gpuHighlight_ = view.highlightToilet;
            gpuValid_ = true;
        } else {
            frame = rasterizer.finish();
        }
    }
    if (view.gpu) {
        if (!samePuddles(gpuPuddleGeometry_, state.puddles)) {
            gpuScene_.batches.resize(gpuStaticBatchCount_);
            Rasterizer decals(std::max(1, view.width), std::max(1, view.height), gpuScene_.background,
                              camera.focal(), true);
            decals.setCamera(camera);
            drawPuddles(decals, camera, state);
            GpuScene cached = decals.takeGpuScene();
            const std::uint64_t revision = ++gpuProjectileRevision_;
            for (GpuBatch& batch : cached.batches) {
                if (batch.vertices.empty()) continue;
                batch.revision = revision;
                gpuScene_.batches.push_back(std::move(batch));
            }
            gpuPuddleBatchCount_ = gpuScene_.batches.size() - gpuStaticBatchCount_;
            gpuPuddleGeometry_ = state.puddles;
        }
        // Only water and flying droplets are re-uploaded each frame.
        gpuScene_.batches.resize(gpuStaticBatchCount_ + gpuPuddleBatchCount_);
        Rasterizer particles(std::max(1, view.width), std::max(1, view.height), gpuScene_.background,
                             camera.focal(), true);
        particles.setCamera(camera);
        const double time = std::chrono::duration<double>(profileStart.time_since_epoch()).count();
        drawWater(particles, camera, world, state, time);
        drawProjectiles(particles, camera, state, humanCache_);
        GpuScene dynamic = particles.takeGpuScene();
        const std::uint64_t dynamicRevision = ++gpuProjectileRevision_;
        for (GpuBatch& batch : dynamic.batches) {
            if (batch.vertices.empty()) continue;
            batch.revision = dynamicRevision;
            gpuScene_.batches.push_back(std::move(batch));
        }
        gpuScene_.width = std::max(1, view.width);
        gpuScene_.height = std::max(1, view.height);
        gpuScene_.focal = camera.focal();
        gpuScene_.viewMatrix = camera.viewMatrix();
        updateObjectTransforms(gpuScene_, state);
        // The interface goes on the device with the world: the same batches, the
        // same submission, the same image. Nothing is composited on the host and
        // nothing has to come back out of the device to be drawn over.
        if (frame.uiLayer != nullptr && !frame.uiLayer->empty()) {
            appendUiBatches(*frame.uiLayer, camera, gpuScene_);
            frame.interfaceInFrame = true;
        }
        if (!renderGpuScene(gpuScene_, frame)) {
            error = "Native GPU scene rendering failed";
            return false;
        }
    }
    const std::size_t framePixels = static_cast<std::size_t>(frame.width) * frame.height;
    // A frame that came back in window order is a finished frame: it has four
    // bytes a pixel and no coverage mask, because nothing has to blend it. Only
    // the rgb form has a mask to check.
    const bool havePixels = frame.canvasIsBgra() ||
        (frame.rgb.size() == framePixels * 3 && frame.alpha.size() == framePixels);
    if (view.gpu && !frame.presented && !havePixels) {
        error = "Native GPU scene rendering failed";
        return false;
    }
    static const bool profiling = std::getenv("GTC_PROFILE") != nullptr;
    if (profiling) {
        const auto end = std::chrono::steady_clock::now();
        const auto milliseconds = [](auto start, auto stop) {
            return std::chrono::duration<double, std::milli>(stop - start).count();
        };
        std::cerr << "profile world=" << milliseconds(profileStart, humanStart)
                  << " humans=" << milliseconds(humanStart, finishStart)
                  << " finish=" << milliseconds(finishStart, end) << " ms\n";
    }
    // Nothing is drawn over the picture here any more. The interface is one box
    // with a frame rate in it, and it belongs to the loop that owns the window,
    // not to the renderer: a renderer that paints its own HUD is a renderer whose
    // output cannot be compared to anything.
    return true;
}

} // namespace gtc::native
