#pragma once

// Geometry checks shared by the smoke harness: ray casts against the body,
// what the renderer actually submitted for the anatomical mesh, and whether
// every part that should be visible made it into the batch.

#include "client/human_renderer.h"
#include "client/raster.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gtc::native {

inline gtc::native::Vec3 jointPosition(const std::vector<gtc::native::Vec3>& points, const std::vector<int>& ids) {
    gtc::native::Vec3 result;
    for (int id : ids) result += points.at(id);
    return result / static_cast<double>(ids.size());
}
struct BodyHit {
    double distance = 1e100;
    int triangle = -1;
    bool genital = false;
};
inline BodyHit bodyHit(const gtc::native::HumanModel& model, const std::vector<gtc::native::Vec3>& posed,
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
inline int submittedGenital(const gtc::native::GpuScene& scene, const gtc::native::HumanRenderCache& cache,
                     const std::vector<int>& ids, gtc::native::Material::Kind kind = gtc::native::Material::Kind::Skin,
                     double offset = 0.0) {
    using namespace gtc::native;
    int submitted = 0;
    for (int id : ids) {
        bool found = false;
        const Vec3 point = cache.posed[id] + cache.posedNormals[id] * offset;
        for (const GpuBatch& batch : scene.batches) {
            if (batch.material.kind != kind) continue;
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
inline bool genitalMode(const gtc::native::HumanModel& model, const gtc::Character& character,
                 const gtc::native::Camera& camera, std::uint64_t tick, bool exposed, bool hideHead,
                 std::string& error) {
    using namespace gtc::native;
    HumanRenderCache cache;
    prepareHuman(model, character, cache);
    std::vector<int> ids;
    for (const Triangle& triangle : model.genitalTriangles) for (int id : triangle.v) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    Camera geometryCamera = camera;
    geometryCamera.worldSpaceGpu = true;
    Rasterizer rasterizer(camera.width, camera.height, Color{}, camera.focal(), true);
    HairLibrary hair;
    drawHuman(rasterizer, geometryCamera, model, hair, character, cache, ".", tick, hideHead);
    const GpuScene scene = rasterizer.takeGpuScene();
    const int skin = submittedGenital(scene, cache, ids);
    const int cloth = submittedGenital(scene, cache, ids, Material::Kind::Cloth);
    if (!ids.empty() && (exposed ? skin == static_cast<int>(ids.size()) && cloth == 0 : cloth == 0 && skin == 0)) {
        if (character.unzipped && character.toiletAction == gtc::ToiletAction::None && cache.genitalTip >= 0) {
            const Vec3 actual = normalized(cache.genitalExit - cache.genitalRoot);
            const Vec3 expected{std::cos(character.pitch) * std::sin(character.yaw), std::sin(character.pitch),
                std::cos(character.pitch) * std::cos(character.yaw)};
            if (dot(actual, expected) < 0.99999) { error = "unzipped crosshair articulation"; return false; }
        }
        return true;
    }
    error = exposed ? "unzipped genital skin submission" : "zipped anatomical mesh must be hidden";
    return false;
}
inline bool bodyVisibility(const gtc::native::HumanModel& model, const gtc::Character& character,
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
    if (!genitalMode(model, dressed, camera, tick, false, true, error)) return false;
    dressed.unzipped = true;
    if (!genitalMode(model, dressed, camera, tick, true, true, error)) return false;
    return true;
}

}  // namespace gtc::native
