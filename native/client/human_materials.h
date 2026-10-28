#pragma once

#include "native.h"

#include "client/raster.h"
#include "core/types.h"

#include <string>
#include <vector>

namespace gtc::native {

// Surfaces the imported body is painted with. They are kept out of the body
// renderer so the renderer stays a renderer and this file stays a paint shop.

const Texture& fabricTexture();
Material skinMaterial(const HumanModel& model, const Character& character, const std::string& root,
                      const std::vector<double>& morphs);

}  // namespace gtc::native
