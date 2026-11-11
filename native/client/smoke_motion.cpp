#include "client/smoke_motion.h"

#include "client/blockbody.h"
#include "client/renderer.h"
#include "client/smoke_body.h"
#include "client/smoke_streams.h"
#include "vulkan/vk_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

const gtc::native::GpuScene* motionScene = nullptr;
bool captureMotionScene(const gtc::native::GpuScene& scene, gtc::native::NativeFrame& frame) {
    motionScene = &scene;
    frame.width = scene.width;
    frame.height = scene.height;
    const std::size_t pixels = static_cast<std::size_t>(scene.width) * scene.height;
    frame.rgb.assign(pixels * 3, 0);
    frame.alpha.assign(pixels, 0);
    return true;
}
}  // namespace

bool motionChecks(gtc::native::WorldRenderer& renderer, std::string& error) {
    using namespace gtc::native;
    ClientState previous, target, current; previous.tick = 40; target.tick = 42;
    gtc::Character citizen; citizen.id = 1; citizen.yaw = 3;
    previous.characters.push_back(citizen); citizen.position = {4, 2}; citizen.yaw = -3;
    target.characters.push_back(citizen);
    gtc::Projectile interpolatedDrop; interpolatedDrop.id = 9; previous.projectiles.push_back(interpolatedDrop);
    interpolatedDrop.position = {4, 2}; interpolatedDrop.height = 2; target.projectiles.push_back(interpolatedDrop);
    current = target; interpolateState(previous, target, current, 0.25);
    const double expectedYaw = 3 + std::atan2(std::sin(-6.0), std::cos(-6.0)) * 0.25;
    if (std::abs(current.tick - 40.5) > 1e-9 || std::abs(current.characters[0].position.x - 1) > 1e-6 ||
        std::abs(current.characters[0].position.y - 0.5) > 1e-6 || std::abs(current.characters[0].yaw - expectedYaw) > 1e-6 ||
        current.projectiles.size() != 1 || std::abs(current.projectiles[0].position.x - 1) > 1e-6 ||
        std::abs(current.projectiles[0].height - 0.5) > 1e-6) {
        error = "partial tick interpolation";
        return false;
    }
    ClientWorld world;
    ClientState state;
    state.tick = 1;
    state.characters.clear();
    citizen.id = 900;
    citizen.position = {0, 0};
    citizen.yaw = 0;
    state.characters.push_back(citizen);
    ViewState view;
    view.gpu = true;
    view.width = 64;
    view.height = 64;
    view.yaw = 3.14159265F;
    view.pitch = -0.34F;
    NativeFrame frame;
    setGpuRenderer(&captureMotionScene);
    const auto cleanup = [&] { setGpuRenderer(nullptr); motionScene = nullptr; };
    if (!renderer.render(world, state, citizen.id, view, frame, error) || !motionScene) { cleanup(); return false; }
    const std::uint64_t revision = motionScene->revision;
    const auto initialView = motionScene->viewMatrix;
    ++state.tick;
    if (!renderer.render(world, state, citizen.id, view, frame, error) || motionScene->revision != revision) {
        error = "idle tick rebuilt the scene";
        cleanup();
        return false;
    }
    state.characters[0].position = {1, 2};
    state.characters[0].yaw = 0.5F;
    if (!renderer.render(world, state, citizen.id, view, frame, error) || !motionScene) { cleanup(); return false; }
    if (motionScene->revision != revision || motionScene->viewMatrix == initialView) {
        error = motionScene->revision != revision ? "unexpected motion rebuild" : "camera did not follow";
        cleanup();
        return false;
    }
    const gtc::native::GpuBatch* body = nullptr;
    for (const auto& batch : motionScene->batches) if (batch.objectId == citizen.id) {
        body = &batch;
        break;
    }
    if (!body) {
        error = "missing character batch";
        cleanup();
        return false;
    }
    const double c = std::cos(0.5), s = std::sin(0.5);
    const bool matrix = std::abs(body->modelMatrix[0] - c) < 1e-9 &&
        std::abs(body->modelMatrix[2] + s) < 1e-9 && std::abs(body->modelMatrix[8] - s) < 1e-9 &&
        std::abs(body->modelMatrix[12] - 1) < 1e-9 && std::abs(body->modelMatrix[14] - 2) < 1e-9;
    if (!matrix) {
        error = "character model transform";
        cleanup();
        return false;
    }
    const std::uint64_t idleRevision = motionScene->revision;
    state.characters[0].speed = 1;
    ++state.tick;
    if (!renderer.render(world, state, citizen.id, view, frame, error) || motionScene->revision == idleRevision) {
        error = "walk pose did not rebuild";
        cleanup();
        return false;
    }
    state.characters[0].speed = 0;
    gtc::Projectile drop;
    drop.id = 1;
    drop.height = 1;
    drop.verticalVelocity = gtc::kPissSpeed;
    state.projectiles = {drop};
    if (!renderer.render(world, state, citizen.id, view, frame, error)) { cleanup(); return false; }
    const std::uint64_t retainedRevision = motionScene->revision;
    const auto projectileRevision = [] {
        for (const auto& batch : motionScene->batches) if (batch.revision) return batch.revision;
        return std::uint64_t{0};
    };
    const std::uint64_t oldProjectileRevision = projectileRevision();
    state.projectiles[0].height = 1.2F;
    if (!renderer.render(world, state, citizen.id, view, frame, error) ||
        motionScene->revision != retainedRevision || projectileRevision() == oldProjectileRevision) {
        error = "projectile motion rebuilt the retained world";
        cleanup();
        return false;
    }
    state.projectiles.clear(); gtc::Puddle stain; stain.vertical = true; stain.normal = {0, 1}; state.puddles = {stain};
    if (!renderer.render(world, state, citizen.id, view, frame, error)) { cleanup(); return false; }
    const auto decalRevision = [] { for (const auto& batch : motionScene->batches) if (batch.material.kind == Material::Kind::Puddle) return batch.revision; return std::uint64_t{0}; };
    const std::uint64_t oldDecalRevision = decalRevision(); ++state.tick;
    if (!renderer.render(world, state, citizen.id, view, frame, error) || decalRevision() != oldDecalRevision) { error = "idle frame rebuilt decals"; cleanup(); return false; }
    state.puddles[0].size = 0.2F;
    if (!renderer.render(world, state, citizen.id, view, frame, error) || decalRevision() == oldDecalRevision ||
        motionScene->revision != retainedRevision) { error = "decal update rebuilt retained world"; cleanup(); return false; }
    state.puddles.clear(); view.mode = ViewMode::FirstPerson;
    view.pitch = 0;
    view.yaw = state.characters[0].yaw;
    if (!renderer.render(world, state, citizen.id, view, frame, error)) { cleanup(); return false; }
    const Camera eye = renderer.viewCamera(world, state, citizen.id, view);
    const auto& m = motionScene->viewMatrix;
    const auto transform = [&](Vec3 point) {
        return Vec3{m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
                    m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
                    m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]};
    };
    const Vec3 eyeView = transform(eye.position);
    const Vec3 targetView = transform(eye.target);
    HumanModel eyeModel;
    if (!eyeModel.loadRuntime(".", true, error)) { cleanup(); return false; }
    HumanRenderCache eyeCache;
    prepareHuman(eyeModel, state.characters[0], eyeCache);
    const Vec3 attachedEye = humanEyePosition(eyeModel, eyeCache.rest, state.characters[0], state.tick);
    bool torsoRetained = false;
    for (const auto& batch : motionScene->batches) if (batch.objectId == citizen.id) {
        for (const auto& vertex : batch.vertices) {
            if (vertex.y > 1.10 && vertex.y < 1.40) torsoRetained = true;
        }
    }
    const auto expectedEye = [&](const gtc::Character& character, double headYaw = 0, double headPitch = 0) {
        HumanRenderCache cache;
        prepareHuman(eyeModel, character, cache);
        return humanEyePosition(eyeModel, cache.rest, character, state.tick, headYaw, headPitch);
    };
    state.characters[0].onToilet = true;
    state.characters[0].toiletAction = gtc::ToiletAction::Shit;
    ++state.tick;
    if (!renderer.render(world, state, citizen.id, view, frame, error)) { cleanup(); return false; }
    const Camera seatedEye = renderer.viewCamera(world, state, citizen.id, view);
    const Vec3 expectedSeatedEye = expectedEye(state.characters[0]);
    const bool seated = length(seatedEye.position - expectedSeatedEye) < 1e-6 &&
        length(transform(seatedEye.position)) < 1e-8 && seatedEye.position.y < eye.position.y;
    // The real 381 mm seat lowers the posed eyes instead of using the old
    // oversized 785 mm seat or a fixed root offset.
    ViewState neckView = view;
    neckView.yaw = state.characters[0].yaw + 3.0F;
    const Camera limited = renderer.viewCamera(world, state, citizen.id, neckView);
    const Vec3 limitedForward = normalized(limited.target - limited.position);
    const double limitedYaw = std::atan2(limitedForward.x, limitedForward.z);
    const double bodyYaw = state.characters[0].yaw;
    const double neckAngle = std::abs(std::atan2(std::sin(limitedYaw - bodyYaw), std::cos(limitedYaw - bodyYaw)));
    constexpr double neckLimit = 100.0 * 3.141592653589793 / 180.0;
    const Vec3 expectedNeckEye = expectedEye(state.characters[0], headAngleForGaze(neckLimit), 0);
    const bool neck = std::abs(neckAngle - neckLimit) < 1e-4 &&
        length(limited.position - expectedNeckEye) < 1e-6 &&
        length(expectedNeckEye - expectedSeatedEye) > 0.01;
    ViewState pitchView = view;
    pitchView.pitch = -2.0F;
    const Camera pitched = renderer.viewCamera(world, state, citizen.id, pitchView);
    constexpr double pitchDown = -3.141592653589793 * 0.5;
    const Vec3 expectedPitchEye = expectedEye(state.characters[0], 0, pitchDown);
    const Vec3 pitchForward = normalized(pitched.target - pitched.position);
    const Vec3 bodyForward{std::sin(bodyYaw), 0, std::cos(bodyYaw)};
    const double forwardTravel = dot(expectedPitchEye - expectedSeatedEye, bodyForward);
    const bool pitch = length(pitched.position - expectedPitchEye) < 1e-6 && forwardTravel > 1e-6 &&
        length(expectedPitchEye - expectedSeatedEye) > 0.01 && std::abs(pitchForward.y - std::sin(pitchDown)) < 1e-6;
    ViewState extremeView = neckView;
    extremeView.pitch = -1.5F;
    const Camera extreme = renderer.viewCamera(world, state, citizen.id, extremeView);
    constexpr double extremePitchDown = -0.5;
    const Vec3 expectedExtremeEye = expectedEye(state.characters[0], headAngleForGaze(neckLimit),
                                                extremePitchDown);
    const Vec3 extremeForward = normalized(extreme.target - extreme.position);
    double armClearance = 1000;
    for (const auto& batch : motionScene->batches) if (batch.objectId == citizen.id) {
        for (const auto& vertex : batch.vertices) {
            armClearance = std::min(armClearance, length(extreme.position - Vec3{vertex.x, vertex.y, vertex.z}));
        }
    }
    ViewState extremeUpView = neckView;
    extremeUpView.pitch = 1.5F;
    const Camera extremeUp = renderer.viewCamera(world, state, citizen.id, extremeUpView);
    const Vec3 expectedExtremeUpEye = expectedEye(state.characters[0], headAngleForGaze(neckLimit), 1.5);
    double upwardClearance = 1000;
    for (const auto& batch : motionScene->batches) if (batch.objectId == citizen.id) {
        for (const auto& vertex : batch.vertices) {
            upwardClearance = std::min(upwardClearance, length(extremeUp.position - Vec3{vertex.x, vertex.y, vertex.z}));
        }
    }
    const bool clearance = length(extreme.position - expectedExtremeEye) < 1e-6 && armClearance > 0.04 &&
        std::abs(extremeForward.y - std::sin(extremePitchDown)) < 1e-6 &&
        length(extremeUp.position - expectedExtremeUpEye) < 1e-6 && upwardClearance > 0.04;
    ClientState standingState = state;
    standingState.characters[0].onToilet = false;
    standingState.characters[0].toiletAction = gtc::ToiletAction::None;
    standingState.characters[0].speed = 0;
    ViewState neutralStandingView = view;
    neutralStandingView.yaw = static_cast<float>(bodyYaw);
    neutralStandingView.pitch = 0;
    const Camera neutralStanding = renderer.viewCamera(world, standingState, citizen.id, neutralStandingView);
    HumanRenderCache corneaCache;
    HumanModel posedEyeModel;
    if (!posedEyeModel.loadRuntime(".", true, error)) { cleanup(); return false; }
    prepareHuman(posedEyeModel, standingState.characters[0], corneaCache);
    const gtc::Character& standingCitizen = standingState.characters[0];
    Camera jointCamera = neutralStanding;
    jointCamera.worldSpaceGpu = true;
    Rasterizer jointRasterizer(1, 1, Color{}, 1.0, true);
    HairLibrary jointHair;
    drawHuman(jointRasterizer, jointCamera, posedEyeModel, jointHair, standingCitizen, corneaCache,
              ".", standingState.tick, true);
    const Vec3 posedEyeJoint = (jointPosition(corneaCache.posed, posedEyeModel.eyeSource[0]) +
                                jointPosition(corneaCache.posed, posedEyeModel.eyeSource[1])) * 0.5;
    bool relaxedArms = true;
    for (const char* side : {"L", "R"}) {
        const std::string wristName = std::string("wrist.") + side;
        const std::string hipName = std::string("upperleg01.") + side;
        const auto wrist = std::find_if(posedEyeModel.rigBones.begin(), posedEyeModel.rigBones.end(),
            [&](const RigBone& bone) { return bone.name == wristName; });
        const auto hip = std::find_if(posedEyeModel.rigBones.begin(), posedEyeModel.rigBones.end(),
            [&](const RigBone& bone) { return bone.name == hipName; });
        if (wrist == posedEyeModel.rigBones.end() || hip == posedEyeModel.rigBones.end()) {
            relaxedArms = false;
            continue;
        }
        const Vec3 wristPoint = jointPosition(corneaCache.posed, wrist->head);
        const Vec3 hipPoint = jointPosition(corneaCache.posed, hip->head);
        const double forward = dot(wristPoint - hipPoint, bodyForward);
        relaxedArms = relaxedArms && forward > -0.15 && forward < 0.16 &&
            wristPoint.y < hipPoint.y + 0.02 && wristPoint.y > hipPoint.y - 0.35;
    }
    constexpr double eyeRange = 30.0 * 3.141592653589793 / 180.0;
    const bool eyeJoint = length(neutralStanding.position - posedEyeJoint) < 1e-6;
    const bool gazeSplit = std::abs(headAngleForGaze(0.2)) < 1e-9 &&
        std::abs(headAngleForGaze(neckLimit) - (neckLimit - eyeRange)) < 1e-9 &&
        std::abs(headAngleForGaze(1.0) - (1.0 - eyeRange)) < 1e-9;
    gtc::Character crouchedCitizen = standingCitizen;
    crouchedCitizen.crouched = true;
    const Vec3 crouchedEye = expectedEye(crouchedCitizen);
    gtc::Character proneCitizen = standingCitizen;
    proneCitizen.prone = true;
    const Vec3 proneEye = expectedEye(proneCitizen);
    gtc::Character elevatedCitizen = standingCitizen;
    elevatedCitizen.elevation = 0.5F;
    const Vec3 elevatedEye = expectedEye(elevatedCitizen);
    const bool stanceEyes = crouchedEye.y < neutralStanding.position.y - 0.20 && proneEye.y < 0.70 &&
        length(Vec3{proneEye.x - neutralStanding.position.x, 0, proneEye.z - neutralStanding.position.z}) > 0.35 &&
        length((elevatedEye - neutralStanding.position) - Vec3{0, 0.5, 0}) < 1e-6;
    gtc::Character scaledCitizen = standingCitizen;
    scaledCitizen.position = {0, 0};
    scaledCitizen.yaw = 0;
    HumanRenderCache similarityCache;
    prepareHuman(eyeModel, scaledCitizen, similarityCache);
    double canonicalBottom = 1e100, canonicalTop = -1e100;
    for (const Triangle& triangle : eyeModel.triangles) for (int index : triangle.v) {
        canonicalBottom = std::min(canonicalBottom, similarityCache.rest[index].y);
        canonicalTop = std::max(canonicalTop, similarityCache.rest[index].y);
    }
    const bool defaultHeight = std::abs((canonicalTop - canonicalBottom) * kHumanHeightScale - 1.7) < 1e-6;
    const HumanGazePose baseContact = humanGazePose(eyeModel, similarityCache.rest, scaledCitizen,
                                                     state.tick, 0, pitchDown);
    const HumanGazePose baseNeutralPose = humanGazePose(eyeModel, similarityCache.rest, scaledCitizen, state.tick);
    const Vec3 baseNeutral = baseNeutralPose.eye;
    const Vec3 baseDown = baseContact.eye;
    const double expectedForward = length(baseNeutralPose.eye - baseNeutralPose.chin);
    const bool fullChain = length(baseContact.chin - baseContact.manubrium) < 1e-6 &&
        length((baseContact.eye - baseContact.manubrium) - Vec3{0, 0, expectedForward}) < 1e-6 &&
        std::abs(expectedForward - 0.1172) < 0.001 && baseDown.y < baseNeutral.y && baseDown.z > baseNeutral.z;
    bool heightScaling = true;
    for (double scale : {0.75, 1.30}) {
        std::vector<Vec3> scaledRest;
        scaledRest.reserve(similarityCache.rest.size());
        for (const Vec3& point : similarityCache.rest) scaledRest.push_back(point * scale);
        const Vec3 scaledNeutral = humanEyePosition(eyeModel, scaledRest, scaledCitizen, state.tick);
        const HumanGazePose scaledContact = humanGazePose(eyeModel, scaledRest, scaledCitizen,
                                                          state.tick, 0, pitchDown);
        const Vec3 scaledDown = scaledContact.eye;
        heightScaling = heightScaling && length(scaledNeutral - baseNeutral * scale) < 1e-6 &&
            length(scaledDown - baseDown * scale) < 1e-6 &&
            length(scaledContact.chin - scaledContact.manubrium) < 1e-6 &&
            std::abs(scaledContact.eye.y - scaledContact.manubrium.y) < 1e-9 &&
            length((scaledDown - scaledNeutral) - (baseDown - baseNeutral) * scale) < 1e-6;
    }
    HumanModel femaleEyeModel;
    if (!femaleEyeModel.loadRuntime(".", false, error)) { cleanup(); return false; }
    bool morphContacts = true;
    for (HumanModel* model : {&eyeModel, &femaleEyeModel}) {
        for (int spec = -1; spec < static_cast<int>(model->specs.size()); ++spec) {
            for (int endpoint = 0; endpoint < (spec < 0 ? 1 : 2); ++endpoint) {
                gtc::Character contactCitizen = scaledCitizen;
                contactCitizen.appearance.male = model == &eyeModel;
                if (spec >= 0) contactCitizen.appearance.morphs[spec] = endpoint ?
                    model->specs[spec].maximum : model->specs[spec].minimum;
                HumanRenderCache contactCache;
                prepareHuman(*model, contactCitizen, contactCache);
                const HumanGazePose neutral = humanGazePose(*model, contactCache.rest, contactCitizen, state.tick);
                const HumanGazePose contact = humanGazePose(*model, contactCache.rest, contactCitizen,
                                                            state.tick, 0, pitchDown);
                const Vec3 expectedOffset{0, 0, length(neutral.eye - neutral.chin)};
                morphContacts = morphContacts && length(contact.chin - contact.manubrium) < 1e-6 &&
                    length((contact.eye - contact.manubrium) - expectedOffset) < 1e-6 &&
                    contact.eye.z > neutral.eye.z && contact.eye.y < neutral.eye.y;
            }
        }
    }
    ViewState standingView = view;
    standingView.yaw = static_cast<float>(bodyYaw);
    standingView.pitch = -2.0F;
    if (!renderer.render(world, standingState, citizen.id, standingView, frame, error)) { cleanup(); return false; }
    const Camera standingCamera = renderer.viewCamera(world, standingState, citizen.id, standingView);
    ClientState visibilityState = standingState;
    visibilityState.characters[0].toiletAction = gtc::ToiletAction::Piss;
    visibilityState.characters[0].pissing = true;
    const Camera visibilityCamera = renderer.viewCamera(world, visibilityState, citizen.id, standingView);
    if (!bodyVisibility(eyeModel, visibilityState.characters[0], visibilityCamera, static_cast<std::uint64_t>(visibilityState.tick), error)) { cleanup(); return false; }
    ViewState tpsView = standingView;
    tpsView.mode = ViewMode::ThirdPerson;
    tpsView.pitch = -0.25F;
    const Camera tpsCamera = renderer.viewCamera(world, standingState, citizen.id, tpsView);
    gtc::Character tpsCitizen = standingState.characters[0];
    if (!genitalMode(eyeModel, tpsCitizen, tpsCamera, static_cast<std::uint64_t>(standingState.tick), false, false, error)) {
        cleanup(); return false;
    }
    tpsCitizen.unzipped = true;
    if (!genitalMode(eyeModel, tpsCitizen, tpsCamera, static_cast<std::uint64_t>(standingState.tick), true, false, error)) {
        cleanup(); return false;
    }
    int visiblePelvis = 0, visibleLegs = 0;
    double standingClearance = 1000;
    for (const auto& batch : motionScene->batches) if (batch.objectId == citizen.id) {
        for (const auto& vertex : batch.vertices) {
            const Vec3 worldPoint{vertex.x, vertex.y, vertex.z};
            standingClearance = std::min(standingClearance, length(standingCamera.position - worldPoint));
            const Vec3 point = transform(worldPoint);
            if (point.z <= 0.035) continue;
            const double x = motionScene->width * 0.5 + motionScene->focal * point.x / point.z;
            const double y = motionScene->height * 0.5 - motionScene->focal * point.y / point.z;
            if (x < 3 || x >= motionScene->width - 3 || y < 3 || y >= motionScene->height - 3) continue;
            if (vertex.y > 0.72 && vertex.y < 1.05) ++visiblePelvis;
            if (vertex.y > 0.05 && vertex.y < 0.65) ++visibleLegs;
        }
    }
    const Vec3 standingForward = normalized(standingCamera.target - standingCamera.position);
    const Vec3 basisX = standingCamera.viewDirection({1, 0, 0});
    const Vec3 basisY = standingCamera.viewDirection({0, 1, 0});
    const Vec3 basisZ = standingCamera.viewDirection({0, 0, 1});
    const bool verticalBasis = std::abs(length(basisX) - 1) < 1e-6 &&
        std::abs(length(basisY) - 1) < 1e-6 && std::abs(length(basisZ) - 1) < 1e-6 &&
        std::abs(dot(basisX, basisY)) < 1e-6 && std::abs(dot(basisX, basisZ)) < 1e-6 &&
        std::abs(dot(basisY, basisZ)) < 1e-6;
    const bool steadyLens = std::abs(neutralStanding.fovDegrees - 62.0) < 1e-9 &&
        std::abs(standingCamera.fovDegrees - neutralStanding.fovDegrees) < 1e-9;
    ViewState aimView = standingView; aimView.aiming = true;
    const bool aimLens = std::abs(renderer.viewCamera(world, standingState, citizen.id, aimView).fovDegrees - 52.0) < 1e-9;
    const bool lowerBody = steadyLens && visiblePelvis > 8 && visibleLegs > 8 && standingClearance > 0.04 &&
        verticalBasis && std::abs(standingForward.y - std::sin(pitchDown)) < 1e-6;
    const auto sceneClearance = [&](const Camera& camera) {
        double nearest = 1000;
        for (const auto& batch : motionScene->batches) if (batch.objectId == citizen.id) {
            for (const auto& vertex : batch.vertices) {
                nearest = std::min(nearest, length(camera.position - Vec3{vertex.x, vertex.y, vertex.z}));
            }
        }
        return nearest;
    };
    const int headHeight = eyeModel.specIndex("headHeight");
    bool shaped = headHeight >= 0;
    double shortY = 0, tallY = 0, leastMorphClearance = 1000;
    std::string leastMorphStage;
    for (int value : {-100, 100}) {
        if (!shaped) break;
        ClientState shapeState = state;
        gtc::Character& character = shapeState.characters[0];
        character.appearance.morphs[headHeight] = value;
        const Camera seatedShape = renderer.viewCamera(world, shapeState, citizen.id, view);
        shaped = length(seatedShape.position - expectedEye(character)) < 1e-6;
        ViewState shapeExtremeView = neckView;
        shapeExtremeView.pitch = -1.5F;
        if (!renderer.render(world, shapeState, citizen.id, shapeExtremeView, frame, error)) {
            cleanup();
            return false;
        }
        const Camera seatedExtreme = renderer.viewCamera(world, shapeState, citizen.id, shapeExtremeView);
        const double seatedClearance = sceneClearance(seatedExtreme);
        if (seatedClearance < leastMorphClearance) {
            leastMorphClearance = seatedClearance;
            leastMorphStage = "seated " + std::to_string(value);
        }
        shaped = shaped && seatedClearance > 0.04;
        character.onToilet = false;
        character.toiletAction = gtc::ToiletAction::None;
        const Camera standingShape = renderer.viewCamera(world, shapeState, citizen.id, view);
        shaped = shaped && length(standingShape.position - expectedEye(character)) < 1e-6;
        if (!renderer.render(world, shapeState, citizen.id, standingView, frame, error)) {
            cleanup();
            return false;
        }
        const Camera standingExtreme = renderer.viewCamera(world, shapeState, citizen.id, standingView);
        const double standingMorphClearance = sceneClearance(standingExtreme);
        if (standingMorphClearance < leastMorphClearance) {
            leastMorphClearance = standingMorphClearance;
            leastMorphStage = "standing " + std::to_string(value);
        }
        shaped = shaped && standingMorphClearance > 0.04;
        if (value < 0) shortY = standingShape.position.y;
        else tallY = standingShape.position.y;
    }
    shaped = shaped && std::abs(tallY - shortY) > 0.001;
    const double leastHeightClearance = 0.75 * std::min({armClearance, upwardClearance, standingClearance, leastMorphClearance});
    const bool heightClearance = leastHeightClearance > 0.035;
    ViewState mouse;
    mouse.mode = ViewMode::FirstPerson;
    mouse.pitch = 0;
    rotateView(mouse, 10, -20);
    ViewState turned;
    turned.mode = ViewMode::FirstPerson;
    turned.yaw = 3.141592653589793F + 0.35F;
    turned.pitch = 0;
    turnViewWithBody(turned, 3.141592653589793F, 0.0F);
    ClientState turnState = standingState;
    turnState.characters[0].yaw = 0;
    turnState.characters[0].onToilet = true;
    turnState.characters[0].toiletAction = gtc::ToiletAction::Sit;
    const Camera turnedCamera = renderer.viewCamera(world, turnState, citizen.id, turned);
    const Vec3 turnedForward = normalized(turnedCamera.target - turnedCamera.position);
    const double turnedYaw = std::atan2(turnedForward.x, turnedForward.z);
    const bool bodyTurn = std::abs(turned.yaw - 0.35F) < 1e-6 && std::abs(turnedYaw - 0.35) < 1e-5;
    constexpr double yawLimit = 100.0 * 3.141592653589793 / 180.0;
    ViewState leftBoundary = turned;
    leftBoundary.yaw = 3.141592653589793F + 0.01F;
    const Camera leftCamera = renderer.viewCamera(world, turnState, citizen.id, leftBoundary);
    const Vec3 leftForward = normalized(leftCamera.target - leftCamera.position);
    const double leftYaw = std::atan2(leftForward.x, leftForward.z);
    ViewState rightBoundary = turned;
    rightBoundary.yaw = -3.141592653589793F - 0.01F;
    const Camera rightCamera = renderer.viewCamera(world, turnState, citizen.id, rightBoundary);
    const Vec3 rightForward = normalized(rightCamera.target - rightCamera.position);
    const double rightYaw = std::atan2(rightForward.x, rightForward.z);
    clampViewToBody(leftBoundary, 0);
    clampViewToBody(rightBoundary, 0);
    ViewState largeDelta = turned;
    largeDelta.yaw = 25.0F;
    clampViewToBody(largeDelta, 0);
    ViewState fullTurn = turned;
    fullTurn.yaw = 2.0F * 3.141592653589793F + 0.35F;
    clampViewToBody(fullTurn, 0, true);
    const bool yawBoundary = std::abs(leftYaw - yawLimit) < 1e-5 && std::abs(rightYaw + yawLimit) < 1e-5 &&
        std::abs(leftBoundary.yaw - yawLimit) < 1e-6 && std::abs(rightBoundary.yaw + yawLimit) < 1e-6 &&
        std::abs(largeDelta.yaw - yawLimit) < 1e-6 && std::abs(fullTurn.yaw - 0.35F) < 1e-6;
    // The crosshair used to be checked here. It is gone with the rest of the
    // overlay: what is drawn over a frame is the counter now, and that is checked
    // as pixels by its own executable rather than by looking at the middle of a
    // 3D frame for a shape that no longer exists.
    const bool eyeMatrix = length(eyeView) < 1e-8 && std::abs(targetView.z - 1) < 1e-8;
    const bool attached = length(eye.position - attachedEye) < 1e-6 && torsoRetained;
    const bool look = std::abs(mouse.yaw + 0.022F) < 1e-6F && std::abs(mouse.pitch - 0.044F) < 1e-6F;
    cleanup();
    if (!eyeMatrix) error = "first-person eye matrix";
    else if (!attached) error = "first-person body attachment";
    else if (!eyeJoint) error = "first-person actual eye-joint attachment";
    else if (!gazeSplit) error = "first-person biological gaze split";
    else if (!stanceEyes) error = "crouch/prone/elevated eye attachment, crouchY=" + std::to_string(crouchedEye.y) +
        ", proneY=" + std::to_string(proneEye.y) + ", proneTravel=" +
        std::to_string(length(Vec3{proneEye.x - neutralStanding.position.x, 0, proneEye.z - neutralStanding.position.z})) +
        ", elevatedDelta=" + std::to_string(length((elevatedEye - neutralStanding.position) - Vec3{0, 0.5, 0}));
    else if (!relaxedArms) error = "relaxed idle wrists must hang beside the thighs";
    else if (!fullChain) error = "first-person 117.2 mm forward, zero-vertical eye/manubrium geometry";
    else if (!defaultHeight) error = "default citizen height must be exactly 1.7 metres";
    else if (!heightScaling) error = "first-person neck motion must scale with body height";
    else if (!morphContacts) error = "first-person chin/manubrium contact across body morphs";
    else if (!seated) error = "first-person sit-down head attachment";
    else if (!neck) error = "first-person human neck limit";
    else if (!pitch) error = "first-person neck pitch pivot, forward travel=" + std::to_string(forwardTravel);
    else if (!clearance) error = "first-person arm clearance";
    else if (!steadyLens) error = "first-person camera FOV changed with gaze";
    else if (!aimLens) error = "explicit RMB aim does not apply modest zoom";
    else if (!lowerBody) error = "standing male lower-body visibility, clearance=" + std::to_string(standingClearance) +
        ", pelvis=" + std::to_string(visiblePelvis) + ", legs=" + std::to_string(visibleLegs);
    else if (!shaped) error = "first-person morphed head attachment/clearance, " + leastMorphStage +
        "=" + std::to_string(leastMorphClearance);
    else if (!heightClearance) error = "short-character near-plane clearance=" + std::to_string(leastHeightClearance);
    else if (!bodyTurn) error = "first-person camera did not follow seated body yaw";
    else if (!yawBoundary) error = "first-person head yaw flipped across the 180-degree boundary";
    else if (!look) error = "first-person look input";
    return eyeMatrix && attached && eyeJoint && gazeSplit && stanceEyes && relaxedArms && fullChain && defaultHeight && heightScaling && morphContacts && seated && neck && pitch && clearance &&
        lowerBody && shaped && heightClearance && bodyTurn && yawBoundary && look;
}
