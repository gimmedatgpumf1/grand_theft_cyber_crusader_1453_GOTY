#pragma once

#include "client/raster.h"
#include "core/types.h"
#include "hair.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gtc::native {

inline constexpr double kEyeInHeadRange = 30.0 * 3.141592653589793 / 180.0;

struct HumanRenderCache {
    bool valid = false;
    bool male = true;
    std::array<int, 128> morphs{};
    std::vector<Vec3> rest;
    std::vector<Vec3> normals;
    bool poseValid = false;
    double poseTick = 0;
    float poseX = 0, poseZ = 0, poseYaw = 0, poseSpeed = 0;
    float poseElevation = 0, poseCharge = 0, poseGrabHeight = 0;
    float posePitch = 0, poseAttackAnim = 0;
    int poseAction = -1;
    bool poseOnToilet = false;
    bool poseGrounded = true, poseCrouched = false, poseProne = false, poseGrabbing = false;
    bool poseUnzipped = false, poseBlocking = false;
    int genitalPivot = -1, genitalTip = -1;
    std::vector<Vec3> posed;
    std::vector<Vec3> posedNormals;
    std::vector<RenderVertex> transformed;
};

void prepareHuman(const HumanModel& model, const Character& character, HumanRenderCache& cache);
struct HumanGazePose {
    Vec3 eye;
    Vec3 chin;
    Vec3 manubrium;
};
HumanGazePose humanGazePose(const HumanModel& model, const std::vector<Vec3>& rest,
                            const Character& character, double tick,
                            double headYaw = 0, double gazePitch = 0);
Vec3 humanEyePosition(const HumanModel& model, const std::vector<Vec3>& rest,
                      const Character& character, double tick,
                      double headYaw = 0, double gazePitch = 0);

void drawHuman(Rasterizer& rasterizer, const Camera& camera, const HumanModel& model,
               const HairLibrary& hair, const Character& character, HumanRenderCache& cache,
               const std::string& root, double tick, bool hideHead);

} // namespace gtc::native
