#pragma once

// The needs panel, bottom right: what the body wants, as bars. Small, quiet, and
// always there -- the one piece of state a player checks without stopping.
//
// Three rows, shortest label that still says what it is: PISS, SHIT, and HP for
// the thing that is not a need but belongs with them. The bar carries the reading;
// the number is there for the times a bar is not enough.

#include "client/raster.h"

namespace gtc::native {

struct NeedsStatus {
    int bladder = 0;   // PISS, 0..100
    int bowel = 0;     // SHIT, 0..100
    int health = 0;    // HP, 0..100
};

constexpr int kNeedsRowCount = 3;

struct NeedsPanelLayout {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    int textHeight = 0;
    int rowTop[kNeedsRowCount] = {0, 0, 0};
    int rowHeight = 0;
    int rowGap = 0;
    int barLeft = 0;
    int barWidth = 0;
    int barHeight = 0;
    int valueRight = 0;    // right edge of the percentage column
};

// Anchored to the bottom right corner. `modeBarTop` and `modeBarWidth` describe the
// mode bar, or a negative top when there is none: on a narrow window the two would
// overlap, and the panel steps up above the bar rather than being drawn through it.
NeedsPanelLayout layoutNeedsPanel(int frameWidth, int frameHeight, int modeBarTop = -1, int modeBarWidth = 0);

void drawNeedsPanel(NativeFrame& frame, const NeedsPanelLayout& layout, const NeedsStatus& status);

}  // namespace gtc::native
