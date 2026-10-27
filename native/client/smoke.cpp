#include "client/client.h"
#include "client/renderer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

gtc::native::Vec3 jointPosition(const std::vector<gtc::native::Vec3>& points, const std::vector<int>& ids) {
    gtc::native::Vec3 result;
    for (int id : ids) result += points.at(id);
    return result / static_cast<double>(ids.size());
}

struct BodyHit {
    double distance = 1e100;
    int triangle = -1;
    bool genital = false;
};

BodyHit bodyHit(const gtc::native::HumanModel& model, const std::vector<gtc::native::Vec3>& posed,
                gtc::native::Vec3 origin, gtc::native::Vec3 target) {
    using namespace gtc::native;
    BodyHit hit;
    const Vec3 direction = normalized(target - origin);
    auto cast = [&](const std::vector<Triangle>& triangles, bool genital) {
        for (std::size_t index = 0; index < triangles.size(); ++index) {
            const Triangle& triangle = triangles[index];
            const Vec3 a = posed[triangle.v[0]], b = posed[triangle.v[1]], c = posed[triangle.v[2]];
            const Vec3 e1 = b - a, e2 = c - a, p = cross(direction, e2);
            const double determinant = dot(e1, p);
            if (std::abs(determinant) < 1e-10) continue;
            const double inverse = 1.0 / determinant;
            const Vec3 s = origin - a;
            const double u = dot(s, p) * inverse;
            if (u < -1e-7 || u > 1.0000001) continue;
            const Vec3 q = cross(s, e1);
            const double v = dot(direction, q) * inverse;
            if (v < -1e-7 || u + v > 1.0000001) continue;
            const double distance = dot(e2, q) * inverse;
            if (distance > 1e-6 && distance < hit.distance) hit = {distance, static_cast<int>(index), genital};
        }
    };
    cast(model.triangles, false);
    cast(model.genitalTriangles, true);
    return hit;
}

int submittedGenital(const gtc::native::GpuScene& scene, const gtc::native::HumanRenderCache& cache,
                     const std::vector<int>& ids) {
    using namespace gtc::native;
    int submitted = 0;
    for (int id : ids) {
        bool found = false;
        const Vec3 point = cache.posed[id];
        for (const GpuBatch& batch : scene.batches) {
            if (batch.material.kind != Material::Kind::Skin) continue;
            for (const GpuVertex& vertex : batch.vertices) {
                if (std::abs(vertex.x - point.x) < 1e-6 && std::abs(vertex.y - point.y) < 1e-6 &&
                    std::abs(vertex.z - point.z) < 1e-6) { found = true; break; }
            }
            if (found) break;
        }
        if (found) ++submitted;
    }
    return submitted;
}

bool bodyVisibility(const gtc::native::HumanModel& model, const gtc::Character& character,
                    const gtc::native::Camera& camera, std::uint64_t tick, std::string& error) {
    using namespace gtc::native;
    HumanRenderCache cache;
    prepareHuman(model, character, cache);
    Camera geometryCamera = camera;
    geometryCamera.worldSpaceGpu = true;
    Rasterizer rasterizer(camera.width, camera.height, Color{}, camera.focal(), true);
    HairLibrary hair;
    drawHuman(rasterizer, geometryCamera, model, hair, character, cache, ".", tick, true);
    const GpuScene submittedScene = rasterizer.takeGpuScene();
    std::vector<int> genital;
    for (const Triangle& triangle : model.genitalTriangles) for (int id : triangle.v) genital.push_back(id);
    std::sort(genital.begin(), genital.end());
    genital.erase(std::unique(genital.begin(), genital.end()), genital.end());
    if (genital.empty() || submittedGenital(submittedScene, cache, genital) != static_cast<int>(genital.size())) {
        error = "male FPS genital geometry submission";
        return false;
    }
    const std::vector<Vec3> firstPose = cache.posed;
    Rasterizer retained(camera.width, camera.height, Color{}, camera.focal(), true);
    drawHuman(retained, geometryCamera, model, hair, character, cache, ".", tick, true);
    for (int id : genital) {
        if (length(firstPose[id] - cache.posed[id]) > 1e-9) {
            error = "cached piss pose drift";
            return false;
        }
    }
    int inside = 0, visible = 0;
    for (int id : genital) {
        const Vec3 point = camera.view(cache.posed[id]);
        const double x = camera.width * 0.5 + camera.focal() * point.x / point.z;
        const double y = camera.height * 0.5 - camera.focal() * point.y / point.z;
        if (point.z > 0.035 && x >= 0 && x < camera.width && y >= 0 && y < camera.height) ++inside;
        const BodyHit hit = bodyHit(model, cache.posed, camera.position, cache.posed[id]);
        if (hit.genital && std::abs(hit.distance - length(cache.posed[id] - camera.position)) < 1e-4) ++visible;
    }
    if (inside * 2 < static_cast<int>(genital.size()) || visible * 4 < static_cast<int>(genital.size())) {
        error = "standing male genital visibility";
        return false;
    }
    double sole = 1e100;
    for (std::size_t id = 0; id < cache.rest.size(); ++id) {
        if (cache.rest[id].y > 0.18) continue;
        double support = 0;
        for (const auto& [bone, weight] : model.rigWeights[id]) {
            if (bone < 0 || static_cast<std::size_t>(bone) >= model.rigBones.size()) continue;
            const std::string& name = model.rigBones[bone].name;
            if (name.rfind("foot.", 0) == 0 || name.rfind("toe", 0) == 0) support += weight;
        }
        if (support > 0.25) sole = std::min(sole, cache.posed[id].y);
    }
    if (sole > 1e90 || std::abs(sole) > 1e-6) {
        error = "standing idle sole contact";
        return false;
    }
    auto visibleBone = [&](const char* name) {
        const auto it = std::find_if(model.rigBones.begin(), model.rigBones.end(), [&](const RigBone& bone) {
            return bone.name == name;
        });
        if (it == model.rigBones.end()) return -1;
        const int bone = static_cast<int>(it - model.rigBones.begin());
        int seen = 0;
        for (std::size_t id = 0; id < cache.rest.size(); ++id) {
            double weight = 0;
            for (const auto& [candidate, value] : model.rigWeights[id]) if (candidate == bone) weight += value;
            if (weight < 0.45) continue;
            const BodyHit hit = bodyHit(model, cache.posed, camera.position, cache.posed[id]);
            if (std::abs(hit.distance - length(cache.posed[id] - camera.position)) < 1e-4) ++seen;
        }
        return seen;
    };
    for (const char* side : {"L", "R"}) {
        const std::string upper = std::string("lowerleg01.") + side;
        const std::string lower = std::string("lowerleg02.") + side;
        if (visibleBone(upper.c_str()) < 12 || visibleBone(lower.c_str()) < 20) {
            error = "anterior tibia occluded by femur";
            return false;
        }
    }
    for (const char* name : {"foot.L", "foot.R"}) {
        const auto it = std::find_if(model.rigBones.begin(), model.rigBones.end(), [&](const RigBone& bone) {
            return bone.name == name;
        });
        if (it == model.rigBones.end()) { error = "foot rig"; return false; }
        const int bone = static_cast<int>(it - model.rigBones.begin());
        const Vec3 middle = (jointPosition(cache.rest, it->head) + jointPosition(cache.rest, it->tail)) * 0.5;
        int best = -1;
        double bestDistance = 1e100;
        for (std::size_t id = 0; id < cache.rest.size(); ++id) {
            double weight = 0;
            for (const auto& [candidate, value] : model.rigWeights[id]) if (candidate == bone) weight += value;
            const double distance = length(cache.rest[id] - middle);
            if (weight >= 0.45 && distance < bestDistance) { best = static_cast<int>(id); bestDistance = distance; }
        }
        if (best < 0) { error = "foot surface"; return false; }
        const Vec3 point = camera.view(cache.posed[best]);
        const double x = camera.width * 0.5 + camera.focal() * point.x / point.z;
        const double y = camera.height * 0.5 - camera.focal() * point.y / point.z;
        const BodyHit hit = bodyHit(model, cache.posed, camera.position, cache.posed[best]);
        bool foot = false;
        if (!hit.genital && hit.triangle >= 0) {
            for (int corner : model.triangles[hit.triangle].v) {
                for (const auto& [candidate, weight] : model.rigWeights[corner]) {
                    if (candidate == bone && weight > 0.2) foot = true;
                }
            }
        }
        if (point.z <= 0.035 || x < 0 || x >= camera.width || y < 0 || y >= camera.height || !foot ||
            std::abs(hit.distance - length(cache.posed[best] - camera.position)) > 1e-4) {
            error = "natural full-down foot visibility";
            return false;
        }
    }
    gtc::Character dressed = character;
    dressed.toiletAction = gtc::ToiletAction::None;
    dressed.pissing = false;
    HumanRenderCache dressedCache;
    prepareHuman(model, dressed, dressedCache);
    Rasterizer dressedRasterizer(camera.width, camera.height, Color{}, camera.focal(), true);
    drawHuman(dressedRasterizer, geometryCamera, model, hair, dressed, dressedCache, ".", tick, true);
    const GpuScene dressedScene = dressedRasterizer.takeGpuScene();
    int dressedVisible = 0;
    for (int id : genital) {
        const BodyHit hit = bodyHit(model, dressedCache.posed, camera.position, dressedCache.posed[id]);
        if (hit.genital && std::abs(hit.distance - length(dressedCache.posed[id] - camera.position)) < 1e-4) ++dressedVisible;
    }
    if (submittedGenital(dressedScene, dressedCache, genital) != static_cast<int>(genital.size()) || dressedVisible < 8) {
        error = "standing male non-piss genital visibility";
        return false;
    }
    return true;
}

int integer(const std::string& value, int fallback) {
    try { return std::stoi(value); } catch (...) { return fallback; }
}

bool receivePair(gtc::native::NativeClient& a, gtc::native::NativeClient& b, std::string& error) {
    return a.receive(error) && b.receive(error);
}

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

bool motionChecks(gtc::native::WorldRenderer& renderer, std::string& error) {
    using namespace gtc::native;
    ClientState target, current;
    gtc::Character citizen;
    citizen.id = 1;
    current.characters.push_back(citizen);
    citizen.position = {4, 2};
    citizen.yaw = 1;
    target.characters.push_back(citizen);
    interpolateCharacters(target, current, 0.1F);
    const double characterBlend = 1.0 - std::exp(-1.5);
    if (std::abs(current.characters[0].position.x - 4 * characterBlend) > 1e-5 ||
        std::abs(current.characters[0].position.y - 2 * characterBlend) > 1e-5) {
        error = "character interpolation";
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
    if (!renderer.render(world, state, citizen.id, view, frame, error) || !motionScene) {
        cleanup();
        return false;
    }
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
    if (!renderer.render(world, state, citizen.id, view, frame, error) || !motionScene) {
        cleanup();
        return false;
    }
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
    view.mode = ViewMode::FirstPerson;
    view.pitch = 0;
    view.yaw = state.characters[0].yaw;
    if (!renderer.render(world, state, citizen.id, view, frame, error)) {
        cleanup();
        return false;
    }
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
    if (!eyeModel.loadRuntime(".", true, error)) {
        cleanup();
        return false;
    }
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
    if (!renderer.render(world, state, citizen.id, view, frame, error)) {
        cleanup();
        return false;
    }
    const Camera seatedEye = renderer.viewCamera(world, state, citizen.id, view);
    const Vec3 expectedSeatedEye = expectedEye(state.characters[0]);
    const bool seated = length(seatedEye.position - expectedSeatedEye) < 1e-6 &&
        length(transform(seatedEye.position)) < 1e-8 && seatedEye.position.y > eye.position.y;
    // This toilet's high measured seat places the eyes slightly above standing;
    // the old fixed root drop and its forced-lower assertion were both wrong.
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
    const Vec3 expectedExtremeUpEye = expectedEye(state.characters[0], headAngleForGaze(neckLimit), 1.0);
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
    neutralStandingView.yaw = bodyYaw;
    neutralStandingView.pitch = 0;
    const Camera neutralStanding = renderer.viewCamera(world, standingState, citizen.id, neutralStandingView);
    HumanRenderCache corneaCache;
    prepareHuman(eyeModel, standingState.characters[0], corneaCache);
    const gtc::Character& standingCitizen = standingState.characters[0];
    Camera jointCamera = neutralStanding;
    jointCamera.worldSpaceGpu = true;
    Rasterizer jointRasterizer(1, 1, Color{}, 1.0, true);
    HairLibrary jointHair;
    drawHuman(jointRasterizer, jointCamera, eyeModel, jointHair, standingCitizen, corneaCache,
              ".", standingState.tick, true);
    const Vec3 posedEyeJoint = (jointPosition(corneaCache.posed, eyeModel.eyeSource[0]) +
                                jointPosition(corneaCache.posed, eyeModel.eyeSource[1])) * 0.5;
    constexpr double eyeRange = 30.0 * 3.141592653589793 / 180.0;
    const bool eyeJoint = length(neutralStanding.position - posedEyeJoint) < 1e-6;
    const bool gazeSplit = std::abs(headAngleForGaze(0.2)) < 1e-9 &&
        std::abs(headAngleForGaze(neckLimit) - (neckLimit - eyeRange)) < 1e-9 &&
        std::abs(headAngleForGaze(1.0) - (1.0 - eyeRange)) < 1e-9;
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
    if (!femaleEyeModel.loadRuntime(".", false, error)) {
        cleanup();
        return false;
    }
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
    standingView.yaw = bodyYaw;
    standingView.pitch = -2.0F;
    if (!renderer.render(world, standingState, citizen.id, standingView, frame, error)) {
        cleanup();
        return false;
    }
    const Camera standingCamera = renderer.viewCamera(world, standingState, citizen.id, standingView);
    ClientState visibilityState = standingState;
    visibilityState.characters[0].toiletAction = gtc::ToiletAction::Piss;
    visibilityState.characters[0].pissing = true;
    const Camera visibilityCamera = renderer.viewCamera(world, visibilityState, citizen.id, standingView);
    if (!bodyVisibility(eyeModel, visibilityState.characters[0], visibilityCamera, visibilityState.tick, error)) {
        cleanup();
        return false;
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
    const auto crosshairPixels = [&] {
        const int cx = frame.width / 2, cy = frame.height / 2;
        int mask = 0, bit = 1;
        for (const auto& point : {std::pair<int, int>{0, 0}, {-4, 0}, {4, 0}, {0, -4}, {0, 4}}) {
            const std::size_t offset = (static_cast<std::size_t>(cy + point.second) * frame.width + cx + point.first) * 3;
            if (frame.rgb[offset] > 32 || frame.rgb[offset + 1] > 32 || frame.rgb[offset + 2] > 32) mask |= bit;
            bit <<= 1;
        }
        return mask;
    };
    ViewState crosshairView = view;
    crosshairView.mode = ViewMode::ThirdPerson;
    if (!renderer.render(world, turnState, citizen.id, crosshairView, frame, error)) {
        cleanup();
        return false;
    }
    const bool thirdCrosshair = crosshairPixels() == 31;
    crosshairView.mode = ViewMode::FirstPerson;
    crosshairView.width = 97;
    crosshairView.height = 83;
    if (!renderer.render(world, turnState, citizen.id, crosshairView, frame, error)) {
        cleanup();
        return false;
    }
    const bool firstCrosshair = crosshairPixels() == 31;
    crosshairView.mode = ViewMode::God;
    crosshairView.width = 480;
    crosshairView.height = 640;
    if (!renderer.render(world, turnState, citizen.id, crosshairView, frame, error)) {
        cleanup();
        return false;
    }
    const int godCrosshairPixels = crosshairPixels();
    const bool crosshairs = thirdCrosshair && firstCrosshair && godCrosshairPixels == 0;
    const bool eyeMatrix = length(eyeView) < 1e-8 && std::abs(targetView.z - 1) < 1e-8;
    const bool attached = length(eye.position - attachedEye) < 1e-6 && torsoRetained;
    const bool look = std::abs(mouse.yaw + 0.022F) < 1e-6F && std::abs(mouse.pitch - 0.044F) < 1e-6F;
    cleanup();
    if (!eyeMatrix) error = "first-person eye matrix";
    else if (!attached) error = "first-person body attachment";
    else if (!eyeJoint) error = "first-person actual eye-joint attachment";
    else if (!gazeSplit) error = "first-person biological gaze split";
    else if (!fullChain) error = "first-person 117.2 mm forward, zero-vertical eye/manubrium geometry";
    else if (!defaultHeight) error = "default citizen height must be exactly 1.7 metres";
    else if (!heightScaling) error = "first-person neck motion must scale with body height";
    else if (!morphContacts) error = "first-person chin/manubrium contact across body morphs";
    else if (!seated) error = "first-person sit-down head attachment";
    else if (!neck) error = "first-person human neck limit";
    else if (!pitch) error = "first-person neck pitch pivot, forward travel=" + std::to_string(forwardTravel);
    else if (!clearance) error = "first-person arm clearance";
    else if (!steadyLens) error = "first-person camera FOV changed with gaze";
    else if (!lowerBody) error = "standing male lower-body visibility, clearance=" + std::to_string(standingClearance) +
        ", pelvis=" + std::to_string(visiblePelvis) + ", legs=" + std::to_string(visibleLegs);
    else if (!shaped) error = "first-person morphed head attachment/clearance, " + leastMorphStage +
        "=" + std::to_string(leastMorphClearance);
    else if (!heightClearance) error = "short-character near-plane clearance=" + std::to_string(leastHeightClearance);
    else if (!bodyTurn) error = "first-person camera did not follow seated body yaw";
    else if (!yawBoundary) error = "first-person head yaw flipped across the 180-degree boundary";
    else if (!crosshairs) error = "first/third-person centered crosshair, third=" + std::to_string(thirdCrosshair) +
        ", first=" + std::to_string(firstCrosshair) + ", god pixels=" + std::to_string(godCrosshairPixels);
    else if (!look) error = "first-person look input";
    return eyeMatrix && attached && eyeJoint && gazeSplit && fullChain && defaultHeight && heightScaling && morphContacts && seated && neck && pitch && clearance &&
        lowerBody && shaped && heightClearance && bodyTurn && yawBoundary && crosshairs && look;
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", output = "build/native-smoke", root = ".";
    int port = 1453;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--host" && i + 1 < argc) host = argv[++i];
        else if (argument == "--port" && i + 1 < argc) port = integer(argv[++i], 1453);
        else if (argument == "--output" && i + 1 < argc) output = argv[++i];
        else if (argument == "--root" && i + 1 < argc) root = argv[++i];
        else {
            std::cerr << "Usage: gtc-native-smoke [--host 127.0.0.1] [--port 1453] [--output DIR]\n";
            return 2;
        }
    }
    std::string error;
    gtc::native::NativeClient alpha, beta;
    if (!alpha.connect(host, static_cast<std::uint16_t>(port), error) ||
        !beta.connect(host, static_cast<std::uint16_t>(port), error)) {
        std::cerr << error << '\n';
        return 1;
    }
    gtc::Appearance aAppearance;
    aAppearance.male = true;
    aAppearance.skin = 1;
    aAppearance.hair = 2;
    aAppearance.hairColor = 1;
    aAppearance.shirt = 1;
    aAppearance.coat = true;
    aAppearance.suit = true;
    aAppearance.tie = 2;
    aAppearance.morphs[0] = 35;
    aAppearance.morphs[3] = 8;
    gtc::Appearance bAppearance;
    bAppearance.male = false;
    bAppearance.skin = 3;
    bAppearance.hair = 5;
    bAppearance.hairColor = 3;
    bAppearance.shirt = 2;
    bAppearance.pants = 1;
    bAppearance.dress = true;
    bAppearance.morphs[0] = 28;
    bAppearance.morphs[7] = 12;
    if (!alpha.sendJoin("Native Alpha") || !beta.sendJoin("Native Beta") ||
        !alpha.sendCustomize(aAppearance) || !beta.sendCustomize(bAppearance) ||
        !alpha.sendCheat("bowel", 100) || !beta.sendCheat("bladder", 100) ||
        !alpha.sendPath(alpha.world().toiletFront) || !beta.sendInput({0, 1}, false, false, 3.14159265F, 0, true)) {
        std::cerr << "Could not send native smoke commands\n";
        return 1;
    }
    bool replicated = false, customized = false, inputMoved = false, pathSeen = false;
    bool backwardReplicated = false, slowReplicated = false;
    bool toiletRequested = false, toiletSeen = false;
    int iterations = 0;
    for (; iterations < 100; ++iterations) {
        if (!receivePair(alpha, beta, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        if (iterations == 5) {
            if (!beta.sendInput({}, false, false, 0, 0) || !beta.sendPath({3.0, 2.3})) return 1;
        }
        const gtc::Character* a = alpha.state().character(alpha.playerId());
        const gtc::Character* b = beta.state().character(beta.playerId());
        const gtc::Character* aInB = beta.state().character(alpha.playerId());
        const gtc::Character* bInA = alpha.state().character(beta.playerId());
        replicated = a && b && aInB && bInA && aInB->name == "Native Alpha" && bInA->name == "Native Beta";
        customized = a && b && aInB && bInA && a->appearance.morphs[0] == 35 && a->appearance.morphs[3] == 8 &&
                     !b->appearance.male && b->appearance.hair == 5 && b->appearance.morphs[7] == 12 &&
                     aInB->appearance.coat && aInB->appearance.suit && aInB->appearance.tie == 2 &&
                     bInA->appearance.dress && bInA->appearance.morphs[7] == 12;
        inputMoved = inputMoved || (b && b->speed > 0.1F);
        backwardReplicated = backwardReplicated || (b && b->speed > 0.1F && b->manualMove.y > 0.9F &&
            std::abs(b->yaw - 3.14159265F) < 0.001F);
        slowReplicated = slowReplicated || (b && b->manualMove.y > 0.9F &&
            std::abs(b->speed * 3.6F - 4.0F) < 0.01F);
        pathSeen = pathSeen || (a && !a->path.empty()) || (b && !b->path.empty());
        if (a && !toiletRequested && gtc::distance(a->position, alpha.world().toiletFront) <= 1.55F) {
            if (!alpha.sendToilet("shit")) return 1;
            toiletRequested = true;
        }
        const gtc::Character* aOther = beta.state().character(alpha.playerId());
        toiletSeen = toiletSeen || (a && aOther && a->toiletAction == gtc::ToiletAction::Shit &&
            aOther->toiletAction == gtc::ToiletAction::Shit && a->onToilet &&
            alpha.state().toiletUser == alpha.playerId() && beta.state().toiletUser == alpha.playerId());
        if (replicated && customized && inputMoved && backwardReplicated && slowReplicated && pathSeen && toiletSeen) break;
    }
    const bool passed = replicated && customized && inputMoved && backwardReplicated && slowReplicated && pathSeen && toiletSeen;
    bool motion = false;
    std::error_code code;
    std::filesystem::create_directories(output, code);
    if (passed) {
        gtc::native::WorldRenderer renderer;
        if (!renderer.load(root, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        motion = motionChecks(renderer, error);
        if (!motion) {
            std::cerr << "Native motion checks failed: " << error << '\n';
            return 1;
        }
        struct Capture { const char* name; gtc::native::NativeClient* client; gtc::native::ViewState view; };
        gtc::native::ViewState tps;
        tps.yaw = 0;
        gtc::native::ViewState fps;
        fps.mode = gtc::native::ViewMode::FirstPerson;
        fps.pitch = -2.0F;
        gtc::native::ViewState fpsLevel;
        fpsLevel.mode = gtc::native::ViewMode::FirstPerson;
        gtc::native::ViewState fpsExtreme = fps;
        if (const gtc::Character* local = alpha.state().character(alpha.playerId())) fpsExtreme.yaw = local->yaw + 3.0F;
        gtc::native::ViewState god;
        god.mode = gtc::native::ViewMode::God;
        const Capture captures[] = {{"alpha-tps", &alpha, tps}, {"beta-tps", &beta, tps},
                                    {"alpha-fps", &alpha, fps}, {"alpha-fps-extreme", &alpha, fpsExtreme},
                                    {"alpha-fps-level", &alpha, fpsLevel}, {"beta-fps-level", &beta, fpsLevel},
                                    {"god", &alpha, god}};
        for (const Capture& capture : captures) {
            gtc::native::NativeFrame frame;
            if (!renderer.render(capture.client->world(), capture.client->state(), capture.client->playerId(),
                                 capture.view, frame, error) || !frame.save(output + "/" + capture.name + ".png")) {
                std::cerr << "Native capture failed: " << error << '\n';
                return 1;
            }
        }
        gtc::native::ClientState standing = alpha.state();
        auto male = std::find_if(standing.characters.begin(), standing.characters.end(), [&](const gtc::Character& character) {
            return character.id == alpha.playerId();
        });
        if (male != standing.characters.end()) {
            male->onToilet = false;
            male->toiletAction = gtc::ToiletAction::None;
            male->position = {0, 0};
            male->yaw = 0;
            gtc::native::ViewState standingFps = fps;
            standingFps.yaw = male->yaw;
            gtc::native::NativeFrame frame;
            if (!renderer.render(alpha.world(), standing, alpha.playerId(), standingFps, frame, error) ||
                !frame.save(output + "/alpha-standing-fps.png")) {
                std::cerr << "Native standing male capture failed: " << error << '\n';
                return 1;
            }
            male->toiletAction = gtc::ToiletAction::Piss;
            male->pissing = true;
            if (!renderer.render(alpha.world(), standing, alpha.playerId(), standingFps, frame, error) ||
                !frame.save(output + "/alpha-piss-fps.png")) {
                std::cerr << "Native standing male piss capture failed: " << error << '\n';
                return 1;
            }
        }
    }
    std::ofstream report(output + "/report.json");
    report << "{\"passed\":" << (passed ? "true" : "false") << ",\"alpha\":" << alpha.playerId()
           << ",\"beta\":" << beta.playerId() << ",\"tick\":" << alpha.state().tick
           << ",\"replicated\":" << (replicated ? "true" : "false")
           << ",\"customized\":" << (customized ? "true" : "false")
           << ",\"inputMoved\":" << (inputMoved ? "true" : "false")
           << ",\"pathSeen\":" << (pathSeen ? "true" : "false")
           << ",\"toiletSeen\":" << (toiletSeen ? "true" : "false")
           << ",\"motion\":" << (motion ? "true" : "false")
           << ",\"iterations\":" << iterations << "}\n";
    std::cout << "native smoke: " << (passed ? "PASS" : "FAIL") << ", alpha=" << alpha.playerId()
              << ", beta=" << beta.playerId() << ", tick=" << alpha.state().tick << '\n';
    if (!passed) std::cerr << "replication=" << replicated << " customize=" << customized
                           << " input=" << inputMoved << " backward=" << backwardReplicated
                           << " slow=" << slowReplicated << " path=" << pathSeen << " toilet=" << toiletSeen << '\n';
    return passed ? 0 : 1;
}
