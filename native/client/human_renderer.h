#pragma once

#include "client/raster.h"
#include "core/types.h"
#include "hair.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gtc::native {

struct HumanRenderCache {
    bool valid = false;
    bool male = true;
    std::array<int, 128> morphs{};
    std::vector<Vec3> rest;
    std::vector<Vec3> normals;
};

void drawHuman(Rasterizer& rasterizer, const Camera& camera, const HumanModel& model,
               const HairLibrary& hair, const Character& character, HumanRenderCache& cache,
               const std::string& root, std::uint64_t tick, bool hideHead);

} // namespace gtc::native
