#include "client/fps_counter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace gtc::native {
namespace {

// No glyph tables here any more. The number is set in the same antialiased
// proportional sans the game has always used, at a size measured in pixels, so
// it reads like a HUD from this century's first decade and not like a sprite.

std::string formatRate(double framesPerSecond) {
    if (!std::isfinite(framesPerSecond) || framesPerSecond < 0.0) framesPerSecond = 0.0;
    char number[16];
    // Rounded, not truncated: a counter that says nineteen while the machine is
    // reporting twenty is the sort of small lie that makes a counter useless.
    std::snprintf(number, sizeof(number), "%.0f", framesPerSecond);
    return std::string(number);
}

}  // namespace

int fpsCounterTextHeight(int frameHeight) {
    // 13 pixels at 640x480, and it grows with the window from there. The floor is
    // 20 because that is the size this game's text has always been drawn at, and
    // because anything smaller is unreadable on a 1080p panel.
    const int scaled = static_cast<int>(std::lround(13.0 * std::max(1, frameHeight) / 480.0));
    return std::clamp(scaled, 20, 44);
}

FpsCounterLayout layoutFpsCounter(int frameWidth, int frameHeight, double framesPerSecond) {
    FpsCounterLayout layout;
    const std::string text = formatRate(framesPerSecond) + " FPS";
    const int pixelHeight = fpsCounterTextHeight(frameHeight);
    layout.textWidth = textWidthPixels(text, pixelHeight);
    layout.textHeight = textHeightPixels(pixelHeight);
    if (layout.textWidth <= 0 || layout.textHeight <= 0) return layout;   // no face on disk

    const int padding = std::max(6, pixelHeight / 3);
    layout.width = layout.textWidth + padding * 2;
    layout.height = layout.textHeight + padding * 2;
    // Centred on the window, not on the old 640x480 grid: the strip's own middle
    // is placed on the window's middle, with the remainder split between the two
    // sides so it is never a pixel off to one side.
    layout.left = (frameWidth - layout.width) / 2;
    layout.top = kFpsCounterMargin;
    layout.textLeft = layout.left + padding;
    layout.textTop = layout.top + padding;
    return layout;
}

void drawFpsCounter(NativeFrame& frame, double framesPerSecond) {
    const std::string text = formatRate(framesPerSecond) + " FPS";
    const int pixelHeight = fpsCounterTextHeight(frame.height);
    const FpsCounterLayout layout = layoutFpsCounter(frame.width, frame.height, framesPerSecond);
    if (layout.width <= 0 || layout.height <= 0) return;

    // Pixel coordinates, not HUD-scale ones: the strip is sized against the
    // window, and `blendRectangle` and the text both read the frame's transform.
    const double previousScale = frame.uiScale;
    const double previousOffsetX = frame.uiOffsetX;
    const double previousOffsetY = frame.uiOffsetY;
    frame.uiScale = 1.0;
    frame.uiOffsetX = 0.0;
    frame.uiOffsetY = 0.0;

    // Transparent, not opaque: the world stays visible through it at half
    // strength, which is what a HUD strip is for. The overlap of the panel and
    // the picture is the only thing that makes it readable over a bright floor.
    frame.blendRectangle(layout.left, layout.top, layout.width, layout.height, colorHex("#05090b"), 0.5);
    drawTextPixels(frame, layout.textLeft, layout.textTop, text, colorHex("#f2f7ea"), pixelHeight);

    frame.uiScale = previousScale;
    frame.uiOffsetX = previousOffsetX;
    frame.uiOffsetY = previousOffsetY;
}

}  // namespace gtc::native
