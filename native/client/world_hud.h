#pragma once

// renderer.h comes first on purpose: it pulls in client.h before any header
// that could introduce gtc::native::Vec2, which is what keeps ClientState the
// same size in every translation unit.
#include "client/renderer.h"
#include "client/raster.h"

#include <cstdint>
#include <string>

namespace gtc::native {

// The overlay: name tags, crosshair and the four corner panels. Kept apart from
// world_renderer.cpp so the 3D pass stays readable and neither file grows past
// the size the naggers allow.

void drawLabels(NativeFrame& frame, const Camera& camera, const ClientState& state, std::uint32_t localId);
void drawCrosshair(NativeFrame& frame, const ViewState& view);
void drawHud(NativeFrame& frame, const ClientWorld& world, const ClientState& state,
             std::uint32_t localId, const ViewState& view);

}  // namespace gtc::native
