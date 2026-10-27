#pragma once

#include "client/raster.h"
#include "core/types.h"
#include "hair.h"

#include <cstdint>
#include <string>

namespace gtc::native {

void drawHuman(Rasterizer& rasterizer, const Camera& camera, const HumanModel& model,
               const HairLibrary& hair, const Character& character, const std::string& root,
               std::uint64_t tick, bool hideHead);

} // namespace gtc::native
