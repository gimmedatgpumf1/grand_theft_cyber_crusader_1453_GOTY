#pragma once

// The whole of the interface, for now: one strip across the top of the screen
// with the frame rate in it. The overlay that used to live here was deleted
// rather than repaired, and this is the floor the next thing gets built on.
//
// It is a translucent strip and a line of the game's own typeface -- the
// proportional sans the rest of the game uses, not a pixel font. A 2003 game on
// a 2003 screen had antialiased type, and a box in the corner that looks like a
// calculator is a different game.

#include "client/raster.h"

namespace gtc::native {

// Distance from the top of the window to the top of the strip, in pixels.
constexpr int kFpsCounterMargin = 10;
// The strip is centred: this is how far its middle may sit from the window's.
constexpr int kFpsCounterCentreTolerance = 1;

// Where the strip and its text land for a frame of this size. Exposed so a check
// can be written against the drawing rather than against a copy of the layout
// arithmetic -- a test that recomputes the answer can only agree with itself.
struct FpsCounterLayout {
    int left = 0;        // strip
    int top = 0;
    int width = 0;
    int height = 0;
    int textLeft = 0;    // the text's own box, inside the strip
    int textTop = 0;
    int textWidth = 0;
    int textHeight = 0;
};

FpsCounterLayout layoutFpsCounter(int frameWidth, int frameHeight, double framesPerSecond);

// Draws `<frames per second> FPS` centred at the top of the frame, on a
// translucent strip. Drawn by the host over the finished picture.
void drawFpsCounter(NativeFrame& frame, double framesPerSecond);

// The height the text is drawn at for a frame of this size, in pixels: never
// below the 20-pixel floor, and larger on larger windows so it stays readable.
int fpsCounterTextHeight(int frameHeight);

}  // namespace gtc::native
