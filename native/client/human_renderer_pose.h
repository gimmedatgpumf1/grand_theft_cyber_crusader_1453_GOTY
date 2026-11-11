// Posing and drawing: the small matrix arithmetic, the chain solve that turns a
// character state into a bone transform per bone, and the two lofts that draw a
// hair and a limb. They were in an anonymous namespace in human_renderer.cpp;
// drawing one human is now its own file and needs them, so they are inline here.

#ifndef GTC_CLIENT_HUMAN_RENDERER_POSE_H
#define GTC_CLIENT_HUMAN_RENDERER_POSE_H

#include "client/human_palette.h"
#include "client/human_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace gtc::native::human_detail {

constexpr double pi = 3.141592653589793;

struct Mat3 {
    std::array<double, 9> v{1, 0, 0, 0, 1, 0, 0, 0, 1};
};

inline Vec3 apply(const Mat3& m, Vec3 p) {
    return {m.v[0] * p.x + m.v[1] * p.y + m.v[2] * p.z,
            m.v[3] * p.x + m.v[4] * p.y + m.v[5] * p.z,
            m.v[6] * p.x + m.v[7] * p.y + m.v[8] * p.z};
}

inline Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 result;
    for (int row = 0; row < 3; ++row) for (int column = 0; column < 3; ++column) {
        result.v[row * 3 + column] = 0;
        for (int k = 0; k < 3; ++k) result.v[row * 3 + column] += a.v[row * 3 + k] * b.v[k * 3 + column];
    }
    return result;
}

inline Mat3 rotationX(double angle) {
    Mat3 result;
    const double c = std::cos(angle), s = std::sin(angle);
    result.v = {1, 0, 0, 0, c, -s, 0, s, c};
    return result;
}

inline Mat3 rotationBetween(Vec3 from, Vec3 to) {
    if (length(from) < 1e-9 || length(to) < 1e-9) return {};
    from = normalized(from); to = normalized(to);
    Vec3 axis = cross(from, to);
    double sine = length(axis), angle = 0;
    const double cosine = std::clamp(dot(from, to), -1.0, 1.0);
    if (sine < 1e-9) {
        if (cosine > 0) return {};
        axis = normalized(cross(from, std::abs(from.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
        angle = pi;
    } else {
        axis = axis / sine;
        angle = std::atan2(sine, cosine);
    }
    const double c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    const double x = axis.x, y = axis.y, z = axis.z;
    Mat3 result;
    result.v = {x * x * t + c, x * y * t - z * s, x * z * t + y * s,
                y * x * t + z * s, y * y * t + c, y * z * t - x * s,
                z * x * t - y * s, z * y * t + x * s, z * z * t + c};
    return result;
}

inline Mat3 rotationZ(double angle) {
    Mat3 result;
    const double c = std::cos(angle), s = std::sin(angle);
    result.v = {c, -s, 0, s, c, 0, 0, 0, 1};
    return result;
}

struct Transform {
    Mat3 rotation;
    Vec3 translation;

    Vec3 point(Vec3 value) const { return apply(rotation, value) + translation; }
    Vec3 direction(Vec3 value) const { return apply(rotation, value); }
};

inline Transform multiply(const Transform& a, const Transform& b) {
    return {multiply(a.rotation, b.rotation), a.point(b.translation)};
}

inline Transform around(Vec3 pivot, const Mat3& rotation) {
    return {rotation, pivot - apply(rotation, pivot)};
}

inline Vec3 joint(const std::vector<Vec3>& positions, const std::vector<int>& ids) {
    Vec3 result;
    for (int index : ids) if (index >= 0 && static_cast<std::size_t>(index) < positions.size()) result += positions[index];
    return result / static_cast<double>(std::max<std::size_t>(1, ids.size()));
}

inline int boneIndex(const HumanModel& model, const std::string& name) {
    for (std::size_t i = 0; i < model.rigBones.size(); ++i) if (model.rigBones[i].name == name) return static_cast<int>(i);
    return -1;
}

inline double standingIdleWeight(const Character& character) {
    if (character.onToilet) return 0.0;
    const double movement = std::clamp(static_cast<double>(character.speed) / kFastWalkSpeed, 0.0, 1.0);
    return 1.0 - std::clamp(movement * 4.0, 0.0, 1.0);
}

inline std::vector<Transform> pose(const HumanModel& model, const std::vector<Vec3>& rest,
                            const Character& character, double tick) {
    std::vector<Transform> transforms(model.rigBones.size());
    std::vector<int> states(model.rigBones.size(), 0);
    const bool seated = character.onToilet;
    const bool prone = character.prone && !seated;
    const bool airborne = !character.grounded && !character.grabbing && !seated;
    const double crouch = prone || seated ? 0.0 :
        std::max(character.crouched ? 1.0 : 0.0, static_cast<double>(character.charge) * 0.65);
    const double movement = seated ? 0.0 : std::clamp(static_cast<double>(character.speed) / kFastWalkSpeed, 0.0, 1.4);
    const double phase = tick * 0.055 * (4.0 + character.speed) + character.id * 0.7;
    const double forwardMovement = character.manualMove.x * std::sin(character.yaw) +
        character.manualMove.y * std::cos(character.yaw);
    const double gait = forwardMovement < -0.05 ? -1.0 : 1.0;
    const double swing = std::sin(phase) * 0.48 * movement * gait;
    // Relaxed standing corrects the source A-pose to hip-width feet and uses
    // a small hip angle so the anterior tibia is not exactly hidden behind
    // the femur from the anatomical eye position. This ordinary rig pose
    // fades out with locomotion and has no gaze or vertex-deformation input;
    // one-shot actions can replace the idle layer.
    const double idle = prone ? 0.0 : standingIdleWeight(character);
    const double idleHip = 8.0 * pi / 180.0 * idle;
    const double idleAdduction = 5.0 * pi / 180.0 * idle;
    const double armRelax = seated ? 0.0 : character.grabbing ? 0.58 : prone ? 0.35 : 0.60;
    const bool freeArms = !seated && !character.grabbing && !prone && !character.blocking &&
        character.attackAnim <= 0.0F && character.toiletAction == ToiletAction::None;
    const double elbowRelax = freeArms ? 0.65 : 0.0;
    const double reachHeight = character.grabHeight > 0.0F ? character.grabHeight - character.elevation : 1.40;
    const double climbAngle = -std::acos(std::clamp((1.35 - reachHeight) / 0.62, -0.96, 0.98));
    const double climbReach = character.grabbing && forwardMovement > 0.05 ? std::sin(phase * 1.4) * 0.18 : 0.0;
    auto local = [&](const RigBone& bone) {
        double angle = 0;
        if (seated) {
            if (bone.name == "upperleg01.L" || bone.name == "upperleg01.R") angle = -1.28;
            if (bone.name == "lowerleg01.L" || bone.name == "lowerleg01.R") angle = 1.28;
            if (bone.name == "upperarm01.L" || bone.name == "upperarm01.R") angle = -0.20;
        } else {
            if (bone.name == "upperleg01.L") angle = -swing - idleHip;
            if (bone.name == "upperleg01.R") angle = swing - idleHip;
            if (bone.name == "lowerleg01.L") angle = std::max(0.0, swing) * 0.75;
            if (bone.name == "lowerleg01.R") angle = std::max(0.0, -swing) * 0.75;
            if (bone.name == "upperarm01.L") angle = swing * 0.72;
            if (bone.name == "upperarm01.R") angle = -swing * 0.72;
            if (bone.name == "lowerarm01.L" || bone.name == "lowerarm01.R") angle = elbowRelax;
            if (character.toiletAction == ToiletAction::Piss && bone.name == "upperarm01.R") angle = -0.42;
            if (crouch > 0.0) {
                if (bone.name == "upperleg01.L" || bone.name == "upperleg01.R") angle += (-1.25 - angle) * crouch;
                if (bone.name == "lowerleg01.L" || bone.name == "lowerleg01.R") angle += (1.90 - angle) * crouch;
                if (bone.name == "spine05") angle += 0.32 * crouch;
                if (bone.name == "upperarm01.L" || bone.name == "upperarm01.R") angle -= 0.45 * crouch;
            }
            if (airborne) {
                if (bone.name == "upperleg01.L" || bone.name == "upperleg01.R") angle -= 0.18;
                if (bone.name == "lowerleg01.L" || bone.name == "lowerleg01.R") angle += 0.32;
                if (bone.name == "upperarm01.L" || bone.name == "upperarm01.R") angle -= 0.35;
            }
            if (character.blocking) {
                if (bone.name == "upperarm01.L" || bone.name == "upperarm01.R") angle = -0.85;
                if (bone.name == "lowerarm01.L" || bone.name == "lowerarm01.R") angle = 1.15;
            } else if (character.attackAnim > 0.0F && !character.unzipped) {
                if (bone.name == "upperarm01.R") angle = -1.35;
                if (bone.name == "upperarm01.L") angle = -0.35;
                if (bone.name == "lowerarm01.R") angle = 0.35;
            } else if (character.grabbing) {
                if (bone.name == "upperarm01.L") angle = climbAngle + climbReach;
                if (bone.name == "upperarm01.R") angle = climbAngle - climbReach;
                if (bone.name == "upperarm02.L" || bone.name == "upperarm02.R") angle = -0.10;
            } else if (prone && (bone.name == "upperarm01.L" || bone.name == "upperarm01.R")) {
                angle = -0.55 + swing * 0.25;
            }
        }
        Mat3 rotation = rotationX(prone && bone.name == "root" ? pi * 0.5 : angle);
        if (idleAdduction != 0.0 && (bone.name == "upperleg01.L" || bone.name == "upperleg01.R")) {
            const double direction = bone.name == "upperleg01.L" ? -1.0 : 1.0;
            rotation = multiply(rotationZ(direction * idleAdduction), rotation);
        }
        if (armRelax > 0.0 && (bone.name == "upperarm01.L" || bone.name == "upperarm01.R")) {
            const double direction = bone.name == "upperarm01.L" ? -1.0 : 1.0;
            rotation = multiply(rotationZ(direction * armRelax), rotation);
        }
        return around(joint(rest, bone.head), rotation);
    };
    std::function<void(int)> compute = [&](int index) {
        if (states[index] == 2) return;
        const RigBone& bone = model.rigBones[index];
        const Transform own = local(bone);
        if (bone.parentIndex >= 0) {
            compute(bone.parentIndex);
            transforms[index] = multiply(transforms[bone.parentIndex], own);
        } else {
            transforms[index] = own;
        }
        states[index] = 2;
    };
    for (std::size_t i = 0; i < transforms.size(); ++i) compute(static_cast<int>(i));
    return transforms;
}

inline Vec3 rotateWorld(Vec3 point, float yaw) {
    const double c = std::cos(yaw), s = std::sin(yaw);
    return {c * point.x + s * point.z, point.y, -s * point.x + c * point.z};
}

struct LoftRing { double y, x, z; };

inline void drawLoft(Rasterizer& rasterizer, const Camera& camera, const std::function<Vec3(Vec3)>& world,
              const std::vector<LoftRing>& rings, const Material& material,
              double arcStart = 0, double arcLength = 2 * pi) {
    constexpr int segments = 24;
    std::vector<Vec3> positions;
    positions.reserve(rings.size() * (segments + 1));
    for (const LoftRing& ring : rings) for (int i = 0; i <= segments; ++i) {
        const double angle = arcStart + arcLength * i / segments;
        positions.push_back(world({std::cos(angle) * ring.x, ring.y, std::sin(angle) * ring.z}));
    }
    std::vector<std::array<int, 3>> triangles;
    for (std::size_t ring = 0; ring + 1 < rings.size(); ++ring) for (int i = 0; i < segments; ++i) {
        const int a = static_cast<int>(ring * (segments + 1) + i), b = a + 1, c = a + segments + 1;
        triangles.push_back({a, c, b});
        triangles.push_back({b, c, c + 1});
    }
    std::vector<Vec3> normals(positions.size());
    for (const auto& triangle : triangles) {
        const Vec3 normal = cross(positions[triangle[1]] - positions[triangle[0]],
                                  positions[triangle[2]] - positions[triangle[0]]);
        for (int index : triangle) normals[index] += normal;
    }
    std::vector<RenderVertex> transformed(positions.size());
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const int ring = static_cast<int>(index / (segments + 1));
        const int segment = static_cast<int>(index % (segments + 1));
        const Vec2 uv{static_cast<double>(segment) / segments,
                      static_cast<double>(ring) / std::max<std::size_t>(1, rings.size() - 1)};
        transformed[index] = cameraVertex(camera, positions[index], normalized(normals[index]), positions[index], uv);
    }
    for (const auto& triangle : triangles) {
        rasterizer.triangle({transformed[triangle[0]], transformed[triangle[1]], transformed[triangle[2]]}, material);
    }
}

inline void drawHair(Rasterizer& rasterizer, const Camera& camera, const HumanModel& model,
              const HairStyle& style, const std::vector<Vec3>& rest, const Transform& head,
              const Character& character, const std::function<Vec3(Vec3)>& world) {
    std::unordered_map<int, Vec3> refs;
    for (int id : style.required) if (id >= 0 && static_cast<std::size_t>(id) < rest.size()) refs[id] = rest[id];
    double ratios[3] = {1, 1, 1};
    for (int axis = 0; axis < 3; ++axis) {
        if (!style.scaleValid[axis]) continue;
        const Vec3 a = refs[style.scaleRefs[axis][0]], b = refs[style.scaleRefs[axis][1]];
        const double distance = axis == 0 ? std::abs(a.x - b.x) : axis == 1 ? std::abs(a.y - b.y) : std::abs(a.z - b.z);
        ratios[axis] = distance / std::max(1e-9, style.scaleDefaults[axis] * model.scale);
    }
    std::vector<Vec3> positions;
    positions.reserve(style.vertices.size());
    for (const HairVertex& vertex : style.vertices) {
        Vec3 point = vertex.offset * model.scale;
        point.x *= ratios[0]; point.y *= ratios[1]; point.z *= ratios[2];
        for (int anchor = 0; anchor < 3; ++anchor) point += refs[vertex.refs[anchor]] * vertex.weights[anchor];
        positions.push_back(point);
    }
    if (style.key == "short01" || style.key == "short02" || style.key == "short04") {
        double top = -1e100;
        for (int id : model.headVertices) if (id >= 0 && static_cast<std::size_t>(id) < rest.size()) top = std::max(top, rest[id].y);
        top -= 0.035;
        const double factor = style.key == "short04" ? 1.15 : 0.48;
        for (Vec3& point : positions) if (point.y > top) point.y = top + (point.y - top) * factor;
    }
    for (Vec3& point : positions) point = world(head.point(point));
    std::vector<Vec3> normals(positions.size());
    for (const auto& triangle : style.triangles) {
        const Vec3 normal = cross(positions[triangle[1]] - positions[triangle[0]],
                                  positions[triangle[2]] - positions[triangle[0]]);
        for (int index : triangle) normals[index] += normal;
    }
    std::vector<RenderVertex> transformed(positions.size());
    for (std::size_t index = 0; index < positions.size(); ++index) {
        transformed[index] = cameraVertex(camera, positions[index], normalized(normals[index]),
                                          positions[index], style.vertices[index].uv);
    }
    Material material;
    material.kind = Material::Kind::Hair;
    material.color = hairColor(character.appearance.hairColor) * 1.35;
    material.texture = &loadTexture(style.texture);
    material.specular = 0.12;
    for (const auto& triangle : style.triangles) {
        std::array<RenderVertex, 3> vertices;
        for (int corner = 0; corner < 3; ++corner) vertices[corner] = transformed[triangle[corner]];
        rasterizer.triangle(vertices, material);
    }
}

inline double humanRootHeight(const HumanModel& model, const std::vector<Vec3>& rest,
                       const Character& character, const std::vector<Transform>& transforms) {
    if (!character.onToilet) {
        const bool prone = character.prone;
        const double idle = standingIdleWeight(character);
        const double supportWeight = prone || character.crouched || character.charge > 0.001F ||
            !character.grounded ? 1.0 : idle;
        if (supportWeight <= 0.0) return character.elevation;
        double sole = 1e100;
        for (std::size_t index = 0; index < rest.size(); ++index) {
            if (!prone && rest[index].y > 0.18) continue;
            Vec3 point;
            double total = 0, support = 0;
            for (const auto& [bone, weight] : model.rigWeights[index]) {
                if (bone < 0 || static_cast<std::size_t>(bone) >= transforms.size()) continue;
                point += transforms[bone].point(rest[index]) * weight;
                total += weight;
                const std::string& name = model.rigBones[bone].name;
                if (name.rfind("foot.", 0) == 0 || name.rfind("toe", 0) == 0) support += weight;
            }
            if (total > 1e-8 && (prone || support > 0.25)) {
                sole = std::min(sole, point.y / total * kHumanHeightScale);
            }
        }
        const double offset = sole < 1e99 ? -sole * supportWeight + (prone ? 0.025 : 0.0) : 0.0;
        return character.elevation + offset;
    }
    std::vector<bool> supportBone(model.rigBones.size(), false);
    for (std::size_t i = 0; i < model.rigBones.size(); ++i) {
        const std::string& name = model.rigBones[i].name;
        supportBone[i] = name == "root" || name == "spine05" ||
            name == "upperleg01.L" || name == "upperleg01.R" ||
            name == "upperleg02.L" || name == "upperleg02.R";
    }
    double lowest = 1e100;
    for (std::size_t index = 0; index < rest.size(); ++index) {
        const Vec3& source = rest[index];
        if (source.y < 0.55 || source.y > 1.10 || std::abs(source.x) > 0.30 || source.z > 0.02) continue;
        Vec3 point;
        double total = 0, support = 0;
        for (const auto& [bone, weight] : model.rigWeights[index]) {
            if (bone < 0 || static_cast<std::size_t>(bone) >= transforms.size()) continue;
            point += transforms[bone].point(source) * weight;
            total += weight;
            if (supportBone[bone]) support += weight;
        }
        if (support < 0.25 || total < 1e-8) continue;
        const Vec3 local = (point / total) * kHumanHeightScale;
        const double outerX = local.x / 0.1905;
        const double outerZ = (local.z - (0.06 - kToiletSeatForwardOffset)) / 0.270;
        const double innerX = local.x / 0.145;
        const double innerZ = (local.z - (0.06 - kToiletSeatForwardOffset)) / 0.205;
        if (outerX * outerX + outerZ * outerZ > 1.0 || innerX * innerX + innerZ * innerZ < 1.0) continue;
        lowest = std::min(lowest, local.y);
    }
    // Place the real posed buttock/posterior-thigh support surface on the
    // porcelain seat ring, outside the open bowl and inside its front edge.
    return character.elevation + (lowest < 1e99 ? kToiletSeatHeight - lowest : 0.0);
}

}  // namespace gtc::native::human_detail

#endif
