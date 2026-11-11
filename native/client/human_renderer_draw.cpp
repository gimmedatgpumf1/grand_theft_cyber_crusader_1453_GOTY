// One human, drawn: the pose is solved, the body is lofted bone by bone, then
// the hair, the skin and whatever the character is carrying go on top. It is the
// longest drawing routine in the client, which is exactly why it is a file.

#include "client/human_renderer.h"

#include "client/blockbody.h"
#include "client/human_materials.h"
#include "client/human_palette.h"
#include "client/human_renderer_pose.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gtc::native {

using namespace human_detail;

void drawHuman(Rasterizer& rasterizer, const Camera& camera, const HumanModel& model,
               const HairLibrary& hair, const Character& character, HumanRenderCache& cache,
               const std::string& root, double tick, bool hideHead) {
    std::vector<double> morphs(character.appearance.morphs.begin(), character.appearance.morphs.end());
    prepareHuman(model, character, cache);
    const std::vector<Vec3>& rest = cache.rest;
    const std::vector<Transform> transforms = pose(model, rest, character, tick);
    const bool freshPose = !cache.poseValid || cache.poseTick != tick ||
        cache.poseX != character.position.x || cache.poseZ != character.position.y ||
        cache.poseYaw != character.yaw || cache.poseSpeed != character.speed ||
        cache.poseElevation != character.elevation || cache.poseCharge != character.charge ||
        cache.poseGrabHeight != character.grabHeight || cache.posePitch != character.pitch ||
        cache.posePissYaw != character.pissYaw || cache.posePissPitch != character.pissPitch ||
        cache.poseAttackAnim != character.attackAnim ||
        cache.poseAction != static_cast<int>(character.toiletAction) || cache.poseOnToilet != character.onToilet ||
        cache.poseGrounded != character.grounded || cache.poseCrouched != character.crouched ||
        cache.poseProne != character.prone || cache.poseGrabbing != character.grabbing ||
        cache.poseUnzipped != character.unzipped || cache.poseBlocking != character.blocking ||
        cache.posePissing != character.pissing;
    const double rootY = humanRootHeight(model, rest, character, transforms);
    auto world = [&](Vec3 point) {
        return rotateWorld(point * kHumanHeightScale, character.yaw) +
            Vec3{character.position.x, rootY, character.position.y};
    };
    const int rootIndex = boneIndex(model, "root");
    const Transform rootTransform = rootIndex >= 0 ? transforms[rootIndex] : Transform{};
    const auto rootWorld = [&](Vec3 point) { return world(rootTransform.point(point)); };
    if (freshPose) {
        cache.posed = rest;
        cache.posedNormals.resize(rest.size());
        for (std::size_t index = 0; index < rest.size(); ++index) {
            double total = 0;
            Vec3 point, normal;
            for (const auto& [bone, weight] : model.rigWeights[index]) {
                if (bone < 0 || static_cast<std::size_t>(bone) >= transforms.size()) continue;
                point += transforms[bone].point(rest[index]) * weight;
                normal += transforms[bone].direction(cache.normals[index]) * weight;
                total += weight;
            }
            if (total > 1e-8) {
                cache.posed[index] = world(point / total);
                cache.posedNormals[index] = rotateWorld(normalized(normal), character.yaw);
            } else {
                cache.posed[index] = rootWorld(rest[index]);
                cache.posedNormals[index] = rotateWorld(rootTransform.direction(cache.normals[index]), character.yaw);
            }
        }
        cache.poseTick = tick;
        cache.poseX = character.position.x;
        cache.poseZ = character.position.y;
        cache.poseYaw = character.yaw;
        cache.poseSpeed = character.speed;
        cache.poseElevation = character.elevation;
        cache.poseCharge = character.charge;
        cache.poseGrabHeight = character.grabHeight;
        cache.posePitch = character.pitch;
        cache.posePissYaw = character.pissYaw; cache.posePissPitch = character.pissPitch;
        cache.poseAttackAnim = character.attackAnim;
        cache.poseUnzipped = character.unzipped;
        cache.posePissing = character.pissing;
        cache.poseBlocking = character.blocking;
        cache.poseAction = static_cast<int>(character.toiletAction);
        cache.poseOnToilet = character.onToilet;
        cache.poseGrounded = character.grounded;
        cache.poseCrouched = character.crouched;
        cache.poseProne = character.prone;
        cache.poseGrabbing = character.grabbing;
        cache.poseValid = true;
        const bool exposed = character.appearance.male &&
            (character.unzipped || character.pissing || character.toiletAction == ToiletAction::Piss);
        cache.genitalPivot = cache.genitalTip = -1;
        if (exposed && !character.onToilet && !model.genitalTriangles.empty()) {
            std::vector<int> ids;
            for (const Triangle& triangle : model.genitalTriangles) for (int id : triangle.v) ids.push_back(id);
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            std::unordered_map<int, std::vector<int>> edges;
            for (const Triangle& triangle : model.genitalTriangles) for (int corner = 0; corner < 3; ++corner) {
                const int a = triangle.v[corner], b = triangle.v[(corner + 1) % 3];
                edges[a].push_back(b); edges[b].push_back(a);
            }
            const auto central = [&](int id) { return std::abs(rest[id].x) < 0.015; };
            int tipId = ids.front();
            for (int id : ids) if (rest[id].z > rest[tipId].z) tipId = id;
            // The urethral opening is the ventral midline point of the distal
            // cap, roughly 16 mm below the dorsal apex. Emitting from the apex
            // drew the ribbon out of the top of the glans, which reads as the
            // stream starting above the organ.
            int meatusId = tipId;
            for (int id : ids) {
                if (std::abs(rest[id].x) > 0.004 || rest[id].z < rest[tipId].z - 0.006) continue;
                if (rest[id].y < rest[meatusId].y) meatusId = id;
            }
            int ventralId = tipId;
            for (int id : edges[tipId]) if (central(id) && rest[id].y < rest[tipId].y &&
                (ventralId == tipId || rest[id].z > rest[ventralId].z)) ventralId = id;
            // The pivot is the midline shaft landmark that the core emitter
            // constants are measured from, so organ and droplets agree.
            int rootId = tipId, previous = -1;
            for (int step = 0; step < 4; ++step) {
                int next = -1;
                for (int id : edges[rootId]) if (id != previous && central(id) && rest[id].y > rest[rootId].y &&
                    rest[id].z < rest[rootId].z && (next < 0 || rest[id].z > rest[next].z)) next = id;
                if (next < 0) break;
                previous = rootId; rootId = next;
            }
            int lowerRootId = ventralId; previous = -1;
            for (int step = 0; step < 7; ++step) {
                int next = -1;
                for (int id : edges[lowerRootId]) if (id != previous && central(id) && rest[id].z < rest[lowerRootId].z &&
                    (next < 0 || rest[id].z > rest[next].z)) next = id;
                if (next < 0) break;
                previous = lowerRootId; lowerRootId = next;
            }
            // The visible stream must leave the urethral opening exactly.
            const Vec3 exit = cache.posed[meatusId];
            cache.genitalPivot = rootId; cache.genitalTip = meatusId;
            const Vec3 pivot = (cache.posed[rootId] + cache.posed[lowerRootId]) * 0.5;
            const Vec3 restDirection = rotateWorld(exit - pivot, -character.yaw);
            const bool active = character.pissing || character.toiletAction == ToiletAction::Piss;
            const double yaw = active ? character.pissYaw : character.yaw;
            const double pitch = active ? character.pissPitch : character.pitch;
            const Vec3 desired = rotateWorld({std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                              std::cos(pitch) * std::cos(yaw)}, -character.yaw);
            const Mat3 rotation = rotationBetween(restDirection, desired);
            auto articulate = [&](Vec3 direction) {
                return rotateWorld(apply(rotation, rotateWorld(direction, -character.yaw)), character.yaw);
            };
            for (int id : ids) {
                cache.posed[id] = pivot + articulate(cache.posed[id] - pivot);
                cache.posedNormals[id] = normalized(articulate(cache.posedNormals[id]));
            }
            cache.genitalRoot = pivot;
            // The opening itself is the emit point; the ribbon is drawn from here,
            // so it cannot begin in front of the organ even if skinning puts the
            // posed pelvis a few millimetres off the logical body axis.
            cache.genitalExit = pivot + articulate(exit - pivot);
        }
    }
    cache.transformed.resize(rest.size());
    auto& transformed = cache.transformed;
    for (std::size_t index = 0; index < rest.size(); ++index) {
        transformed[index] = cameraVertex(camera, cache.posed[index], cache.posedNormals[index], rest[index]);
    }
    const Vec3 eyeRest = (joint(rest, model.eyeSource[0]) + joint(rest, model.eyeSource[1])) * 0.5;
    // The authored body has no imported skin atlas: its surface is a flat local
    // colour, so the texture and normal-map lookups are skipped entirely.
    Material plainSkin;
    plainSkin.kind = Material::Kind::Skin;
    plainSkin.color = skinColor(character.appearance.skin);
    plainSkin.specular = 0.035;
    const Material skin = model.blockBody ? plainSkin : skinMaterial(model, character, root, morphs);
    static const char* shirts[] = {"#20262b", "#b3453c", "#56765f", "#c4b58d", "#334c6b", "#6b4c70", "#c7ccd1"};
    static const char* pants[] = {"#303b44", "#2d3338", "#4a453e", "#23384a"};
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
                result[corner].view += transformed[index].normal * (offset * kHumanHeightScale);
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
    const bool showGenitals = character.appearance.male &&
        (character.unzipped || character.pissing || character.toiletAction == ToiletAction::Piss);
    // The authored body is a finished figure: it carries its own clothes, its
    // own block trousers and its own closed crotch, so none of the imported
    // mesh's bolt-on garments apply. Drawing them over it is what buried it in a
    // black skirt.
    if (model.blockBody) {
        if (showGenitals) {
            for (const Triangle& triangle : model.genitalTriangles) {
                std::array<RenderVertex, 3> vertices;
                for (int corner = 0; corner < 3; ++corner) {
                    const int index = triangle.v[corner];
                    vertices[corner] = transformed[index];
                }
                rasterizer.triangle(vertices, skin);
            }
        }
        const int boneHead = boneIndex(model, "head");
        const Transform headTransform = boneHead >= 0 ? transforms[boneHead] : rootTransform;
        drawBlockAccessories(rasterizer, camera,
                             [&](Vec3 point) { return world(headTransform.point(point)); }, character);
        return;
    }
    if (showGenitals) {
        for (const Triangle& triangle : model.genitalTriangles) {
            std::array<RenderVertex, 3> vertices;
            for (int corner = 0; corner < 3; ++corner) {
                const int index = triangle.v[corner];
                vertices[corner] = transformed[index];
                vertices[corner].uv = model.texcoords[triangle.uv[corner]];
            }
            rasterizer.triangle(vertices, skin);
        }
    } else if (character.appearance.male && !model.genitalTriangles.empty()) {
        double minZ = 1e100, maxZ = -1e100;
        for (const Triangle& triangle : model.genitalTriangles) for (int id : triangle.v) {
            minZ = std::min(minZ, rest[id].z); maxZ = std::max(maxZ, rest[id].z);
        }
        int pivot = model.genitalTriangles.front().v[0];
        for (const Triangle& triangle : model.genitalTriangles) for (int id : triangle.v) {
            if (rest[id].z <= minZ + 0.3 * (maxZ - minZ) && rest[id].y > rest[pivot].y) pivot = id;
        }
        const Vec3 center = cache.posed[pivot];
        const Vec3 forward{std::sin(character.yaw), 0, std::cos(character.yaw)};
        const Vec3 right{std::cos(character.yaw), 0, -std::sin(character.yaw)};
        const Vec3 rows[3][3] = {
            {center - right * 0.105 - forward * 0.018 + Vec3{0, 0.085, 0},
             center + forward * 0.050 + Vec3{0, 0.065, 0},
             center + right * 0.105 - forward * 0.018 + Vec3{0, 0.085, 0}},
            {center - right * 0.102 - forward * 0.012 + Vec3{0, -0.015, 0},
             center + forward * 0.115 + Vec3{0, -0.010, 0},
             center + right * 0.102 - forward * 0.012 + Vec3{0, -0.015, 0}},
            {center - right * 0.085 - forward * 0.004 + Vec3{0, -0.115, 0},
             center + forward * 0.045 + Vec3{0, -0.105, 0},
             center + right * 0.085 - forward * 0.004 + Vec3{0, -0.115, 0}}};
        Material tent = trousers;
        tent.kind = Material::Kind::Textured;
        auto fold = [&](Vec3 a, Vec3 b, Vec3 c) {
            const Vec3 normal = normalized(cross(b - a, c - a));
            rasterizer.triangle({cameraVertex(camera, a, normal, a, {0, 0}),
                cameraVertex(camera, b, normal, b, {1, 0}), cameraVertex(camera, c, normal, c, {0.5, 1})}, tent);
        };
        for (int row = 0; row < 2; ++row) for (int side = 0; side < 2; ++side) {
            fold(rows[row][side], rows[row + 1][side], rows[row + 1][side + 1]);
            fold(rows[row][side], rows[row + 1][side + 1], rows[row][side + 1]);
        }
    }
    const int collarBone = boneIndex(model, "neck01");
    const Transform collarTransform = collarBone >= 0 ? transforms[collarBone] : rootTransform;
    const auto collarWorld = [&](Vec3 point) { return world(collarTransform.point(point)); };
    const int torsoBone = boneIndex(model, "spine01");
    const Transform torsoTransform = torsoBone >= 0 ? transforms[torsoBone] : rootTransform;
    const auto torsoWorld = [&](Vec3 point) { return world(torsoTransform.point(point)); };
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
    drawLoft(rasterizer, camera, rootWorld, {{0.945, beltX, beltZ}, {0.975, beltX, beltZ}}, belt);
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
    drawLoft(rasterizer, camera, collarWorld, {{neckY - 0.018, collarX, collarZ},
        {neckY + collarHeight, collarX * 0.93, collarZ * 0.94}}, collar);
    if (character.appearance.coat) {
        Material coat;
        coat.kind = Material::Kind::Cloth;
        coat.color = shirtBase * 0.82;
        coat.texture = &fabricTexture();
        drawLoft(rasterizer, camera, rootWorld, {{0.43, 0.34 * garmentX, 0.21 * garmentZ},
            {0.70, 0.29 * garmentX, 0.18 * garmentZ}, {0.98, 0.23 * garmentX, 0.15 * garmentZ}},
                 coat, pi / 2 + 0.42, 2 * pi - 0.84);
    }
    if (character.appearance.dress) {
        Material dress;
        dress.kind = Material::Kind::Cloth;
        dress.color = shirtBase;
        dress.texture = &fabricTexture();
        drawLoft(rasterizer, camera, rootWorld, {{0.40, 0.36 * garmentX, 0.25 * garmentZ},
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
                vertices[corner].view += transformed[index].normal * (0.0025 * kHumanHeightScale);
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
                vertices[corner].view += transformed[index].normal * (0.003 * kHumanHeightScale);
            }
            rasterizer.triangle(vertices, lipPaint ? lips : makeup);
        }
    }
    if (const HairStyle* style = hair.find(character.appearance.hair)) {
        drawHair(rasterizer, camera, model, *style, rest, head, character, world);
    }
    static const char* eyeColors[] = {"#34271f", "#6a4d2c", "#496b82", "#4c6854", "#777d82", "#171b1d"};
    Material white; white.color = colorHex("#d8d5ca");
    Material iris; iris.color = colorHex(eyeColors[std::clamp(character.appearance.eyeColor, 0, 5)]);
    Material pupil; pupil.color = colorHex("#090c0d");
    Material glasses; glasses.color = colorHex("#10171a"); glasses.specular = 0.35;
    auto accessory = [&](Vec3 center, Vec3 radii, const Material& material, int segments, int rings) {
        drawEllipsoid(rasterizer, camera, center, radii * kHumanHeightScale, material, segments, rings);
    };
    for (int side = 0; side < 2; ++side) {
        const Vec3 center = world(head.point(joint(rest, model.eyeSource[side])));
        const Vec3 direction = normalized(rotateWorld(head.direction(model.eyeDirection[side]), character.yaw));
        accessory(center, Vec3{0.011, 0.011, 0.011}, white, 12, 8);
        accessory(center + direction * (0.010 * kHumanHeightScale), Vec3{0.0054, 0.0054, 0.0054}, iris, 10, 6);
        accessory(center + direction * (0.013 * kHumanHeightScale), Vec3{0.0024, 0.0024, 0.0024}, pupil, 8, 6);
        if (character.appearance.sunglasses) {
            accessory(center + direction * (0.017 * kHumanHeightScale), Vec3{0.018, 0.013, 0.018}, glasses, 12, 8);
        }
    }
    if (character.appearance.beret) {
        Material beret; beret.color = colorHex("#70413d");
        accessory(world(head.point(eyeRest + Vec3{0, 0.15, -0.01})),
                      Vec3{0.105, 0.035, 0.095}, beret, 18, 8);
    }
    Material metal;
    metal.color = colorHex("#b99b56");
    metal.specular = 0.32;
    if (character.appearance.earrings) for (int side : {-1, 1}) {
        accessory(world(head.point(eyeRest + Vec3{side * 0.072, -0.055, 0})),
                      Vec3{0.009, 0.018, 0.009}, metal, 10, 6);
    }
    if (character.appearance.necklace) {
        for (int i = 0; i < 14; ++i) {
            const double angle = i * 2 * pi / 14;
            accessory(collarWorld(Vec3{0.066 * std::cos(angle), 1.42,
                0.052 * std::sin(angle)}), Vec3{0.006, 0.006, 0.006}, metal, 6, 4);
        }
        accessory(torsoWorld(Vec3{0, 1.34, 0.075}), Vec3{0.018, 0.025, 0.010}, metal, 10, 6);
    }
    if (character.appearance.tie > 0) {
        static const char* ties[] = {"#20262b", "#243b60", "#7d2528", "#375a80", "#454c55", "#b2924a"};
        Material tie;
        tie.color = colorHex(ties[std::clamp(character.appearance.tie, 0, 5)]);
        accessory(torsoWorld(Vec3{0, 1.20, 0.125}), Vec3{0.026, 0.15, 0.018}, tie, 12, 8);
        accessory(torsoWorld(Vec3{0, 1.37, 0.12}), Vec3{0.035, 0.032, 0.022}, tie, 10, 6);
    }
    if (character.appearance.fur) {
        Material fur;
        fur.color = colorHex("#a3937c");
        for (int i = 0; i < 16; ++i) {
            const double angle = i * 2 * pi / 16;
            accessory(collarWorld(Vec3{0.12 * std::cos(angle), 1.39,
                0.085 * std::sin(angle)}), Vec3{0.035, 0.030, 0.035}, fur, 8, 5);
        }
    }
}

}  // namespace gtc::native
