#include "client/human_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace gtc::native {
namespace {

constexpr double pi = 3.141592653589793;

struct Mat3 {
    std::array<double, 9> v{1, 0, 0, 0, 1, 0, 0, 0, 1};
};

Vec3 apply(const Mat3& m, Vec3 p) {
    return {m.v[0] * p.x + m.v[1] * p.y + m.v[2] * p.z,
            m.v[3] * p.x + m.v[4] * p.y + m.v[5] * p.z,
            m.v[6] * p.x + m.v[7] * p.y + m.v[8] * p.z};
}

Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 result;
    for (int row = 0; row < 3; ++row) for (int column = 0; column < 3; ++column) {
        result.v[row * 3 + column] = 0;
        for (int k = 0; k < 3; ++k) result.v[row * 3 + column] += a.v[row * 3 + k] * b.v[k * 3 + column];
    }
    return result;
}

Mat3 rotationX(double angle) {
    Mat3 result;
    const double c = std::cos(angle), s = std::sin(angle);
    result.v = {1, 0, 0, 0, c, -s, 0, s, c};
    return result;
}

struct Transform {
    Mat3 rotation;
    Vec3 translation;

    Vec3 point(Vec3 value) const { return apply(rotation, value) + translation; }
    Vec3 direction(Vec3 value) const { return apply(rotation, value); }
};

Transform multiply(const Transform& a, const Transform& b) {
    return {multiply(a.rotation, b.rotation), a.point(b.translation)};
}

Transform around(Vec3 pivot, const Mat3& rotation) {
    return {rotation, pivot - apply(rotation, pivot)};
}

Vec3 joint(const std::vector<Vec3>& positions, const std::vector<int>& ids) {
    Vec3 result;
    for (int index : ids) if (index >= 0 && static_cast<std::size_t>(index) < positions.size()) result += positions[index];
    return result / std::max<std::size_t>(1, ids.size());
}

int boneIndex(const HumanModel& model, const std::string& name) {
    for (std::size_t i = 0; i < model.rigBones.size(); ++i) if (model.rigBones[i].name == name) return static_cast<int>(i);
    return -1;
}

std::vector<Transform> pose(const HumanModel& model, const std::vector<Vec3>& rest,
                            const Character& character, std::uint64_t tick) {
    std::vector<Transform> transforms(model.rigBones.size());
    std::vector<int> states(model.rigBones.size(), 0);
    const bool seated = character.onToilet;
    const double movement = seated ? 0.0 : std::clamp(character.speed / 2.8, 0.0, 1.4);
    const double phase = tick * 0.055 * (4.0 + character.speed) + character.id * 0.7;
    const double swing = std::sin(phase) * 0.48 * movement;
    auto local = [&](const RigBone& bone) {
        double angle = 0;
        if (seated) {
            if (bone.name == "upperleg01.L" || bone.name == "upperleg01.R") angle = -1.28;
            if (bone.name == "lowerleg01.L" || bone.name == "lowerleg01.R") angle = 1.28;
            if (bone.name == "upperarm01.L" || bone.name == "upperarm01.R") angle = -0.20;
        } else {
            if (bone.name == "upperleg01.L") angle = -swing;
            if (bone.name == "upperleg01.R") angle = swing;
            if (bone.name == "lowerleg01.L") angle = std::max(0.0, swing) * 0.75;
            if (bone.name == "lowerleg01.R") angle = std::max(0.0, -swing) * 0.75;
            if (bone.name == "upperarm01.L") angle = swing * 0.72;
            if (bone.name == "upperarm01.R") angle = -swing * 0.72;
            if (character.toiletAction == ToiletAction::Piss && bone.name == "upperarm01.R") angle = -0.42;
        }
        return around(joint(rest, bone.head), rotationX(angle));
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

Vec3 rotateWorld(Vec3 point, float yaw) {
    const double c = std::cos(yaw), s = std::sin(yaw);
    return {c * point.x + s * point.z, point.y, -s * point.x + c * point.z};
}

Material skinMaterial(const HumanModel& model, const Character& character, const std::string& root,
                      const std::vector<double>& morphs) {
    static const char* colors[] = {"#f0c8a8", "#d9a17d", "#b97c58", "#8a573e", "#714a38", "#e5b894"};
    Material material;
    material.kind = Material::Kind::Skin;
    material.color = colorHex(colors[std::clamp(character.appearance.skin, 0, 5)]);
    material.specular = 0.035;
    const std::string sex = model.male ? "male" : "female";
    material.young = &loadTexture(root + "/web/assets/human/skin-" + sex + "-young-detail.jpg");
    material.old = &loadTexture(root + "/web/assets/human/skin-" + sex + "-old-detail.jpg");
    material.normalYoung = &loadTexture(root + "/web/assets/human/skin-" + sex + "-young-normal.png");
    material.normalOld = &loadTexture(root + "/web/assets/human/skin-" + sex + "-old-normal.png");
    const int age = model.specIndex("age");
    material.age = age >= 0 && static_cast<std::size_t>(age) < morphs.size() ? morphs[age] : 25;
    return material;
}

Color hairColor(int index) {
    static const char* colors[] = {"#24201e", "#493226", "#75604a", "#b5a075", "#697078",
                                   "#69362c", "#e5d5aa", "#d2b477", "#a9adb0"};
    return colorHex(colors[std::clamp(index, 0, 8)]);
}

bool shirtTriangle(const Character& character, Vec3 point) {
    const Appearance& a = character.appearance;
    const double low = a.dress ? 0.43 : a.coat ? 0.62 : 0.80;
    const double width = a.coat ? 0.47 : 0.42;
    return point.y > low && point.y < 1.49 && std::abs(point.x) < width;
}

bool pantsTriangle(Vec3 point) {
    return point.y > 0.12 && point.y <= 0.86 && std::abs(point.x) < 0.34;
}

void drawHair(Rasterizer& rasterizer, const Camera& camera, const HumanModel& model,
              const HairStyle& style, const std::vector<Vec3>& rest, const Transform& head,
              const Character& character, double rootY) {
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
    for (Vec3& point : positions) point = rotateWorld(head.point(point), character.yaw) +
        Vec3{character.position.x, rootY, character.position.y};
    std::vector<Vec3> normals(positions.size());
    for (const auto& triangle : style.triangles) {
        const Vec3 normal = cross(positions[triangle[1]] - positions[triangle[0]],
                                  positions[triangle[2]] - positions[triangle[0]]);
        for (int index : triangle) normals[index] += normal;
    }
    Material material;
    material.kind = Material::Kind::Hair;
    material.color = hairColor(character.appearance.hairColor) * 1.35;
    material.texture = &loadTexture(style.texture);
    material.specular = 0.12;
    for (const auto& triangle : style.triangles) {
        std::array<RenderVertex, 3> vertices;
        for (int corner = 0; corner < 3; ++corner) {
            const int index = triangle[corner];
            vertices[corner] = cameraVertex(camera, positions[index], normalized(normals[index]),
                                            positions[index], style.vertices[index].uv);
        }
        rasterizer.triangle(vertices, material);
    }
}

} // namespace

void drawHuman(Rasterizer& rasterizer, const Camera& camera, const HumanModel& model,
               const HairLibrary& hair, const Character& character, const std::string& root,
               std::uint64_t tick, bool hideHead) {
    std::vector<int> ids(model.vertices.size());
    for (std::size_t i = 0; i < ids.size(); ++i) ids[i] = static_cast<int>(i);
    std::vector<double> morphs(character.appearance.morphs.begin(), character.appearance.morphs.end());
    const std::vector<Vec3> rest = model.shape(ids, morphs);
    const std::vector<Transform> transforms = pose(model, rest, character, tick);
    std::vector<Vec3> posed = rest;
    for (std::size_t index = 0; index < posed.size(); ++index) {
        double total = 0;
        Vec3 point;
        for (const auto& [bone, weight] : model.rigWeights[index]) {
            if (bone < 0 || static_cast<std::size_t>(bone) >= transforms.size()) continue;
            point += transforms[bone].point(rest[index]) * weight;
            total += weight;
        }
        if (total > 1e-8) posed[index] = point / total;
    }
    const double rootY = character.onToilet ? -0.35 : 0.0;
    auto world = [&](Vec3 point) {
        return rotateWorld(point, character.yaw) + Vec3{character.position.x, rootY, character.position.y};
    };
    for (Vec3& point : posed) point = world(point);
    std::vector<Vec3> normals(posed.size());
    for (const Triangle& triangle : model.triangles) {
        const Vec3 normal = cross(posed[triangle.v[1]] - posed[triangle.v[0]],
                                  posed[triangle.v[2]] - posed[triangle.v[0]]);
        for (int index : triangle.v) normals[index] += normal;
    }
    for (Vec3& normal : normals) normal = normalized(normal);
    const Vec3 eyeRest = (joint(rest, model.eyeSource[0]) + joint(rest, model.eyeSource[1])) * 0.5;
    const Material skin = skinMaterial(model, character, root, morphs);
    static const char* shirts[] = {"#b8cf4d", "#b64e43", "#397987", "#d8cdb5", "#67547c", "#386343", "#c68138"};
    static const char* pants[] = {"#28323a", "#443c37", "#263b55", "#5b5144"};
    Material shirt;
    shirt.color = colorHex(shirts[std::clamp(character.appearance.shirt, 0, 6)]);
    if (character.appearance.suit) shirt.color = colorHex("#25313a");
    if (character.appearance.dress) shirt.color = colorHex("#79465f");
    if (character.appearance.coat) shirt.color = colorHex("#455445");
    Material trousers;
    trousers.color = colorHex(pants[std::clamp(character.appearance.pants, 0, 3)]);
    Material shoes;
    shoes.color = colorHex("#202421");
    shoes.specular = 0.12;
    for (const Triangle& triangle : model.triangles) {
        const Vec3 restCenter = (rest[triangle.v[0]] + rest[triangle.v[1]] + rest[triangle.v[2]]) / 3.0;
        if (hideHead && restCenter.y > eyeRest.y - 0.14) continue;
        auto vertices = [&](double offset) {
            std::array<RenderVertex, 3> result;
            for (int corner = 0; corner < 3; ++corner) {
                const int index = triangle.v[corner];
                result[corner] = cameraVertex(camera, posed[index] + normals[index] * offset, normals[index],
                                              rest[index], model.texcoords[triangle.uv[corner]]);
            }
            return result;
        };
        rasterizer.triangle(vertices(0), skin);
        if (shirtTriangle(character, restCenter)) rasterizer.triangle(vertices(0.009), shirt);
        else if (!character.appearance.dress && pantsTriangle(restCenter)) rasterizer.triangle(vertices(0.008), trousers);
        else if (restCenter.y < 0.13) rasterizer.triangle(vertices(0.009), shoes);
    }
    if (hideHead) return;
    const int headIndex = boneIndex(model, "head");
    const Transform head = headIndex >= 0 ? transforms[headIndex] : Transform{};
    Material scalp;
    scalp.color = hairColor(character.appearance.hairColor) * 0.65;
    if (character.appearance.hair > 0 || character.appearance.beard > 0) {
        for (const Triangle& triangle : model.headTriangles) {
            const Vec3 center = (rest[triangle.v[0]] + rest[triangle.v[1]] + rest[triangle.v[2]]) / 3.0;
            const bool cap = character.appearance.hair > 0 &&
                (center.y > eyeRest.y + 0.095 || (center.z < eyeRest.z - 0.035 && center.y > eyeRest.y - 0.035));
            const bool beard = character.appearance.beard > 0 && center.z > eyeRest.z - 0.025 &&
                center.y < eyeRest.y - 0.075 && center.y > eyeRest.y - (character.appearance.beard == 2 ? 0.29 : 0.19);
            if (!cap && !beard) continue;
            std::array<RenderVertex, 3> vertices;
            for (int corner = 0; corner < 3; ++corner) {
                const int index = triangle.v[corner];
                vertices[corner] = cameraVertex(camera, posed[index] + normals[index] * 0.0025, normals[index], rest[index]);
            }
            rasterizer.triangle(vertices, scalp);
        }
    }
    if (character.appearance.makeup > 0 || character.appearance.lipColor > 0) {
        static const char* makeupColors[] = {"#6b4d45", "#74545f", "#4c6170", "#76505f", "#4f684f"};
        static const char* lipColors[] = {"#9a5b50", "#a44f52", "#763f4c", "#b47162", "#6e3538"};
        Material makeup;
        makeup.color = colorHex(makeupColors[std::clamp(character.appearance.makeup, 0, 4)]);
        Material lips;
        lips.color = colorHex(lipColors[std::clamp(character.appearance.lipColor, 0, 4)]);
        for (const Triangle& triangle : model.headTriangles) {
            const Vec3 center = (rest[triangle.v[0]] + rest[triangle.v[1]] + rest[triangle.v[2]]) / 3.0;
            const bool eyePaint = character.appearance.makeup > 0 && center.z > eyeRest.z - 0.015 &&
                center.y > eyeRest.y - 0.035 && center.y < eyeRest.y + 0.035;
            const bool lipPaint = character.appearance.lipColor > 0 && center.z > eyeRest.z - 0.005 &&
                center.y < eyeRest.y - 0.085 && center.y > eyeRest.y - 0.155 && std::abs(center.x) < 0.05;
            if (!eyePaint && !lipPaint) continue;
            std::array<RenderVertex, 3> vertices;
            for (int corner = 0; corner < 3; ++corner) {
                const int index = triangle.v[corner];
                vertices[corner] = cameraVertex(camera, posed[index] + normals[index] * 0.003, normals[index], rest[index]);
            }
            rasterizer.triangle(vertices, lipPaint ? lips : makeup);
        }
    }
    if (const HairStyle* style = hair.find(character.appearance.hair)) {
        drawHair(rasterizer, camera, model, *style, rest, head, character, rootY);
    }
    static const char* eyeColors[] = {"#34271f", "#6a4d2c", "#496b82", "#4c6854", "#777d82", "#171b1d"};
    Material white; white.color = colorHex("#d8d5ca");
    Material iris; iris.color = colorHex(eyeColors[std::clamp(character.appearance.eyeColor, 0, 5)]);
    Material pupil; pupil.color = colorHex("#090c0d");
    Material glasses; glasses.color = colorHex("#10171a"); glasses.specular = 0.35;
    for (int side = 0; side < 2; ++side) {
        const Vec3 center = world(head.point(joint(rest, model.eyeSource[side])));
        const Vec3 direction = normalized(rotateWorld(head.direction(model.eyeDirection[side]), character.yaw));
        drawEllipsoid(rasterizer, camera, center, Vec3{0.011, 0.011, 0.011}, white, 12, 8);
        drawEllipsoid(rasterizer, camera, center + direction * 0.010, Vec3{0.0054, 0.0054, 0.0054}, iris, 10, 6);
        drawEllipsoid(rasterizer, camera, center + direction * 0.013, Vec3{0.0024, 0.0024, 0.0024}, pupil, 8, 6);
        if (character.appearance.sunglasses) {
            drawEllipsoid(rasterizer, camera, center + direction * 0.017, Vec3{0.018, 0.013, 0.018}, glasses, 12, 8);
        }
    }
    if (character.appearance.beret) {
        Material beret; beret.color = colorHex("#70413d");
        drawEllipsoid(rasterizer, camera, world(head.point(eyeRest + Vec3{0, 0.15, -0.01})),
                      Vec3{0.105, 0.035, 0.095}, beret, 18, 8);
    }
    Material metal;
    metal.color = colorHex("#b99b56");
    metal.specular = 0.32;
    if (character.appearance.earrings) for (int side : {-1, 1}) {
        drawEllipsoid(rasterizer, camera, world(head.point(eyeRest + Vec3{side * 0.072, -0.055, 0})),
                      Vec3{0.009, 0.018, 0.009}, metal, 10, 6);
    }
    if (character.appearance.necklace) {
        for (int i = 0; i < 14; ++i) {
            const double angle = i * 2 * pi / 14;
            drawEllipsoid(rasterizer, camera, world(Vec3{0.066 * std::cos(angle), 1.42,
                0.052 * std::sin(angle)}), Vec3{0.006, 0.006, 0.006}, metal, 6, 4);
        }
        drawEllipsoid(rasterizer, camera, world(Vec3{0, 1.34, 0.075}), Vec3{0.018, 0.025, 0.010}, metal, 10, 6);
    }
    if (character.appearance.tie > 0) {
        static const char* ties[] = {"#7e3838", "#334f72", "#4d683c", "#79613a", "#69436e", "#30383d"};
        Material tie;
        tie.color = colorHex(ties[std::clamp(character.appearance.tie, 0, 5)]);
        drawEllipsoid(rasterizer, camera, world(Vec3{0, 1.20, 0.125}), Vec3{0.026, 0.15, 0.018}, tie, 12, 8);
        drawEllipsoid(rasterizer, camera, world(Vec3{0, 1.37, 0.12}), Vec3{0.035, 0.032, 0.022}, tie, 10, 6);
    }
    if (character.appearance.fur) {
        Material fur;
        fur.color = colorHex("#a3937c");
        for (int i = 0; i < 16; ++i) {
            const double angle = i * 2 * pi / 16;
            drawEllipsoid(rasterizer, camera, world(Vec3{0.12 * std::cos(angle), 1.39,
                0.085 * std::sin(angle)}), Vec3{0.035, 0.030, 0.035}, fur, 8, 5);
        }
    }
}

} // namespace gtc::native
