#pragma once

// F1: the need controls. Two current values and the eight keys that change them,
// on a translucent panel in the middle of the screen.
//
// The keys themselves are handled by the client, which is the only thing that
// knows what a cheat is; this file only knows how to draw one, and takes the two
// percentages as arguments so it can be checked without a server.

#include "client/raster.h"

namespace gtc::native {

// Where every part of the panel lands, for a frame of this size. Exposed so the
// check can be written against the drawing rather than against a copy of the
// arithmetic.
struct CheatMenuLayout {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    int textHeight = 0;    // body size in pixels
    int titleHeight = 0;   // header size in pixels
    int contentLeft = 0;   // left edge of the two columns
    int keyWidth = 0;      // widest of the eight key glyphs
    int leftColumnRight = 0;   // exclusive end of the left label column
    int rightKeyLeft = 0;      // left edge of the right column's keys
    int titleTop = 0;
    int statusTop = 0;
    int firstRowTop = 0;   // top of row 0; rows are rowGap apart
    int rowGap = 0;
    int footerTop = 0;
};

CheatMenuLayout layoutCheatMenu(int frameWidth, int frameHeight);

// `bladderPercent` and `bowelPercent` are what the server says the local citizen
// needs are; the panel shows them, clamped, next to the keys that change them.
void drawCheatMenu(NativeFrame& frame, int bladderPercent, int bowelPercent);

}  // namespace gtc::native
