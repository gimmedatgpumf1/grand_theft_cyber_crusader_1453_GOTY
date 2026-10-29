#pragma once

// The bottom of the screen: the three camera modes, their keys, and which one is
// running. It is the only piece of the interface that is always there, so it is
// the quietest of them -- a strip, three labels, and the active one marked.

#include "client/renderer.h"

namespace gtc::native {

// Where the strip and its three blocks land for a frame of this size. The blocks
// are measured in the same order the keys are pressed: 1 third person, 2 god,
// 3 first person.
struct ModeBarLayout {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    int textHeight = 0;
    int blockLeft[3] = {0, 0, 0};
    int blockWidth[3] = {0, 0, 0};
    int underlineTop = 0;
    int underlineHeight = 0;
};

ModeBarLayout layoutModeBar(int frameWidth, int frameHeight);

// Draws `1 3RD PERSON   2 GOD   3 1ST PERSON` along the bottom edge, the active
// mode in the reading colour with a rule under it and the other two dimmed.
// `hovered` is the block the pointer is over, or -1 for none: the strip is
// clickable whenever the mouse is free, so it has to look like it.
void drawModeBar(NativeFrame& frame, ViewMode mode, int hovered = -1);

// Which block is under this point: 0 third person, 1 god, 2 first person, or -1
// for a point off the strip. The gaps between blocks belong to their neighbours,
// so there is no dead column to click into.
int modeBarAt(const ModeBarLayout& layout, int x, int y);

}  // namespace gtc::native
