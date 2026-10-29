#pragma once

// The reticle. Four arms and a centre dot, drawn on the host over the finished
// picture, in the view's own coordinates.

#include "client/renderer.h"

namespace gtc::native {

// Draws the reticle at the middle of the frame, with a dark outline so it stays
// readable against bright floors and dark walls alike. Nothing is drawn in god
// view, where the cursor is a command rather than an aim.
void drawCrosshair(NativeFrame& frame, const ViewState& view);

}  // namespace gtc::native
