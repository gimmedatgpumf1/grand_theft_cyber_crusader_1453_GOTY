#pragma once

// The whole of the interface, for now: one box in the corner that says how fast
// the frame is arriving. Everything the old overlay did was deleted rather than
// repaired, so this file is the floor the next thing gets built on -- a box, a
// few digits, and nothing that has to be trusted from another subsystem.

#include "client/raster.h"

namespace gtc::native {

// Where the counter sits, in the 640x480 logical space the frame scales from.
constexpr int kFpsCounterX = 12;
constexpr int kFpsCounterY = 10;
// Height of one digit in logical pixels. Big enough to read at 1080p without the
// counter becoming the subject of the picture.
constexpr int kFpsCounterDigit = 2;

// Draws `FPS <n>` on a filled box. Runs on the host over the finished picture,
// in the same pass that has always put the picture on screen.
void drawFpsCounter(NativeFrame& frame, double framesPerSecond);

}  // namespace gtc::native
