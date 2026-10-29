#pragma once

// One place for the shapes and colours the interface is made of, so the pieces
// that are rebuilt one at a time do not drift into three different panels.

#include "client/raster.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {

// A strip of picture: dark, half strength, no border. A 2003 interface was not
// a black box, it was a tint you could still see the game through.
constexpr const char* kPanelColour = "#05090b";
// A strip the game shows through, and a panel that has to be read. The frame
// rate can afford to be faint; a list of keys over a pale floor cannot.
constexpr double kPanelOpacity = 0.5;
constexpr double kMenuPanelOpacity = 0.72;
// Near-white for anything that has to be read, desaturated green-grey for the
// labels underneath it.
constexpr const char* kInkColour = "#f2f7ea";
constexpr const char* kMutedColour = "#a9bdb1";

// A filled rectangle in pixels, whatever transform the frame happens to carry.
// Every piece of the interface is positioned against the window, not against the
// old 640x480 grid, so the transform is neutralised for the call and put back.
inline void fillPanel(NativeFrame& frame, int x, int y, int width, int height, double opacity = kPanelOpacity) {
    const double scale = frame.uiScale, offsetX = frame.uiOffsetX, offsetY = frame.uiOffsetY;
    frame.uiScale = 1.0;
    frame.uiOffsetX = frame.uiOffsetY = 0.0;
    frame.blendRectangle(x, y, width, height, colorHex(kPanelColour), opacity);
    frame.uiScale = scale;
    frame.uiOffsetX = offsetX;
    frame.uiOffsetY = offsetY;
}

// Text in pixels, never below the 20-pixel floor this game's text has always
// had, and larger on larger windows. `factor` is the size at 480 pixels tall.
inline int interfaceTextHeight(int frameHeight, double factor) {
    const int scaled = static_cast<int>(lround(factor * (frameHeight < 1 ? 1 : frameHeight) / 480.0));
    return scaled < 20 ? 20 : (scaled > 48 ? 48 : scaled);
}

// Distance between lines of a list, for a given text size.
inline int lineGap(int textHeight) { return textHeight * 3 / 4; }

// A round shape, in pixels, with the frame's transform already neutralised by
// the caller. Drawn as one span a row: at forty rows it is a disc, and a disc is
// the only thing in this interface that is not a rectangle.
inline void fillDisc(NativeFrame& frame, int cx, int cy, int radius, Color color, double opacity) {
    if (radius <= 0) return;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int half = static_cast<int>(std::sqrt(static_cast<double>(radius) * radius - dy * dy));
        frame.blendRectangle(cx - half, cy + dy, 2 * half + 1, 1, color, opacity);
    }
}

// The outline of one: two spans a row, from the inner radius out to the outer.
inline void ringDisc(NativeFrame& frame, int cx, int cy, int radius, int thickness, Color color, double opacity) {
    if (radius <= 0 || thickness <= 0) return;
    const int inner = std::max(0, radius - thickness);
    for (int dy = -radius; dy <= radius; ++dy) {
        const int outerHalf = static_cast<int>(std::sqrt(static_cast<double>(radius) * radius - dy * dy));
        const int innerHalf = std::abs(dy) >= inner
            ? 0
            : static_cast<int>(std::sqrt(static_cast<double>(inner) * inner - dy * dy));
        if (innerHalf < outerHalf) {
            frame.blendRectangle(cx - outerHalf, cy + dy, outerHalf - innerHalf, 1, color, opacity);
            frame.blendRectangle(cx + innerHalf + 1, cy + dy, outerHalf - innerHalf, 1, color, opacity);
        } else {
            frame.blendRectangle(cx - outerHalf, cy + dy, 2 * outerHalf + 1, 1, color, opacity);
        }
    }
}

}  // namespace gtc::native
