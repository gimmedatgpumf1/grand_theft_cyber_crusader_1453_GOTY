#include "client/crosshair.h"

#include "client/interface_style.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {
namespace {

// How thick and how long, for a frame of this size: one pixel per 640 of the
// smaller side, so the reticle grows with the resolution instead of disappearing
// into a 1080p picture.
int strokeUnit(const NativeFrame& frame) {
    const int smaller = std::min(frame.width, frame.height);
    return std::clamp(static_cast<int>(std::lround(smaller / 640.0)), 1, 4);
}

}  // namespace

void drawCrosshair(NativeFrame& frame, const ViewState& view) {
    if (view.mode == ViewMode::God) return;
    // The middle of the window. The orbit camera has no reticle of its own: it
    // previews a direction, and a marker floating over the scene for it was a
    // second crosshair to keep in sync with the first.
    int centreX = frame.width / 2, centreY = frame.height / 2;
    const int unit = strokeUnit(frame);
    const int gap = 3 * unit, arm = 7 * unit, length = arm - gap + 1;
    centreX = std::clamp(centreX, arm + unit + 1, frame.width - arm - unit - 1);
    centreY = std::clamp(centreY, arm + unit + 1, frame.height - arm - unit - 1);

    const double scale = frame.uiScale, offsetX = frame.uiOffsetX, offsetY = frame.uiOffsetY;
    frame.uiScale = 1.0;
    frame.uiOffsetX = frame.uiOffsetY = 0.0;

    const Color shadow = colorHex("#020604"), ink = colorHex("#d7ff54");
    const int x = centreX - unit / 2, y = centreY - unit / 2;
    // Two passes, outline then ink: a reticle that is one colour vanishes over
    // the pale chequerboard floor and over the near-black ceiling.
    const auto stroke = [&](int left, int top, int width, int height, bool outline) {
        const int border = outline ? unit : 0;
        frame.rectangle(left - border, top - border, width + 2 * border, height + 2 * border,
                        outline ? shadow : ink);
    };
    for (int pass = 0; pass < 2; ++pass) {
        const bool outline = pass == 0;
        stroke(x, y, unit, unit, outline);
        stroke(centreX - arm, y, length, unit, outline);
        stroke(centreX + gap, y, length, unit, outline);
        stroke(x, centreY - arm, unit, length, outline);
        stroke(x, centreY + gap, unit, length, outline);
    }

    frame.uiScale = scale;
    frame.uiOffsetX = offsetX;
    frame.uiOffsetY = offsetY;
}

}  // namespace gtc::native
