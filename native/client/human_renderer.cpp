#include "client/human_renderer.h"
#include "client/human_renderer_pose.h"

#include "client/blockbody.h"
#include "client/human_materials.h"
#include "client/human_palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <unordered_map>

namespace gtc::native {

using namespace human_detail;

void prepareHuman(const HumanModel& model, const Character& character, HumanRenderCache& cache) {
    if (cache.valid && cache.male == character.appearance.male && cache.morphs == character.appearance.morphs) return;
    std::vector<double> morphs(character.appearance.morphs.begin(), character.appearance.morphs.end());
    std::vector<int> ids(model.vertices.size());
    for (std::size_t i = 0; i < ids.size(); ++i) ids[i] = static_cast<int>(i);
    cache.rest = model.shape(ids, morphs);
    // The authored body carries no per-vertex target deltas, so its sliders are
    // answered by a pair of proportions instead: stature and head size.
    if (model.blockBody) shapeBlockBody(blockProportions(model, morphs), cache.rest);
    cache.normals.assign(cache.rest.size(), Vec3{});
    for (const Triangle& triangle : model.triangles) {
        const Vec3 normal = cross(cache.rest[triangle.v[1]] - cache.rest[triangle.v[0]],
                                  cache.rest[triangle.v[2]] - cache.rest[triangle.v[0]]);
        for (int index : triangle.v) cache.normals[index] += normal;
    }
    for (const Triangle& triangle : model.genitalTriangles) {
        const Vec3 normal = cross(cache.rest[triangle.v[1]] - cache.rest[triangle.v[0]],
                                  cache.rest[triangle.v[2]] - cache.rest[triangle.v[0]]);
        for (int index : triangle.v) cache.normals[index] += normal;
    }
    for (Vec3& normal : cache.normals) normal = normalized(normal);
    cache.morphs = character.appearance.morphs;
    cache.male = character.appearance.male;
    cache.valid = true;
    cache.poseValid = false;
}

namespace {


struct GazeChain {
    std::vector<int> indices;
    std::vector<Vec3> pivots;
    std::vector<double> flexionLimits;
    std::vector<double> yawShares;
    std::vector<double> extensionShares;
    Vec3 eye;
    Vec3 chin;
    Vec3 manubrium;
    bool valid = false;
};

GazeChain gazeChain(const HumanModel& model, const std::vector<Vec3>& rest,
                    const std::vector<Transform>& transforms) {
    static const char* names[] = {"neck01", "neck02", "neck03", "head"};
    // Direction-specific published mobility: grouped lower/middle cervical
    // flexion, then C1/C2 and C0/C1. These are limits, not arbitrary weights.
    constexpr double flexionDegrees[] = {65.0, 27.0, 11.5, 3.5};
    constexpr double yaw[] = {0.25, 0.25, 0.50, 0.0};
    constexpr double extension[] = {22.0 + 28.0 + 15.0, 12.0 + 15.0, 10.9, 21.0};
    GazeChain chain;
    for (int i = 0; i < 4; ++i) {
        const int index = boneIndex(model, names[i]);
        if (index < 0) continue;
        chain.indices.push_back(index);
        chain.pivots.push_back(transforms[index].point(joint(rest, model.rigBones[index].head)));
        chain.flexionLimits.push_back(flexionDegrees[i] * pi / 180.0);
        chain.yawShares.push_back(yaw[i]);
        chain.extensionShares.push_back(extension[i]);
    }
    const int headIndex = boneIndex(model, "head"), jawIndex = boneIndex(model, "jaw");
    const int leftClavicle = boneIndex(model, "clavicle.L");
    const int rightClavicle = boneIndex(model, "clavicle.R");
    chain.valid = chain.indices.size() == 4 && headIndex >= 0 && jawIndex >= 0 &&
        leftClavicle >= 0 && rightClavicle >= 0;
    const Transform head = headIndex >= 0 ? transforms[headIndex] : Transform{};
    const Vec3 eyes = (joint(rest, model.eyeSource[0]) + joint(rest, model.eyeSource[1])) * 0.5;
    chain.eye = head.point(eyes);
    chain.chin = jawIndex >= 0 ? head.point(joint(rest, model.rigBones[jawIndex].tail)) : chain.eye;
    if (leftClavicle >= 0 && rightClavicle >= 0) {
        chain.manubrium = (joint(rest, model.rigBones[leftClavicle].head) +
            joint(rest, model.rigBones[rightClavicle].head)) * 0.5;
    }
    return chain;
}

Vec3 anteriorManubrium(const HumanModel& model, const std::vector<Vec3>& rest, Vec3 sternum) {
    // Intersect the actual morphed skin with a vertical ray at the two
    // sternoclavicular joints' midpoint. The joint is internal; the foremost
    // intersected triangle is the anterior manubrium surface the chin contacts.
    double front = -1e100;
    for (const Triangle& triangle : model.triangles) {
        const Vec3& a = rest[triangle.v[0]];
        const Vec3& b = rest[triangle.v[1]];
        const Vec3& c = rest[triangle.v[2]];
        if (std::min({a.y, b.y, c.y}) > sternum.y || std::max({a.y, b.y, c.y}) < sternum.y ||
            std::min({a.x, b.x, c.x}) > sternum.x || std::max({a.x, b.x, c.x}) < sternum.x) continue;
        const double denominator = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::abs(denominator) < 1e-12) continue;
        const double wa = ((b.y - c.y) * (sternum.x - c.x) +
            (c.x - b.x) * (sternum.y - c.y)) / denominator;
        const double wb = ((c.y - a.y) * (sternum.x - c.x) +
            (a.x - c.x) * (sternum.y - c.y)) / denominator;
        const double wc = 1.0 - wa - wb;
        if (wa < -1e-7 || wb < -1e-7 || wc < -1e-7) continue;
        front = std::max(front, wa * a.z + wb * b.z + wc * c.z);
    }
    if (front > -1e99) sternum.z = front;
    return sternum;
}


} // namespace

HumanGazePose humanGazePose(const HumanModel& model, const std::vector<Vec3>& rest,
                            const Character& character, double tick,
                            double headYaw, double gazePitch) {
    const std::vector<Transform> transforms = pose(model, rest, character, tick);
    GazeChain chain = gazeChain(model, rest, transforms);
    chain.manubrium = anteriorManubrium(model, rest, chain.manubrium);
    Vec3 posedEye = chain.eye, posedChin = chain.chin;
    if (chain.valid && (std::abs(headYaw) > 1e-9 || std::abs(gazePitch) > 1e-9)) {
        const std::size_t count = chain.pivots.size();
        std::vector<double> pitches(count, 0.0);
        Vec3 protraction;
        if (gazePitch < 0) {
            const Vec3 eyeOverChin = chain.eye - chain.chin;
            const double headFlexion = std::atan2(eyeOverChin.y, eyeOverChin.z);
            double mobility = 0;
            for (double limit : chain.flexionLimits) mobility += limit;
            std::vector<Vec3> contactPoints = chain.pivots;
            contactPoints.push_back(chain.chin);
            for (std::size_t i = 0; i < count; ++i) {
                pitches[i] = -headFlexion * chain.flexionLimits[i] / std::max(1e-9, mobility);
                for (std::size_t j = i + 1; j < contactPoints.size(); ++j) {
                    contactPoints[j] = contactPoints[i] +
                        apply(rotationX(-pitches[i]), contactPoints[j] - contactPoints[i]);
                }
            }
            // Rigid rotation alone cannot reach this fully flexed pose on the
            // MakeHuman pivots. Solve the remaining cervicothoracic translation
            // from the actual contact landmark; never offset the camera directly.
            protraction = chain.manubrium - contactPoints.back();
            const double amount = std::clamp((-gazePitch - kEyeInHeadRange) /
                (pi * 0.5 - kEyeInHeadRange), 0.0, 1.0);
            for (double& pitch : pitches) pitch *= amount;
            protraction = protraction * amount;
        } else {
            const double physical = gazePitch - std::clamp(gazePitch, -kEyeInHeadRange, kEyeInHeadRange);
            double total = 0;
            for (double share : chain.extensionShares) total += share;
            for (std::size_t i = 0; i < count; ++i) {
                pitches[i] = physical * chain.extensionShares[i] / std::max(1e-9, total);
            }
        }
        std::vector<Vec3> points = chain.pivots;
        points.push_back(posedEye);
        points.push_back(posedChin);
        double yawTotal = 0;
        for (double share : chain.yawShares) yawTotal += share;
        for (std::size_t i = 0; i < count; ++i) {
            const double yaw = headYaw * chain.yawShares[i] / std::max(1e-9, yawTotal);
            for (std::size_t j = i + 1; j < points.size(); ++j) {
                points[j] = points[i] + rotateWorld(apply(rotationX(-pitches[i]), points[j] - points[i]),
                                                    static_cast<float>(yaw));
            }
        }
        posedEye = points[count] + protraction;
        posedChin = points[count + 1] + protraction;
    }
    const double rootY = humanRootHeight(model, rest, character, transforms);
    auto world = [&](Vec3 point) {
        return rotateWorld(point * kHumanHeightScale, character.yaw) +
            Vec3{character.position.x, rootY, character.position.y};
    };
    return {world(posedEye), world(posedChin), world(chain.manubrium)};
}

Vec3 humanEyePosition(const HumanModel& model, const std::vector<Vec3>& rest,
                      const Character& character, double tick,
                      double headYaw, double gazePitch) {
    return humanGazePose(model, rest, character, tick, headYaw, gazePitch).eye;
}

Vec3 humanStreamOrigin(const Character& character, const HumanRenderCache* cache, float fallbackPitch) {
    if (cache && cache->poseValid && cache->genitalTip >= 0 &&
        static_cast<std::size_t>(cache->genitalTip) < cache->posed.size()) {
        const Vec3 tip = cache->genitalExit;
        const double yaw = character.yaw - cache->poseYaw;
        const double c = std::cos(yaw), s = std::sin(yaw);
        const double x = tip.x - cache->poseX, z = tip.z - cache->poseZ;
        return {character.position.x + c * x + s * z,
                tip.y + character.elevation - cache->poseElevation,
                character.position.y - s * x + c * z};
    }
    const float cp = std::cos(fallbackPitch), sp = std::sin(fallbackPitch);
    const gtc::Vec2 forward{std::sin(character.yaw), std::cos(character.yaw)};
    const gtc::Vec2 origin = character.position + forward * (kPissEmitterPivotForward + kPissEmitterShaft * cp);
    return {origin.x, character.elevation + kPissEmitterPivotHeight + kPissEmitterShaft * sp, origin.y};
}

} // namespace gtc::native
