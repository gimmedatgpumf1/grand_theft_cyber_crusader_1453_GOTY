#pragma once

// One place for the shapes and colours the interface is made of, so the pieces
// that are rebuilt one at a time do not drift into three different panels.

#include "client/raster.h"

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

}  // namespace gtc::native
