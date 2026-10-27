#include "client/human_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
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

const Texture& agedSkinTexture(const std::string& root, const std::string& sex, int age, bool normal) {
    const double old = std::clamp((age - 24) / 58.0, 0.0, 1.0);
    const double blend = old * old * (3 - 2 * old) * (normal ? 1.0 : 0.94);
    const std::string key = root + sex + std::to_string(age) + (normal ? "normal" : "detail");
    static std::map<std::string, Texture> cache;
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    const std::string base = root + "/web/assets/human/skin-" + sex;
    const Texture& young = loadTexture(base + (normal ? "-young-normal.png" : "-young-detail.jpg"));
    const Texture& aged = loadTexture(base + (normal ? "-old-normal.png" : "-old-detail.jpg"));
    if (young.width != aged.width || young.height != aged.height || young.pixels.empty()) return young;
    Texture result;
    result.width = young.width;
    result.height = young.height;
    result.pixels.resize(young.pixels.size());
    for (std::size_t i = 0; i < result.pixels.size(); ++i) {
        result.pixels[i] = static_cast<unsigned char>(std::lround(young.pixels[i] * (1 - blend) + aged.pixels[i] * blend));
    }
    return cache.emplace(key, std::move(result)).first->second;
}

Material skinMaterial(const HumanModel& model, const Character& character, const std::string& root,
                      const std::vector<double>& morphs) {
    static const char* colors[] = {"#f0c8a8", "#d9a17d", "#b97c58", "#8a573e", "#714a38", "#e5b894"};
    Material material;
    material.kind = Material::Kind::Skin;
    material.color = colorHex(colors[std::clamp(character.appearance.skin, 0, 5)]);
    material.specular = 0.035;
    const std::string sex = model.male ? "male" : "female";
    const int ageIndex = model.specIndex("age");
    const int age = ageIndex >= 0 && static_cast<std::size_t>(ageIndex) < morphs.size() ?
        static_cast<int>(morphs[ageIndex]) : 25;
    material.texture = &agedSkinTexture(root, sex, age, false);
    material.normalYoung = &agedSkinTexture(root, sex, age, true);
    material.age = age;
    return material;
}

const Texture& fabricTexture() {
    static const Texture texture = [] {
        Texture result;
        result.width = result.height = 128;
        result.pixels.resize(128U * 128U * 4U, 255);
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
            const bool vertical = x % 4 == 0, horizontal = y % 4 == 0;
            const bool dark = (vertical && x % 8) || (horizontal && y % 8);
            const unsigned char value = (vertical || horizontal) ? (dark ? 237 : 245) : 244;
            const std::size_t offset = (static_cast<std::size_t>(y) * 128 + x) * 4;
            result.pixels[offset] = result.pixels[offset + 1] = result.pixels[offset + 2] = value;
        }
        return result;
    }();
    return texture;
}

Color hairColor(int index) {
    static const char* colors[] = {"#171a1c", "#3b281d", "#75604a", "#b5a075", "#5b6268",
                                   "#69362c", "#e5d5aa", "#d2b477", "#a9adb0"};
    return colorHex(colors[std::clamp(index, 0, 8)]);
}

struct LoftRing { double y, x, z; };

void drawLoft(Rasterizer& rasterizer, const Camera& camera, const std::function<Vec3(Vec3)>& world,
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

} // namespace

void drawHuman(Rasterizer& rasterizer, const Camera& camera, const HumanModel& model,
               const HairLibrary& hair, const Character& character, HumanRenderCache& cache,
               const std::string& root, std::uint64_t tick, bool hideHead) {
    std::vector<double> morphs(character.appearance.morphs.begin(), character.appearance.morphs.end());
    if (!cache.valid || cache.male != character.appearance.male || cache.morphs != character.appearance.morphs) {
        std::vector<int> ids(model.vertices.size());
        for (std::size_t i = 0; i < ids.size(); ++i) ids[i] = static_cast<int>(i);
        cache.rest = model.shape(ids, morphs);
        cache.normals.assign(cache.rest.size(), Vec3{});
        for (const Triangle& triangle : model.triangles) {
            const Vec3 normal = cross(cache.rest[triangle.v[1]] - cache.rest[triangle.v[0]],
                                      cache.rest[triangle.v[2]] - cache.rest[triangle.v[0]]);
            for (int index : triangle.v) cache.normals[index] += normal;
        }
        for (Vec3& normal : cache.normals) normal = normalized(normal);
        cache.morphs = character.appearance.morphs;
        cache.male = character.appearance.male;
        cache.valid = true;
    }
    const std::vector<Vec3>& rest = cache.rest;
    const std::vector<Transform> transforms = pose(model, rest, character, tick);
    std::vector<Vec3> posed = rest;
    std::vector<Vec3> normals(posed.size());
    for (std::size_t index = 0; index < posed.size(); ++index) {
        double total = 0;
        Vec3 point, normal;
        for (const auto& [bone, weight] : model.rigWeights[index]) {
            if (bone < 0 || static_cast<std::size_t>(bone) >= transforms.size()) continue;
            point += transforms[bone].point(rest[index]) * weight;
            normal += transforms[bone].direction(cache.normals[index]) * weight;
            total += weight;
        }
        if (total > 1e-8) {
            posed[index] = point / total;
            normals[index] = rotateWorld(normalized(normal), character.yaw);
        } else {
            normals[index] = rotateWorld(cache.normals[index], character.yaw);
        }
    }
    const double rootY = character.onToilet ? -0.35 : 0.0;
    auto world = [&](Vec3 point) {
        return rotateWorld(point, character.yaw) + Vec3{character.position.x, rootY, character.position.y};
    };
    for (Vec3& point : posed) point = world(point);
    std::vector<RenderVertex> transformed(posed.size());
    for (std::size_t index = 0; index < posed.size(); ++index) {
        transformed[index] = cameraVertex(camera, posed[index], normals[index], rest[index]);
    }
    const Vec3 eyeRest = (joint(rest, model.eyeSource[0]) + joint(rest, model.eyeSource[1])) * 0.5;
    const Material skin = skinMaterial(model, character, root, morphs);
    static const char* shirts[] = {"#20262b", "#b3453c", "#56765f", "#c4b58d", "#334c6b", "#6b4c70", "#c7ccd1"};
    static const char* pants[] = {"#1c2328", "#2d3338", "#4a453e", "#23384a"};
    Material shirt;
    shirt.kind = Material::Kind::Cloth;
    shirt.specular = 0.0;
    shirt.texture = &fabricTexture();
    const Color shirtBase = colorHex(shirts[std::clamp(character.appearance.shirt, 0, 6)]);
    shirt.color = shirtBase;
    if (character.appearance.suit) shirt.color = shirt.color * 1.18;
    if (character.appearance.coat) shirt.color = shirt.color * 0.82;
    Material trousers;
    trousers.kind = Material::Kind::Cloth;
    trousers.specular = 0.0;
    trousers.texture = &fabricTexture();
    trousers.color = colorHex(pants[std::clamp(character.appearance.pants, 0, 3)]);
    Material shoes;
    shoes.color = colorHex("#202421");
    shoes.specular = 0.12;
    const bool coveredSleeve = character.appearance.coat || character.appearance.shirt % 2 == 0;
    for (std::size_t triangleIndex = 0; triangleIndex < model.triangles.size(); ++triangleIndex) {
        const Triangle& triangle = model.triangles[triangleIndex];
        const Vec3 restCenter = (rest[triangle.v[0]] + rest[triangle.v[1]] + rest[triangle.v[2]]) / 3.0;
        if (hideHead && restCenter.y > eyeRest.y - 0.14) continue;
        auto vertices = [&](double offset) {
            std::array<RenderVertex, 3> result;
            for (int corner = 0; corner < 3; ++corner) {
                const int index = triangle.v[corner];
                result[corner] = transformed[index];
                result[corner].view += transformed[index].normal * offset;
                result[corner].uv = model.texcoords[triangle.uv[corner]];
            }
            return result;
        };
        const BodyMaterial region = triangleIndex < model.bodyMaterials.size() ?
            model.bodyMaterials[triangleIndex] : BodyMaterial::Skin;
        if (region == BodyMaterial::Shirt || region == BodyMaterial::Torso ||
            (region == BodyMaterial::Sleeve && coveredSleeve)) rasterizer.triangle(vertices(0.009), shirt);
        else if (region == BodyMaterial::Pants) rasterizer.triangle(vertices(0.008), trousers);
        else if (region == BodyMaterial::Boots) rasterizer.triangle(vertices(0.009), shoes);
        else rasterizer.triangle(vertices(0), skin);
    }
    Material belt;
    belt.kind = Material::Kind::Cloth;
    belt.color = colorHex("#151a1d");
    belt.texture = &fabricTexture();
    double beltX = 0.24, beltZ = 0.166;
    for (std::size_t triangleIndex = 0; triangleIndex < model.triangles.size(); ++triangleIndex) {
        const BodyMaterial region = triangleIndex < model.bodyMaterials.size() ?
            model.bodyMaterials[triangleIndex] : BodyMaterial::Skin;
        if (region != BodyMaterial::Pants && region != BodyMaterial::Torso) continue;
        for (int index : model.triangles[triangleIndex].v) {
            const Vec3& point = rest[index];
            if (point.y < 0.88 || point.y > 1.04 || std::abs(point.x) > 0.48) continue;
            beltX = std::max(beltX, std::abs(point.x) + 0.012);
            beltZ = std::max(beltZ, std::abs(point.z) + 0.015);
        }
    }
    drawLoft(rasterizer, camera, world, {{0.945, beltX, beltZ}, {0.975, beltX, beltZ}}, belt);
    const double garmentX = std::clamp(beltX / 0.24, 1.0, 1.8);
    const double garmentZ = std::clamp(beltZ / 0.166, 1.0, 1.8);
    double neckY = 1.42;
    const int neckIndex = boneIndex(model, "neck01");
    if (neckIndex >= 0) neckY = joint(rest, model.rigBones[neckIndex].head).y - 0.85 * model.scale;
    Material collar;
    collar.kind = Material::Kind::Cloth;
    collar.color = character.appearance.suit ? colorHex("#dfe3df") : shirtBase;
    collar.texture = &fabricTexture();
    const double collarHeight = character.appearance.beret ? 0.035 : character.appearance.suit ? 0.026 : 0.020;
    const double collarX = character.appearance.suit ? 0.112 : 0.094;
    const double collarZ = character.appearance.suit ? 0.095 : 0.082;
    drawLoft(rasterizer, camera, world, {{neckY - 0.018, collarX, collarZ},
        {neckY + collarHeight, collarX * 0.93, collarZ * 0.94}}, collar);
    if (character.appearance.coat) {
        Material coat;
        coat.kind = Material::Kind::Cloth;
        coat.color = shirtBase * 0.82;
        coat.texture = &fabricTexture();
        drawLoft(rasterizer, camera, world, {{0.43, 0.34 * garmentX, 0.21 * garmentZ},
            {0.70, 0.29 * garmentX, 0.18 * garmentZ}, {0.98, 0.23 * garmentX, 0.15 * garmentZ}},
                 coat, pi / 2 + 0.42, 2 * pi - 0.84);
    }
    if (character.appearance.dress) {
        Material dress;
        dress.kind = Material::Kind::Cloth;
        dress.color = shirtBase;
        dress.texture = &fabricTexture();
        drawLoft(rasterizer, camera, world, {{0.40, 0.36 * garmentX, 0.25 * garmentZ},
            {0.72, 0.29 * garmentX, 0.21 * garmentZ}, {1.04, 0.22 * garmentX, 0.16 * garmentZ}}, dress);
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
                vertices[corner] = transformed[index];
                vertices[corner].view += transformed[index].normal * 0.0025;
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
                vertices[corner] = transformed[index];
                vertices[corner].view += transformed[index].normal * 0.003;
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
        static const char* ties[] = {"#20262b", "#243b60", "#7d2528", "#375a80", "#454c55", "#b2924a"};
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
